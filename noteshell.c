// 需要 POSIX 2008 才能暴露 getaddrinfo / struct addrinfo / SO_RCVTIMEO
// （在 -std=c99 下默认隐藏），必须在任何头文件之前定义
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_NOTES 1000
#define MAX_CONTENT 1024
#define MAX_TIMESTAMP 64
#define HISTORY_FILE ".noteshell_history"
#define SYNC_FILE ".noteshell_sync"      // noteWeb 同步用的序号文件
#define SYNC_URL_FILE ".noteshell_sync_url" // 可选：同步目标 URL（默认 http://127.0.0.1:8000）
#define ARCHIVE_DIR ".noteshell_history.d" // 多版本归档目录
#define ARCHIVE_KEEP 10                   // 归档最多保留多少份（超出清最旧）

// 笔记结构体
typedef struct {
    char timestamp[MAX_TIMESTAMP];
    char content[MAX_CONTENT];
} Note;

// 全局变量
Note notes[MAX_NOTES];
int note_count = 0;
int current_page = 0;
int notes_per_page = 10;

struct termios orig_termios;

// 输入历史（供 ↑/↓ 浏览使用）
#define MAX_LINE_HISTORY 200
char line_history[MAX_LINE_HISTORY][MAX_CONTENT];
int line_history_count = 0;

// ---------- noteWeb 同步 ----------
// 每次新增/删除笔记后，同步序号 +1。noteWeb 读取该文件，
// 发现序号变化就把当前笔记列表拉过去，实现“单会话一次”同步。
long sync_seq = 0;

// 前置声明
void bump_sync(void);

void get_sync_path(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (home == NULL) home = ".";
    snprintf(path, size, "%s/%s", home, SYNC_FILE);
}

long load_sync_seq() {
    char path[512];
    get_sync_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    long v = 0;
    if (f) {
        if (fscanf(f, "%ld", &v) != 1) v = 0;
        fclose(f);
    }
    return v;
}

void save_sync_seq(long v) {
    char path[512];
    get_sync_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%ld\n", v);
        fclose(f);
    }
}

// 通过 HTTP POST 把消息推给 noteWeb（失败静默，不影响本地使用）
// 直接建立 TCP 连接发送，不经过 shell，因此双引号/反斜杠/中文都安全
// 解析 "http://host[:port]/path"，失败返回 -1
static int parse_http_url(const char *url, char *host, size_t host_size,
                          char *port, size_t port_size,
                          char *path, size_t path_size) {
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        return -1; // 不支持 https（保持零依赖，不引入 TLS）
    }

    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : p + strlen(p);

    // 拆分 host 与可选 port
    const char *colon = memchr(p, ':', host_end - p);
    if (colon != NULL) {
        size_t hl = colon - p;
        if (hl == 0 || hl >= host_size) return -1;
        memcpy(host, p, hl);
        host[hl] = '\0';
        size_t pl = host_end - (colon + 1);
        if (pl == 0 || pl >= port_size) return -1;
        memcpy(port, colon + 1, pl);
        port[pl] = '\0';
    } else {
        size_t hl = host_end - p;
        if (hl == 0 || hl >= host_size) return -1;
        memcpy(host, p, hl);
        host[hl] = '\0';
        strncpy(port, "80", port_size - 1);
        port[port_size - 1] = '\0';
    }

    // 路径：默认 /
    if (slash != NULL) {
        strncpy(path, slash, path_size - 1);
        path[path_size - 1] = '\0';
    } else {
        strncpy(path, "/", path_size - 1);
        path[path_size - 1] = '\0';
    }
    return 0;
}

