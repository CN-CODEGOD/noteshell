// main.cpp

// editor.h
#pragma once
#include <string>
#include <vector>
#include <curses.h>
#include <locale>
#include <fstream>
#include <codecvt>
#include <cwctype>
#include <windows.h>

static std::wstring get_exe_dir() {
	TCHAR buffer[MAX_PATH];
	GetModuleFileName(NULL, buffer, MAX_PATH);
	std::wstring fullPath(buffer);
	size_t pos = fullPath.find_last_of(L"\\/");
	return fullPath.substr(0, pos);
}

class Editor {
public:
	Editor();
	void run();

private:
	enum Mode { NORMAL, INSERT, COMMAND };
	struct Buffer {
		std::wstring filename;
		std::vector<std::wstring> lines;
		int cursor_x = 0, cursor_y = 0; // ÎÄ±¾×ø±ê
	};

	std::vector<Buffer> buffers;
	int current_buffer;
	Mode mode;
	std::wstring command_buffer;

	void render();
	void process_input();
	void draw_line(const std::wstring& line, int row);
	void move_cursor_left();
	void move_cursor_right();
	void move_cursor_up();
	void move_cursor_down();
	void insert_char(wchar_t ch);
	int char_width(wchar_t ch);

	void enter_command_mode();
	void execute_command();
	void save_file();
	void switch_buffer();
};

Editor::Editor() : current_buffer(0), mode(INSERT) {
	setlocale(LC_ALL, "");
	buffers.push_back({ L"file1.txt", {L""} });
	buffers.push_back({ L"file2.txt", {L""} });
}

int Editor::char_width(wchar_t ch) {
	if ((ch >= 0x4E00 && ch <= 0x9FFF) || ch >= 0x3400)
		return 2;
	return 1;
}

void Editor::draw_line(const std::wstring& line, int row) {
	mvprintw(row, 0, "%4d ", row + 1);
	int col = 5;
	for (wchar_t ch : line) {
		mvaddnwstr(row, col, &ch, 1);
		col += char_width(ch);
		if (col >= COLS) break;
	}
}

void Editor::render() {
	clear();
	const Buffer& buf = buffers[current_buffer];
	int max_lines = min((int)buf.lines.size(), LINES - 2);
	for (int i = 0; i < max_lines; ++i) {
		draw_line(buf.lines[i], i);
	}
	move(LINES - 1, 0);
	clrtoeol();
	if (mode == COMMAND)
		printw(":%ls", command_buffer.c_str());
	else
		printw("-- %s --  %ls  command =>:w save :q exit tab switch", mode == INSERT ? "INSERT" : "NORMAL", buf.filename.c_str());

	int x = 5;
	for (int i = 0; i < buf.cursor_x && i < buf.lines[buf.cursor_y].size(); ++i)
		x += char_width(buf.lines[buf.cursor_y][i]);
	move(buf.cursor_y, x);
	refresh();
}

void Editor::insert_char(wchar_t ch) {
	Buffer& buf = buffers[current_buffer];
	if (buf.cursor_y >= buf.lines.size()) buf.lines.resize(buf.cursor_y + 1);
	auto& line = buf.lines[buf.cursor_y];
	if (buf.cursor_x > line.size()) buf.cursor_x = line.size();
	line.insert(line.begin() + buf.cursor_x, ch);
	buf.cursor_x++;
}

void Editor::move_cursor_left() {
	Buffer& buf = buffers[current_buffer];
	if (buf.cursor_x > 0) buf.cursor_x--;
}

void Editor::move_cursor_right() {
	Buffer& buf = buffers[current_buffer];
	if (buf.cursor_x < buf.lines[buf.cursor_y].size()) buf.cursor_x++;
}

void Editor::move_cursor_up() {
	Buffer& buf = buffers[current_buffer];
	if (buf.cursor_y > 0) {
		buf.cursor_y--;
		if (buf.cursor_x > buf.lines[buf.cursor_y].size())
			buf.cursor_x = buf.lines[buf.cursor_y].size();
	}
}

void Editor::move_cursor_down() {
	Buffer& buf = buffers[current_buffer];
	if (buf.cursor_y + 1 < buf.lines.size()) {
		buf.cursor_y++;
		if (buf.cursor_x > buf.lines[buf.cursor_y].size())
			buf.cursor_x = buf.lines[buf.cursor_y].size();
	}
}

void Editor::enter_command_mode() {
	command_buffer.clear();
	mode = COMMAND;
}

void Editor::execute_command() {
	if (command_buffer == L"w") save_file();
	else if (command_buffer == L"q") endwin(), exit(0);
	else if (command_buffer == L"b") switch_buffer();
	mode = NORMAL;
}

void Editor::save_file() {
	Buffer& buf = buffers[current_buffer];

	std::wstring path = get_exe_dir() + L"\\" + buf.filename;
	std::wofstream file(path);
	file.imbue(std::locale("", std::locale::ctype));
	for (auto& line : buf.lines) file << line << L"\n";
}

void Editor::switch_buffer() {
	current_buffer = (current_buffer + 1) % buffers.size();
}

void Editor::process_input() {
	wint_t ch;
	get_wch(&ch);
	Buffer& buf = buffers[current_buffer];

	if (mode == INSERT) {
		if (ch == 27) mode = NORMAL; // ESC
		else if (ch == L'\n' || ch == 13) {
			std::wstring remain = buf.lines[buf.cursor_y].substr(buf.cursor_x);
			buf.lines[buf.cursor_y].resize(buf.cursor_x);
			buf.lines.insert(buf.lines.begin() + buf.cursor_y + 1, remain);
			buf.cursor_y++;
			buf.cursor_x = 0;
		}
		else insert_char(ch);
	}
	else if (mode == NORMAL) {
		if (ch == L'i') mode = INSERT;
		else if (ch == L':') enter_command_mode();
		else if (ch == KEY_LEFT) move_cursor_left();
		else if (ch == KEY_RIGHT) move_cursor_right();
		else if (ch == KEY_UP) move_cursor_up();
		else if (ch == KEY_DOWN) move_cursor_down();
		else if (ch == 9) switch_buffer(); // TAB
	}
	else if (mode == COMMAND) {
		if (ch == 10 || ch == 13) execute_command();
		else if (ch == 27) mode = NORMAL;
		else if (iswprint(ch)) command_buffer += ch;
	}
}

void Editor::run() {
	initscr();
	raw();
	keypad(stdscr, TRUE);
	noecho();
	while (true) {
		render();
		process_input();
	}
	endwin();
}

int main() {
	Editor editor;
	editor.run();
	return 0;
}