// 连接 host:port，成功返回 fd，失败返回 -1
static int http_connect(const char *host, const char *port) {
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break; // 成功
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

// 发送全部数据；返回 0 成功，-1 失败
static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

void sync_http_post(const char *message) {
    const char *base = getenv("NOTESHELL_SYNC_URL");
    if (base == NULL || base[0] == '\0') base = "http://127.0.0.1:8000";

    char host[256], port[16], path[512];
    if (parse_http_url(base, host, sizeof(host), port, sizeof(port),
                       path, sizeof(path)) != 0) {
        return; // 不支持的 URL（如 https），静默跳过
    }

    // 目标路径固定为 /api/push（忽略 base 自带的路径）
    int fd = http_connect(host, port);
    if (fd < 0) return; // 服务器没开，静默跳过

    size_t body_len = strlen(message);

    // 组装 HTTP/1.1 请求头 + body
    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "POST /api/push HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        host, port, body_len);
    if (hlen <= 0 || (size_t)hlen >= sizeof(header)) {
        close(fd);
        return;
    }

    int ok = 0;
    if (send_all(fd, header, (size_t)hlen) == 0) {
        if (send_all(fd, message, body_len) == 0) {
            ok = 1;
        }
    }

    // 尽力读取响应（不是必须，但可确保服务端处理完）
    if (ok) {
        char rbuf[512];
        // 设置短接收超时，避免服务器不响应时阻塞
        struct timeval tv;
        tv.tv_sec = 2; tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        while (recv(fd, rbuf, sizeof(rbuf), 0) > 0) { /* drain */ }
    }

    close(fd);
}

// 恢复终端设置
void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// 设置终端为 raw 模式
void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// 获取当前时间戳
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// 获取历史文件路径
void get_history_path(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (home == NULL) {
        home = ".";
    }
    snprintf(path, size, "%s/%s", home, HISTORY_FILE);
}

// 获取归档目录路径
void get_archive_dir(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (home == NULL) home = ".";
    snprintf(path, size, "%s/%s", home, ARCHIVE_DIR);
}

// 把归档文件名（形如 20261008-094100-123456）转成可排序字符串。
// 回退：若目录项不是我们期待的格式，用名字本身排序（仍能稳定清理）。
static int archive_name_cmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

// 清理归档目录，只保留最近 ARCHIVE_KEEP 份（按名字升序，删最旧）。
static void prune_archives(const char *dir) {
    DIR *d = opendir(dir);
    if (d == NULL) return;

    static char names[512][128];
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue; // 跳过 . 与 .. 及隐藏项
        if (count < 512) {
            strncpy(names[count], ent->d_name, sizeof(names[count]) - 1);
            names[count][sizeof(names[count]) - 1] = '\0';
            count++;
        }
    }
    closedir(d);

    if (count <= ARCHIVE_KEEP) return;

    qsort(names, count, sizeof(names[0]), archive_name_cmp);

    // 名字按时间排序（ts 前缀），保留最后 ARCHIVE_KEEP 个，删前面的
    int to_delete = count - ARCHIVE_KEEP;
    for (int i = 0; i < to_delete; i++) {
        char full[700];
        snprintf(full, sizeof(full), "%s/%s", dir, names[i]);
        remove(full);
    }
}

// 把当前历史文件归档为 ~/.noteshell_history.d/<时间戳>，并保留最近 ARCHIVE_KEEP 份。
// 在“覆盖写正式文件之前”调用，保存的是上一个版本。
static void archive_current_history(const char *path) {
    if (access(path, F_OK) != 0) return; // 没有旧文件，无需归档

    char dir[600];
    get_archive_dir(dir, sizeof(dir));
    // 目录已存在时 mkdir 会返回 EEXIST，忽略即可
    mkdir(dir, 0700);

    // 时间戳文件名：YYYYMMDD-HHMMSS-微秒，保证同一秒内多次保存也不重名
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm_info);
    struct timeval tv;
    gettimeofday(&tv, NULL);

    char dest[700];
    snprintf(dest, sizeof(dest), "%s/%s-%06ld", dir, stamp, (long)tv.tv_usec);

    // 用 copy 而非 rename，保证归档失败也不影响主文件原子替换流程
    FILE *src = fopen(path, "r");
    if (src == NULL) return;
    FILE *dst = fopen(dest, "w");
    if (dst == NULL) { fclose(src); return; }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
    }
    fclose(src);
    fflush(dst);
    fsync(fileno(dst));
    fclose(dst);

    prune_archives(dir);
}

// 加载历史记录
void load_history() {
    char path[512];
    get_history_path(path, sizeof(path));
    
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    
    note_count = 0;
    while (note_count < MAX_NOTES && fgets(notes[note_count].timestamp, MAX_TIMESTAMP, file) != NULL) {
        // 移除换行符
        notes[note_count].timestamp[strcspn(notes[note_count].timestamp, "\n")] = 0;
        
        if (fgets(notes[note_count].content, MAX_CONTENT, file) != NULL) {
            notes[note_count].content[strcspn(notes[note_count].content, "\n")] = 0;
            note_count++;
        }
    }
    
    fclose(file);
}

// 保存历史记录
// 原子写：先写临时文件 fsync，再把旧文件备份为 .bak，最后 rename 覆盖。
// 这样即使写到一半崩溃，原文件也不会被截断。
void save_history() {
    char path[512];
    char tmp[600];
    char bak[600];
    get_history_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    snprintf(bak, sizeof(bak), "%s.bak", path);

    FILE *file = fopen(tmp, "w");
    if (file == NULL) {
        return; // 打不开临时文件，放弃（不影响内存中的笔记）
    }

    for (int i = 0; i < note_count; i++) {
        fprintf(file, "%s\n%s\n", notes[i].timestamp, notes[i].content);
    }

    // 确保数据真正落到磁盘，再 rename
    fflush(file);
    fsync(fileno(file));
    fclose(file);

    // 旧文件存在则：
    //   1) 归档为 ~/.noteshell_history.d/<时间戳>（多版本，保留最近ARCHIVE_KEEP份）
    //   2) 同时仍然保留一份 .bak（单份快速回退，兼容旧习惯）
    if (access(path, F_OK) == 0) {
        archive_current_history(path);
        rename(path, bak);
    }

    // 原子替换
    if (rename(tmp, path) != 0) {
        // rename 失败时尽量恢复原文件
        if (access(bak, F_OK) == 0) {
            rename(bak, path);
        }
        remove(tmp);
    }
}

// 添加新笔记
void add_note(const char *content) {
    if (note_count >= MAX_NOTES) {
        // 删除最早的笔记
        for (int i = 0; i < note_count - 1; i++) {
            notes[i] = notes[i + 1];
        }
        note_count--;
    }
    
    get_timestamp(notes[note_count].timestamp, MAX_TIMESTAMP);
    strncpy(notes[note_count].content, content, MAX_CONTENT - 1);
    notes[note_count].content[MAX_CONTENT - 1] = '\0';
    note_count++;
    
    save_history();
    bump_sync();
}

// 同步序号自增，并推送到 noteWeb
void bump_sync() {
    sync_seq = load_sync_seq() + 1;
    save_sync_seq(sync_seq);

    // 推送到 noteWeb：TAB 分隔，格式 seq<TAB>count<TAB>timestamp<TAB>content
    Note *last = &notes[note_count - 1];
    char msg[2200];
    snprintf(msg, sizeof(msg), "%ld\t%d\t%s\t%s",
             sync_seq, note_count, last->timestamp, last->content);
    sync_http_post(msg);
}

// 清屏
void clear_screen() {
    printf("\033[2J\033[H");
}

// 移动光标到指定位置
void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

// 获取终端大小
void get_terminal_size(int *rows, int *cols) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        *rows = w.ws_row;
        *cols = w.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

// “按任意键返回”的等待。
//
// 设计要点（为什么这样写）：
//   raw 模式下单个按键不带换行，无法用“读到换行”来界定一次输入。
//   因此我们只消费“转义序列的残留字节”，绝不丢弃普通可打印字符——
//   因为那可能是用户极快连打、已经进入 tty 缓冲区的下一条命令。
//
// 做法：
//   1) 读第一个字节；若它不是 ESC，就到此为止（普通按键，直接返回）。
//      此时不再读任何字节，绝不碰用户的下一条输入。
//   2) 若是 ESC，说明可能是方向键/功能键序列（ESC [ A ...）。
//      只把转义序列自己的后续字节读掉（带极短超时，避免阻塞）：
//      紧跟的 '[' / 'O' 以及一个终结字节（字母/~）。
//   3) 转义序列结束后立即停止，同样不碰后续可打印字符。
void wait_any_key() {
    char ch;
    if (read(STDIN_FILENO, &ch, 1) <= 0) return;

    // 非 ESC：一次性普通按键，直接返回（不动缓冲区里的其它字节）
    if (ch != '\033') return;

    // 是 ESC：只在很短的时间内尝试读转义序列的后续字节。
    // 用 select 超时，避免用户只单独按了 ESC 时无限阻塞。
    // 只读取“转义序列结构内”的字节，序列一结束就停。
    for (int i = 0; i < 2; i++) {
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 30000; // 30ms：足够衔接同一序列的后续字节

        int r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (r <= 0) return; // 没有后续字节：ESC 单独按下，结束

        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) return;

        if (i == 0) {
            // 第一个后续字节应是 '[' 或 'O'；否则不是标准转义序列，
            // 这种情况不再继续读，避免误吞普通字符。
            if (c != '[' && c != 'O') return;
        } else {
            // 第二个后续字节是序列终结符（如 'A'/'B'/'C'/'D' 或 '~'），
            // 读到它序列即结束，直接返回。
            return;
        }
    }
}

// 显示帮助信息
void show_help() {
    clear_screen();
    printf("\033[1;36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    NoteShell 帮助                        ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
    
    printf("\033[1;33m命令列表:\033[0m\n\n");
    printf("  \033[1;32m/help\033[0m     - 显示此帮助信息\n");
    printf("  \033[1;32m/clear\033[0m    - 清屏\n");
    printf("  \033[1;32m/list\033[0m     - 列出所有笔记（分页）\n");
    printf("  \033[1;32m/history\033[0m  - 按时间翻查历史（分页，可跟关键词）\n");
    printf("  \033[1;32m/search <关键词>\033[0m - 搜索笔记\n");
    printf("  \033[1;32m/edit <编号> <新内容>\033[0m - 修改指定笔记\n");
    printf("  \033[1;32m/delete <编号>\033[0m  - 删除指定笔记\n");
    printf("  \033[1;32m/export [文件]\033[0m - 导出笔记为 Markdown（默认 noteshell_export.md）\n");
    printf("  \033[1;32m/count\033[0m    - 显示笔记总数\n");
    printf("  \033[1;32m/exit\033[0m     - 退出程序\n\n");
    
    printf("\033[1;33m快捷键:\033[0m\n\n");
    printf("  \033[1;32m↑/↓ 或 n/p\033[0m - 浏览/翻页历史记录\n");
    printf("  \033[1;32mEnter\033[0m    - 保存笔记\n");
    printf("  \033[1;32mq\033[0m        - 从浏览/翻查视图返回\n");
    printf("  \033[1;32mCtrl+C\033[0m   - 强制退出\n\n");
    
    printf("\033[1;33m使用说明:\033[0m\n\n");
    printf("  直接输入文本即可创建新笔记，系统会自动添加时间戳。\n");
    printf("  使用方向键可以浏览历史记录。\n");
    printf("  笔记原子写入 ~/.noteshell_history；每版归档在 \n");
    printf("  ~/.noteshell_history.d/（保留最近 %d 份），并保留一份 .bak。\n", ARCHIVE_KEEP);
    printf("  用 /history 分页翻查全部历史。\n\n");
    
    printf("\033[2m按任意键返回...\033[0m");
    fflush(stdout);
    
    wait_any_key();
}

// 显示笔记列表
void show_notes() {
    clear_screen();
    printf("\033[1;36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║                   NoteShell 笔记列表                     ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
    
    if (note_count == 0) {
        printf("\033[1;33m暂无笔记\033[0m\n");
        printf("\n\033[2m按任意键返回...\033[0m");
        fflush(stdout);
        char ch;
        read(STDIN_FILENO, &ch, 1);
        return;
    }
    
    int rows, cols;
    get_terminal_size(&rows, &cols);
    notes_per_page = rows - 10;
    if (notes_per_page < 5) notes_per_page = 5;
    
    int total_pages = (note_count + notes_per_page - 1) / notes_per_page;
    if (current_page >= total_pages) current_page = total_pages - 1;
    if (current_page < 0) current_page = 0;
    
    int start = current_page * notes_per_page;
    int end = start + notes_per_page;
    if (end > note_count) end = note_count;
    
    for (int i = start; i < end; i++) {
        printf("\033[1;32m[%3d]\033[0m \033[2m%s\033[0m\n", i + 1, notes[i].timestamp);
        printf("      %s\n\n", notes[i].content);
    }
    
    printf("\033[1;36m第 %d/%d 页 (共 %d 条笔记)\033[0m\n", 
           current_page + 1, total_pages, note_count);
    printf("\033[2m使用 ↑/↓ 翻页，按 q 返回...\033[0m");
    fflush(stdout);
    
    char ch;
    while (1) {
        read(STDIN_FILENO, &ch, 1);
        if (ch == 'q' || ch == 'Q') break;
        if (ch == '\033') {
            char seq[2];
            read(STDIN_FILENO, &seq[0], 1);
            read(STDIN_FILENO, &seq[1], 1);
            if (seq[0] == '[') {
                if (seq[1] == 'A') { // 上
                    if (current_page > 0) {
                        current_page--;
                        show_notes();
                        return;
                    }
                } else if (seq[1] == 'B') { // 下
                    if (current_page < total_pages - 1) {
                        current_page++;
                        show_notes();
                        return;
                    }
                }
            }
        }
    }
}

// 翻查历史（按时间顺序列出全部笔记，从旧到新，带序号与时间）
// 支持分页：↑/↓ 或 n/p 翻页，q 返回。
// 用法: /history          显示全部
//       /history <关键词>  只看某关键词（过滤后结果同样分页）
void show_history(const char *keyword) {
    // 先把匹配的下标收集起来（过滤后分页）
    static int idx[MAX_NOTES];
    int total = 0;
    for (int i = 0; i < note_count; i++) {
        if (keyword != NULL && keyword[0] != '\0' &&
            strstr(notes[i].content, keyword) == NULL) {
            continue;
        }
        idx[total++] = i;
    }

    clear_screen();
    printf("\033[1;36m");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║                 NoteShell 历史翻查                         ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");

    if (note_count == 0) {
        printf("\033[1;33m暂无历史笔记\033[0m\n");
        printf("\n\033[2m按任意键返回...\033[0m");
        fflush(stdout);
        wait_any_key();
        return;
    }

    if (total == 0) {
        printf("\033[1;33m没有匹配 \"%s\" 的历史笔记\033[0m\n", keyword);
        printf("\n\033[2m按任意键返回...\033[0m");
        fflush(stdout);
        wait_any_key();
        return;
    }

    // 分页参数：与 /list 类似，按终端高度算每页条数
    int rows, cols;
    get_terminal_size(&rows, &cols);
    int per_page = rows - 8;
    if (per_page < 3) per_page = 3;
    int total_pages = (total + per_page - 1) / per_page;
    int page = 0;

    while (1) {
        // 重画当前页（全屏清后重绘，避免残留）
        if (page > 0) clear_screen();
        printf("\033[1;36m");
        printf("╔═══════════════════════════════════════════════════════╗\n");
        printf("║                 NoteShell 历史翻查                         ║\n");
        printf("╚═══════════════════════════════════════════════════════╝\n");
        printf("\033[0m\n");

        if (keyword != NULL && keyword[0] != '\0') {
            printf("\033[1;35m过滤: \"%s\"\033[0m\n\n", keyword);
        }

        int start = page * per_page;
        int end = start + per_page;
        if (end > total) end = total;

        for (int k = start; k < end; k++) {
            int i = idx[k];
            // 序号按“全量位置”显示，方便配合 /delete 使用
            printf("\033[1;32m[%3d]\033[0m \033[2m%s\033[0m  \033[1;35m#%d/%d\033[0m\n",
                   i + 1, notes[i].timestamp, i + 1, note_count);
            printf("      %s\n\n", notes[i].content);
        }

        printf("\033[1;36m第 %d/%d 页 (共 %d 条", page + 1, total_pages, total);
        if (keyword != NULL && keyword[0] != '\0') {
            printf("，匹配 \"%s\"", keyword);
        }
        printf(")\033[0m\n");
        printf("\033[2m↑/↓ 或 n/p 翻页，q 返回...\033[0m");
        fflush(stdout);

        char ch;
        if (read(STDIN_FILENO, &ch, 1) <= 0) return;

        if (ch == 'q' || ch == 'Q') break;

        int go_prev = 0, go_next = 0;
        if (ch == 'n' || ch == 'N') go_next = 1;
        else if (ch == 'p' || ch == 'P') go_prev = 1;
        else if (ch == '\033') {
            // 方向键：ESC [ A/B
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) break;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) break;
            if (seq[0] == '[') {
                if (seq[1] == 'A') go_prev = 1;
                else if (seq[1] == 'B') go_next = 1;
            }
        }

        if (go_prev && page > 0) page--;
        if (go_next && page < total_pages - 1) page++;
    }
}

// 搜索笔记
void search_notes(const char *keyword) {
    printf("\033[1;36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║                   NoteShell 搜索结果                     ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
    
    int found = 0;
    for (int i = 0; i < note_count; i++) {
        if (strstr(notes[i].content, keyword) != NULL) {
            printf("\033[1;32m[%3d]\033[0m \033[2m%s\033[0m\n", i + 1, notes[i].timestamp);
            printf("      %s\n\n", notes[i].content);
            found++;
        }
    }
    
    if (found == 0) {
        printf("\033[1;33m未找到包含 \"%s\" 的笔记\033[0m\n", keyword);
    } else {
        printf("\033[1;36m找到 %d 条结果\033[0m\n", found);
    }
    
    printf("\n\033[2m按任意键返回...\033[0m");
    fflush(stdout);
    wait_any_key();
}

// 删除笔记
void delete_note(int index) {
    if (index < 1 || index > note_count) {
        printf("\033[1;31m错误: 无效的笔记编号\033[0m\n");
        return;
    }
    
    index--; // 转换为 0 基索引
    for (int i = index; i < note_count - 1; i++) {
        notes[i] = notes[i + 1];
    }
    note_count--;
    save_history();
    bump_sync();
    
    printf("\033[1;32m✓ 笔记已删除\033[0m\n");
}

// 编辑笔记（/edit <编号> <新内容>）：只改内容，时间戳保留原样（附编辑标记）
void edit_note(int index, const char *new_content) {
    if (index < 1 || index > note_count) {
        printf("\033[1;31m错误: 无效的笔记编号\033[0m\n");
        return;
    }
    if (new_content == NULL || new_content[0] == '\0') {
        printf("\033[1;31m错误: 新内容不能为空\033[0m\n");
        return;
    }

    int i = index - 1; // 转换为 0 基索引
    char old[MAX_CONTENT];
    strncpy(old, notes[i].content, MAX_CONTENT - 1);
    old[MAX_CONTENT - 1] = '\0';

    strncpy(notes[i].content, new_content, MAX_CONTENT - 1);
    notes[i].content[MAX_CONTENT - 1] = '\0';

    save_history();
    bump_sync();

    printf("\033[1;32m✓ 笔记已更新\033[0m\n");
    printf("\033[2m  旧: %s\033[0m\n", old);
    printf("\033[2m  新: %s\033[0m\n", notes[i].content);
}

// 导出笔记为 Markdown（/export [文件]），默认 ./noteshell_export.md
void export_notes(const char *filename) {
    const char *path = (filename != NULL && filename[0] != '\0')
                           ? filename
                           : "noteshell_export.md";
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        printf("\033[1;31m错误: 无法写入文件 %s\033[0m\n", path);
        return;
    }

    fprintf(f, "# NoteShell 笔记导出\n\n");
    fprintf(f, "> 共 %d 条 ｜ 导出时间: ", note_count);
    char ts[MAX_TIMESTAMP];
    get_timestamp(ts, sizeof(ts));
    fprintf(f, "%s\n\n", ts);
    fprintf(f, "---\n\n");

    for (int i = 0; i < note_count; i++) {
        fprintf(f, "## [%d] %s\n\n%s\n\n",
                i + 1, notes[i].timestamp, notes[i].content);
    }

    if (fclose(f) != 0) {
        printf("\033[1;31m错误: 写入收尾失败 %s\033[0m\n", path);
        return;
    }
    printf("\033[1;32m✓ 已导出 %d 条笔记到 %s\033[0m\n", note_count, path);
}

// 记住一条输入历史（供 ↑/↓ 浏览）
void push_line_history(const char *line) {
    if (strlen(line) == 0) return;
    // 与上一条相同则不重复记录
    if (line_history_count > 0 &&
        strcmp(line_history[line_history_count - 1], line) == 0) {
        return;
    }
    if (line_history_count >= MAX_LINE_HISTORY) {
        // 丢弃最旧的一条
        for (int i = 0; i < line_history_count - 1; i++) {
            strcpy(line_history[i], line_history[i + 1]);
        }
        line_history_count--;
    }
    strncpy(line_history[line_history_count], line, MAX_CONTENT - 1);
    line_history[line_history_count][MAX_CONTENT - 1] = '\0';
    line_history_count++;
}

// 清空当前行显示，重画成 new_line（内容必须 <= 已显示的字符数）
void redraw_current_line(const char *old_line, const char *new_line) {
    int old_len = (int)strlen(old_line);
    int new_len = (int)strlen(new_line);
    // 先把光标退到行首（用退格擦掉旧内容）
    for (int i = 0; i < old_len; i++) {
        printf("\b \b");
    }
    // 再打印新内容
    printf("%s", new_line);
    (void)new_len;
    fflush(stdout);
}

// 读取一行输入（支持 ← 删除、↑/↓ 浏览历史）
// 返回 0 表示读取到一行；-1 表示 EOF/读取失败
int read_line(char *buffer, size_t size) {
    size_t pos = 0;
    char ch;
    int hist_index = line_history_count; // 指向“空行”下一个位置
    char search[MAX_CONTENT];
    search[0] = '\0';
    (void)search;

    buffer[0] = '\0';

    while (pos < size - 1) {
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n <= 0) return -1;

        if (ch == '\n' || ch == '\r') {
            break;
        } else if (ch == 127 || ch == 8) { // Backspace
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (ch == '\033') { // Escape sequence
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) break;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) break;

            if (seq[0] == '[') {
                if (seq[1] == 'A') { // ↑ 上箭头：回看更早的历史
                    if (hist_index > 0) {
                        char old[MAX_CONTENT];
                        strcpy(old, buffer);
                        hist_index--;
                        strncpy(buffer, line_history[hist_index], size - 1);
                        buffer[size - 1] = '\0';
                        redraw_current_line(old, buffer);
                        pos = strlen(buffer);
                    }
                } else if (seq[1] == 'B') { // ↓ 下箭头：回到更新的一条
                    if (hist_index < line_history_count) {
                        char old[MAX_CONTENT];
                        strcpy(old, buffer);
                        hist_index++;
                        if (hist_index == line_history_count) {
                            buffer[0] = '\0'; // 回到空行
                        } else {
                            strncpy(buffer, line_history[hist_index], size - 1);
                            buffer[size - 1] = '\0';
                        }
                        redraw_current_line(old, buffer);
                        pos = strlen(buffer);
                    }
                } else if (seq[1] == 'C') { // → 右箭头：光标右移（简单忽略）
                } else if (seq[1] == 'D') { // ← 左箭头：光标左移（简单忽略）
                }
            }
        } else {
            buffer[pos++] = ch;
            buffer[pos] = '\0';
            printf("%c", ch);
            fflush(stdout);
        }
    }
    
    buffer[pos] = '\0';
    push_line_history(buffer);
    printf("\n");
    return 0;
}

// 主循环
void main_loop() {
    char input[MAX_CONTENT];
    
    while (1) {
        // 显示提示符
        printf("\n\033[1;36mnoteshell>\033[0m ");
        fflush(stdout);
        
        // 读取输入
        if (read_line(input, sizeof(input)) != 0) {
            break; // EOF：直接退出
        }
        
        // 处理空输入（但空行也已被 push_line_history 忽略）
        if (strlen(input) == 0) {
            continue;
        }
        
        // 处理命令
        if (input[0] == '/') {
            if (strcmp(input, "/exit") == 0 || strcmp(input, "/quit") == 0) {
                printf("\033[1;32m再见!\033[0m\n");
                break;
            } else if (strcmp(input, "/help") == 0) {
                show_help();
            } else if (strcmp(input, "/clear") == 0) {
                clear_screen();
            } else if (strcmp(input, "/list") == 0) {
                show_notes();
            } else if (strcmp(input, "/history") == 0) {
                show_history(NULL);
            } else if (strncmp(input, "/history ", 9) == 0) {
                show_history(input + 9);
            } else if (strncmp(input, "/search ", 8) == 0) {
                search_notes(input + 8);
            } else if (strncmp(input, "/edit ", 6) == 0) {
                // 格式: /edit <编号> <新内容>
                char *rest = input + 6;
                char *sp = strchr(rest, ' ');
                if (sp == NULL) {
                    printf("\033[1;31m用法: /edit <编号> <新内容>\033[0m\n");
                } else {
                    *sp = '\0';
                    int index = atoi(rest);
                    char *new_content = sp + 1;
                    // 跳过多余空格
                    while (*new_content == ' ') new_content++;
                    edit_note(index, new_content);
                }
            } else if (strncmp(input, "/delete ", 8) == 0) {
                int index = atoi(input + 8);
                delete_note(index);
            } else if (strcmp(input, "/export") == 0) {
                export_notes(NULL);
            } else if (strncmp(input, "/export ", 8) == 0) {
                export_notes(input + 8);
            } else if (strcmp(input, "/count") == 0) {
                printf("\033[1;36m共 %d 条笔记\033[0m\n", note_count);
            } else {
                printf("\033[1;31m未知命令: %s\033[0m\n", input);
                printf("输入 \033[1;32m/help\033[0m 查看帮助\n");
            }
        } else {
            // 添加笔记
            add_note(input);
            printf("\033[1;32m✓ 笔记已保存\033[0m\n");
        }
    }
}

int main() {
    // 设置终端为 raw 模式
    enable_raw_mode();
    
    // 加载历史记录
    load_history();
    
    // 清屏并显示欢迎信息
    clear_screen();
    printf("\033[1;36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║                                                          ║\n");
    printf("║   \033[1;33m███╗   ██╗ ██████╗ ████████╗███████╗██╗  ██╗\033[1;36m   ║\n");
    printf("║   \033[1;33m████╗  ██║██╔═══██╗╚══██╔══╝██╔════╝██║  ██║\033[1;36m   ║\n");
    printf("║   \033[1;33m██╔██╗ ██║██║   ██║   ██║   ███████╗███████║\033[1;36m   ║\n");
    printf("║   \033[1;33m██║╚██╗██║██║   ██║   ██║   ╚════██║██╔══██║\033[1;36m   ║\n");
    printf("║   \033[1;33m██║ ╚████║╚██████╔╝   ██║   ███████║██║  ██║\033[1;36m   ║\n");
    printf("║   \033[1;33m╚═╝  ╚═══╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝\033[1;36m   ║\n");
    printf("║                                                          ║\n");
    printf("║              \033[1;37m终端笔记本 - 记录你的想法\033[1;36m              ║\n");
    printf("║                                                          ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
    
    printf("已加载 \033[1;32m%d\033[0m 条历史笔记\n", note_count);
    printf("输入 \033[1;32m/help\033[0m 查看帮助，输入 \033[1;32m/exit\033[0m 退出\n\n");
    
    // 进入主循环
    main_loop();
    
    // 恢复终端设置
    disable_raw_mode();
    
    return 0;
}