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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>

#define MAX_NOTES 1000
#define MAX_CONTENT 4096
#define MAX_TIMESTAMP 64
#define MAX_HOST 256
#define MAX_PORT 16
#define MAX_NOTEBOOK_NAME 128
#define CONFIG_FILE ".noteshell_config"
#define MAX_RESPONSE 65536
#define MAX_NOTEBOOKS 50
#define MAX_LINE_HISTORY 200
#define MAX_TAGS 10
#define MAX_TAG_LEN 32

typedef struct {
    int id;
    char timestamp[MAX_TIMESTAMP];
    char content[MAX_CONTENT];
    char priority[8];
    char tags[MAX_TAGS][MAX_TAG_LEN];
    int tag_count;
    int pinned;
    char author[8];
    char updated[MAX_TIMESTAMP];
    char updated_by[8];
    char status[16];
    char due[MAX_TIMESTAMP];
} Note;

typedef struct {
    char name[MAX_NOTEBOOK_NAME];
    char title[MAX_NOTEBOOK_NAME];
    char desc[MAX_CONTENT];
    int vim_mode;
    int count;
} NotebookInfo;

Note notes[MAX_NOTES];
int note_count = 0;
int current_page = 0;
int notes_per_page = 10;

NotebookInfo notebook_list[MAX_NOTEBOOKS];
int notebook_count = 0;
char current_notebook[MAX_NOTEBOOK_NAME] = "default";

char server_host[MAX_HOST];
char server_port[MAX_PORT];

struct termios orig_termios;

#define MAX_LINE_HISTORY 200
char line_history[MAX_LINE_HISTORY][MAX_CONTENT];
int line_history_count = 0;

// ---------- 配置 ----------

void get_config_path(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(path, size, "%s/%s", home, CONFIG_FILE);
}

int load_config() {
    char path[512];
    get_config_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    server_host[0] = '\0';
    strncpy(server_port, "8001", MAX_PORT - 1);

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, "server_host=", 12) == 0) {
            strncpy(server_host, line + 12, MAX_HOST - 1);
            server_host[MAX_HOST - 1] = '\0';
        } else if (strncmp(line, "server_port=", 12) == 0) {
            strncpy(server_port, line + 12, MAX_PORT - 1);
            server_port[MAX_PORT - 1] = '\0';
        }
    }
    fclose(f);

    if (server_host[0] == '\0') return -1;
    return 0;
}

int save_config(const char *host, const char *port) {
    char path[512];
    char tmp[600];
    get_config_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "server_host=%s\nserver_port=%s\n", host, port);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

int prompt_server_config() {
    printf("\033[1;36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║              NoteShell - 首次连接配置                    ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");

    char buf[512];

    printf("请输入服务器 IP 地址 [127.0.0.1]: ");
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    buf[strcspn(buf, "\n")] = '\0';
    if (buf[0] == '\0') strncpy(buf, "127.0.0.1", sizeof(buf));

    char host[MAX_HOST];
    strncpy(host, buf, MAX_HOST - 1);
    host[MAX_HOST - 1] = '\0';

    printf("请输入服务器端口 [8001]: ");
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    buf[strcspn(buf, "\n")] = '\0';
    if (buf[0] == '\0') strncpy(buf, "8001", sizeof(buf));

    char port[MAX_PORT];
    strncpy(port, buf, MAX_PORT - 1);
    port[MAX_PORT - 1] = '\0';

    if (save_config(host, port) != 0) return -1;

    strncpy(server_host, host, MAX_HOST - 1);
    server_host[MAX_HOST - 1] = '\0';
    strncpy(server_port, port, MAX_PORT - 1);
    server_port[MAX_PORT - 1] = '\0';

    printf("\n\033[1;32m✓ 配置已保存到 ~/.noteshell_config\033[0m\n\n");
    return 0;
}

// ---------- 终端 ----------

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void clear_screen() {
    printf("\033[2J\033[H");
}

void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

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

void wait_any_key() {
    char ch;
    if (read(STDIN_FILENO, &ch, 1) <= 0) return;
    if (ch != '\033') return;
    for (int i = 0; i < 2; i++) {
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 30000;
        int r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (r <= 0) return;
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) return;
        if (i == 0) {
            if (c != '[' && c != 'O') return;
        } else {
            return;
        }
    }
}

// ---------- HTTP 客户端 ----------

static int http_connect(const char *host, const char *port) {
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

int http_request(const char *method, const char *path,
                 const char *body,
                 char *response, size_t response_size) {
    int fd = http_connect(server_host, server_port);
    if (fd < 0) return -1;

    size_t body_len = body ? strlen(body) : 0;

    char header[2048];
    int hlen = snprintf(header, sizeof(header),
        "%s %s HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        method, path, server_host, server_port, body_len);
    if (hlen <= 0 || (size_t)hlen >= sizeof(header)) {
        close(fd);
        return -1;
    }

    if (send_all(fd, header, (size_t)hlen) != 0) {
        close(fd);
        return -1;
    }
    if (body_len > 0) {
        if (send_all(fd, body, body_len) != 0) {
            close(fd);
            return -1;
        }
    }

    // 读取响应
    char raw[MAX_RESPONSE];
    size_t total = 0;
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (total < sizeof(raw) - 1) {
        ssize_t n = recv(fd, raw + total, sizeof(raw) - 1 - total, 0);
        if (n <= 0) break;
        total += (size_t)n;
    }
    raw[total] = '\0';
    close(fd);

    if (total == 0) return -1;

    // 解析状态码
    int status_code = 0;
    if (total > 9 && strncmp(raw, "HTTP/1.", 7) == 0) {
        status_code = atoi(raw + 9);
    }

    // 找到 body 起始位置（跳过 headers）
    char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
    } else {
        body_start = raw;
    }

    if (response && response_size > 0) {
        strncpy(response, body_start, response_size - 1);
        response[response_size - 1] = '\0';
    }

    return status_code;
}

// ---------- JSON 解析（最小化） ----------

const char* json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p+1)) {
            p++;
            switch (*p) {
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case '"': out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                default: out[i++] = *p; break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return p;
}

int json_get_int(const char *json, const char *key) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    return atoi(p);
}

int json_get_bool(const char *json, const char *key) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (strncmp(p, "true", 4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;
    return -1;
}

void json_escape(const char *src, char *dst, size_t dst_size) {
    size_t di = 0;
    for (size_t si = 0; src[si] && di < dst_size - 2; si++) {
        char c = src[si];
        switch (c) {
            case '"':  if (di + 2 < dst_size) { dst[di++] = '\\'; dst[di++] = '"'; } break;
            case '\\': if (di + 2 < dst_size) { dst[di++] = '\\'; dst[di++] = '\\'; } break;
            case '\n': if (di + 2 < dst_size) { dst[di++] = '\\'; dst[di++] = 'n'; } break;
            case '\t': if (di + 2 < dst_size) { dst[di++] = '\\'; dst[di++] = 't'; } break;
            case '\r': if (di + 2 < dst_size) { dst[di++] = '\\'; dst[di++] = 'r'; } break;
            default:   dst[di++] = c; break;
        }
    }
    dst[di] = '\0';
}

// 解析 JSON 数组中的字符串
int json_get_string_array(const char *json, const char *key, char arr[][MAX_TAG_LEN], int max_count) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;
    
    int count = 0;
    while (*p && *p != ']' && count < max_count) {
        while (*p && *p != '"' && *p != ']') p++;
        if (*p == ']') break;
        if (*p != '"') break;
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < MAX_TAG_LEN - 1) {
            arr[count][i++] = *p++;
        }
        arr[count][i] = '\0';
        count++;
        if (*p == '"') p++;
    }
    return count;
}

// 解析笔记数组（从 /api/notebook/<name> 响应）
int parse_notes_response(const char *json) {
    note_count = 0;
    const char *notes_start = strstr(json, "\"notes\"");
    if (!notes_start) return -1;
    const char *arr_start = strchr(notes_start, '[');
    if (!arr_start) return -1;

    const char *p = arr_start + 1;
    while (*p && *p != ']' && note_count < MAX_NOTES) {
        while (*p && *p != '{' && *p != ']') p++;
        if (*p == ']') break;
        if (*p != '{') break;

        // 找到这个对象的结尾
        int depth = 1;
        const char *obj_start = p;
        p++;
        while (*p && depth > 0) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            p++;
        }

        // 提取这个对象的内容
        size_t obj_len = (size_t)(p - obj_start);
        char obj[MAX_CONTENT + 1024];
        if (obj_len >= sizeof(obj)) obj_len = sizeof(obj) - 1;
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';

        Note *note = &notes[note_count];
        note->id = json_get_int(obj, "id");
        json_get_string(obj, "ts", note->timestamp, MAX_TIMESTAMP);
        json_get_string(obj, "content", note->content, MAX_CONTENT);
        json_get_string(obj, "priority", note->priority, sizeof(note->priority));
        if (note->priority[0] == '\0') strcpy(note->priority, "P2");
        note->tag_count = json_get_string_array(obj, "tags", note->tags, MAX_TAGS);
        note->pinned = json_get_bool(obj, "pinned");
        if (note->pinned < 0) note->pinned = 0;
        json_get_string(obj, "author", note->author, sizeof(note->author));
        if (note->author[0] == '\0') strcpy(note->author, "A");
        json_get_string(obj, "updated", note->updated, MAX_TIMESTAMP);
        if (note->updated[0] == '\0') strcpy(note->updated, note->timestamp);
        json_get_string(obj, "updated_by", note->updated_by, sizeof(note->updated_by));
        if (note->updated_by[0] == '\0') strcpy(note->updated_by, note->author);
        json_get_string(obj, "status", note->status, sizeof(note->status));
        if (note->status[0] == '\0') strcpy(note->status, "todo");
        json_get_string(obj, "due", note->due, MAX_TIMESTAMP);
        
        note_count++;
    }
    return 0;
}

// ---------- API 封装 ----------

int api_verify_connection() {
    char resp[MAX_RESPONSE];
    int status = http_request("GET", "/api/notebooks", NULL, resp, sizeof(resp));
    return (status == 200) ? 0 : -1;
}

int api_fetch_notebooks() {
    char resp[MAX_RESPONSE];
    int status = http_request("GET", "/api/notebooks", NULL, resp, sizeof(resp));
    if (status != 200) return -1;

    notebook_count = 0;
    const char *p = resp;
    while ((p = strstr(p, "\"name\"")) != NULL && notebook_count < MAX_NOTEBOOKS) {
        NotebookInfo *nb = &notebook_list[notebook_count];
        json_get_string(p - 1, "name", nb->name, MAX_NOTEBOOK_NAME);

        // 找同一对象中的其他字段
        int depth = 0;
        const char *obj_start = p;
        while (obj_start > resp && *obj_start != '{') obj_start--;
        const char *q = obj_start;
        depth = 1; q++;
        while (*q && depth > 0) {
            if (*q == '{') depth++;
            else if (*q == '}') depth--;
            q++;
        }
        char obj[2048];
        size_t len = (size_t)(q - obj_start);
        if (len >= sizeof(obj)) len = sizeof(obj) - 1;
        memcpy(obj, obj_start, len);
        obj[len] = '\0';

        json_get_string(obj, "title", nb->title, MAX_NOTEBOOK_NAME);
        if (nb->title[0] == '\0') strcpy(nb->title, nb->name);
        json_get_string(obj, "desc", nb->desc, MAX_CONTENT);
        nb->vim_mode = json_get_bool(obj, "vim_mode");
        if (nb->vim_mode < 0) nb->vim_mode = 0;
        nb->count = json_get_int(obj, "count");
        if (nb->count < 0) nb->count = 0;
        notebook_count++;

        p = q;
    }
    return 0;
}

int api_fetch_notes(const char *notebook) {
    char path[512];
    snprintf(path, sizeof(path), "/api/notebook/%s", notebook);
    char resp[MAX_RESPONSE];
    int status = http_request("GET", path, NULL, resp, sizeof(resp));
    if (status != 200) return -1;
    return parse_notes_response(resp);
}

int api_add_note(const char *notebook, const char *content) {
    char path[512];
    snprintf(path, sizeof(path), "/api/notebook/%s/note", notebook);
    char escaped[MAX_CONTENT * 2];
    json_escape(content, escaped, sizeof(escaped));
    char body[MAX_CONTENT * 2 + 256];
    snprintf(body, sizeof(body), 
        "{\"content\":\"%s\",\"priority\":\"P2\",\"tags\":[],\"pinned\":false,\"author\":\"A\"}", 
        escaped);
    char resp[MAX_RESPONSE];
    int status = http_request("POST", path, body, resp, sizeof(resp));
    return (status == 200) ? 0 : -1;
}

int api_add_note_extended(const char *notebook, const char *content, 
                          const char *priority, const char *tags_json, 
                          int pinned, const char *author) {
    char path[512];
    snprintf(path, sizeof(path), "/api/notebook/%s/note", notebook);
    char escaped[MAX_CONTENT * 2];
    json_escape(content, escaped, sizeof(escaped));
    char body[MAX_CONTENT * 2 + 512];
    snprintf(body, sizeof(body), 
        "{\"content\":\"%s\",\"priority\":\"%s\",\"tags\":%s,\"pinned\":%s,\"author\":\"%s\"}", 
        escaped, priority, tags_json, pinned ? "true" : "false", author);
    char resp[MAX_RESPONSE];
    int status = http_request("POST", path, body, resp, sizeof(resp));
    return (status == 200) ? 0 : -1;
}

int api_delete_note(const char *notebook, int note_id) {
    char path[512];
    snprintf(path, sizeof(path), "/api/notebook/%s/note/%d", notebook, note_id);
    char resp[MAX_RESPONSE];
    int status = http_request("DELETE", path, NULL, resp, sizeof(resp));
    return (status == 200) ? 0 : -1;
}

int api_edit_note(const char *notebook, int note_id, const char *content) {
    char path[512];
    snprintf(path, sizeof(path), "/api/notebook/%s/note/%d", notebook, note_id);
    char escaped[MAX_CONTENT * 2];
    json_escape(content, escaped, sizeof(escaped));
    char body[MAX_CONTENT * 2 + 64];
    snprintf(body, sizeof(body), "{\"content\":\"%s\"}", escaped);
    char resp[MAX_RESPONSE];
    int status = http_request("PUT", path, body, resp, sizeof(resp));
    return (status == 200) ? 0 : -1;
}

int api_create_notebook(const char *name) {
    char body[256];
    snprintf(body, sizeof(body), "{\"name\":\"%s\"}", name);
    char resp[MAX_RESPONSE];
    int status = http_request("POST", "/api/notebooks", body, resp, sizeof(resp));
    return (status == 200) ? 0 : -1;
}

int api_get_settings(const char *notebook) {
    char path[512];
    snprintf(path, sizeof(path), "/api/notebook/%s/settings", notebook);
    char resp[MAX_RESPONSE];
    int status = http_request("GET", path, NULL, resp, sizeof(resp));
    if (status != 200) return -1;
    return json_get_bool(resp, "vim_mode");
}

int api_update_settings(const char *notebook, int vim_mode) {
    char path[512];
    snprintf(path, sizeof(path), "/api/notebook/%s/settings", notebook);
    char body[64];
    snprintf(body, sizeof(body), "{\"vim_mode\":%s}", vim_mode ? "true" : "false");
    char resp[MAX_RESPONSE];
    int status = http_request("PUT", path, body, resp, sizeof(resp));
    return (status == 200) ? 0 : -1;
}

int api_update_notebook(const char *notebook, const char *title, const char *desc) {
    char path[512];
    snprintf(path, sizeof(path), "/api/notebook/%s", notebook);
    char escaped_title[MAX_NOTEBOOK_NAME * 2];
    char escaped_desc[MAX_CONTENT * 2];
    json_escape(title ? title : "", escaped_title, sizeof(escaped_title));
    json_escape(desc ? desc : "", escaped_desc, sizeof(escaped_desc));
    char body[MAX_NOTEBOOK_NAME * 2 + MAX_CONTENT * 2 + 128];
    snprintf(body, sizeof(body), "{\"title\":\"%s\",\"desc\":\"%s\"}", 
             escaped_title, escaped_desc);
    char resp[MAX_RESPONSE];
    int status = http_request("PUT", path, body, resp, sizeof(resp));
    return (status == 200) ? 0 : -1;
}

int api_update_note(const char *notebook, int note_id, const char *priority, 
                    const char *tags_json, int pinned, const char *author, 
                    const char *status, const char *due) {
    char path[512];
    snprintf(path, sizeof(path), "/api/notebook/%s/note/%d", notebook, note_id);
    char body[1024];
    snprintf(body, sizeof(body), 
        "{\"priority\":\"%s\",\"tags\":%s,\"pinned\":%s,\"author\":\"%s\",\"status\":\"%s\",\"due\":\"%s\"}", 
        priority, tags_json, pinned ? "true" : "false", author, status, due ? due : "");
    char resp[MAX_RESPONSE];
    int status_code = http_request("PUT", path, body, resp, sizeof(resp));
    return (status_code == 200) ? 0 : -1;
}

int api_search_notes(const char *notebook, const char *keyword) {
    char path[512];
    snprintf(path, sizeof(path), "/api/notebook/%s/search?q=%s", notebook, keyword);
    char resp[MAX_RESPONSE];
    int status = http_request("GET", path, NULL, resp, sizeof(resp));
    if (status != 200) return -1;
    return parse_notes_response(resp);
}

// ---------- 时间戳 ----------

void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// ---------- 输入处理 ----------

// 处理特殊符号包裹的内容
// 如果输入以 " 或 ` 开头，找到对应的结束符号，提取中间内容
// 返回 1 表示已处理，0 表示普通文本
int process_special_symbols(const char *input, char *output, size_t out_size) {
    if (!input || !input[0]) return 0;

    char start_char = input[0];
    if (start_char != '"' && start_char != '`') return 0;

    // 查找结束符号
    const char *end = strrchr(input + 1, start_char);
    if (!end || end == input) return 0;

    // 提取中间内容
    size_t content_len = (size_t)(end - input - 1);
    if (content_len >= out_size) content_len = out_size - 1;
    memcpy(output, input + 1, content_len);
    output[content_len] = '\0';

    return 1;
}

// ---------- 笔记操作 ----------

void add_note(const char *content) {
    if (api_add_note(current_notebook, content) == 0) {
        api_fetch_notes(current_notebook);
        printf("\033[1;32m✓ 笔记已保存\033[0m\n");
    } else {
        printf("\033[1;31m✗ 保存失败，请检查服务器连接\033[0m\n");
    }
}

void delete_note(int index) {
    if (index < 1 || index > note_count) {
        printf("\033[1;31m错误: 无效的笔记编号\033[0m\n");
        return;
    }
    int note_id = notes[index - 1].id;
    if (api_delete_note(current_notebook, note_id) == 0) {
        api_fetch_notes(current_notebook);
        printf("\033[1;32m✓ 笔记已删除\033[0m\n");
    } else {
        printf("\033[1;31m✗ 删除失败\033[0m\n");
    }
}

void edit_note(int index, const char *new_content) {
    if (index < 1 || index > note_count) {
        printf("\033[1;31m错误: 无效的笔记编号\033[0m\n");
        return;
    }
    if (!new_content || !new_content[0]) {
        printf("\033[1;31m错误: 新内容不能为空\033[0m\n");
        return;
    }
    int note_id = notes[index - 1].id;
    char old_content[MAX_CONTENT];
    strncpy(old_content, notes[index - 1].content, MAX_CONTENT - 1);
    old_content[MAX_CONTENT - 1] = '\0';

    if (api_edit_note(current_notebook, note_id, new_content) == 0) {
        api_fetch_notes(current_notebook);
        printf("\033[1;32m✓ 笔记已更新\033[0m\n");
        printf("\033[2m  旧: %s\033[0m\n", old_content);
        // 找到更新后的笔记
        for (int i = 0; i < note_count; i++) {
            if (notes[i].id == note_id) {
                printf("\033[2m  新: %s\033[0m\n", notes[i].content);
                break;
            }
        }
    } else {
        printf("\033[1;31m✗ 更新失败\033[0m\n");
    }
}

// ---------- 显示 ----------

void show_help() {
    clear_screen();
    printf("\033[1;36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    NoteShell 帮助                        ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");

    printf("\033[1;33m笔记命令:\033[0m\n\n");
    printf("  \033[1;32m/help\033[0m              - 显示此帮助信息\n");
    printf("  \033[1;32m/clear\033[0m             - 清屏\n");
    printf("  \033[1;32m/list\033[0m              - 列出当前笔记本的所有笔记（分页）\n");
    printf("  \033[1;32m/history\033[0m           - 按时间翻查历史（分页，可跟关键词）\n");
    printf("  \033[1;32m/search <关键词>\033[0m    - 搜索笔记\n");
    printf("  \033[1;32m/edit <编号> <新内容>\033[0m - 修改指定笔记\n");
    printf("  \033[1;32m/delete <编号>\033[0m      - 删除指定笔记\n");
    printf("  \033[1;32m/export [文件]\033[0m      - 导出笔记为 Markdown\n");
    printf("  \033[1;32m/count\033[0m             - 显示当前笔记总数\n");
    printf("  \033[1;32m/priority <编号> <P0|P1|P2>\033[0m - 设置笔记优先级\n");
    printf("  \033[1;32m/tags <编号> <tag1,tag2,...>\033[0m - 设置笔记标签\n");
    printf("  \033[1;32m/pin <编号>\033[0m        - 切换笔记置顶状态\n");
    printf("  \033[1;32m/author <编号> <A|M>\033[0m - 设置笔记作者（A=noteshell, M=其他）\n");
    printf("  \033[1;32m/due <编号> <YYYY-MM-DD>\033[0m - 设置笔记截止日期\n\n");

    printf("\033[1;33m笔记本命令:\033[0m\n\n");
    printf("  \033[1;32m/notebooks\033[0m         - 列出所有笔记本\n");
    printf("  \033[1;32m/notebook <名称>\033[0m    - 切换到指定笔记本\n");
    printf("  \033[1;32m/nbadd <名称>\033[0m      - 创建新笔记本\n");
    printf("  \033[1;32m/nbinfo <标题> [|描述]\033[0m - 设置笔记本标题和描述\n");
    printf("  \033[1;32m/nbvim\033[0m             - 查看当前笔记本的 vim 模式\n");
    printf("  \033[1;32m/nbvim on/off\033[0m      - 设置当前笔记本的 vim 模式\n\n");

    printf("\033[1;33m其他:\033[0m\n\n");
    printf("  \033[1;32m/exit\033[0m              - 退出程序\n\n");

    printf("\033[1;33m快捷键:\033[0m\n\n");
    printf("  \033[1;32mEnter\033[0m            - 确认提交\n");
    printf("  \033[1;32mShift+Enter\033[0m      - 换行（多行输入）\n");
    printf("  \033[1;32mCtrl+J\033[0m           - 换行（备用）\n");
    printf("  \033[1;32m↑/↓\033[0m              - 浏览输入历史\n");
    printf("  \033[1;32m←/→\033[0m              - 移动光标\n");
    printf("  \033[1;32mq\033[0m                - 从浏览视图返回\n");
    printf("  \033[1;32mCtrl+C\033[0m           - 强制退出\n\n");

    printf("\033[1;33m特殊符号:\033[0m\n\n");
    printf("  \033[1;32m\"text\"\033[0m           - 引号包裹的文本（支持多行）\n");
    printf("  \033[1;32m`code`\033[0m           - 反引号包裹的代码块（支持多行）\n\n");

    printf("\033[1;33m使用说明:\033[0m\n\n");
    printf("  直接输入文本按 Enter 即可创建新笔记。\n");
    printf("  需要多行输入时，按 Ctrl+J 换行（不是 Enter）。\n");
    printf("  也可以用 \"\" 或 `` 包裹多行内容。\n");
    printf("  笔记保存在服务器 %s:%s 上。\n\n", server_host, server_port);

    printf("\033[2m按任意键返回...\033[0m");
    fflush(stdout);
    wait_any_key();
}

void show_notes() {
    clear_screen();
    printf("\033[1;36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║           NoteShell 笔记列表 [%s]\n", current_notebook);
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
        Note *note = &notes[i];
        
        // 优先级颜色
        const char *prio_color = "\033[2m";
        if (strcmp(note->priority, "P0") == 0) prio_color = "\033[1;31m";
        else if (strcmp(note->priority, "P1") == 0) prio_color = "\033[1;33m";
        else if (strcmp(note->priority, "P2") == 0) prio_color = "\033[1;32m";
        
        // 显示编号、时间、优先级、置顶标记
        printf("\033[1;32m[%3d]\033[0m \033[2m%s\033[0m %s[%s]\033[0m", 
               i + 1, note->timestamp, prio_color, note->priority);
        if (note->pinned) {
            printf(" \033[1;35m📌\033[0m");
        }
        printf("\n");
        
        // 显示标签
        if (note->tag_count > 0) {
            printf("      \033[1;36m");
            for (int t = 0; t < note->tag_count; t++) {
                printf("#%s ", note->tags[t]);
            }
            printf("\033[0m\n");
        }
        
        // 多行内容缩进显示
        char *line_start = note->content;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != NULL) {
            *newline = '\0';
            printf("      %s\n", line_start);
            *newline = '\n';
            line_start = newline + 1;
        }
        if (*line_start)
            printf("      %s\n", line_start);
        
        // 显示作者和状态
        printf("      \033[2m作者: %s | 状态: %s", note->author, note->status);
        if (note->due[0]) {
            printf(" | 截止: %s", note->due);
        }
        if (strcmp(note->updated, note->timestamp) != 0) {
            printf(" | 修改: %s", note->updated);
        }
        printf("\033[0m\n\n");
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
                if (seq[1] == 'A') {
                    if (current_page > 0) { current_page--; show_notes(); return; }
                } else if (seq[1] == 'B') {
                    if (current_page < total_pages - 1) { current_page++; show_notes(); return; }
                }
            }
        }
    }
}

void show_history(const char *keyword) {
    static int idx[MAX_NOTES];
    int total = 0;
    for (int i = 0; i < note_count; i++) {
        if (keyword && keyword[0] && strstr(notes[i].content, keyword) == NULL)
            continue;
        idx[total++] = i;
    }

    clear_screen();
    printf("\033[1;36m");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║              NoteShell 历史翻查 [%s]\n", current_notebook);
    printf("╚═══════════════════════════════════════════════════════════╝\n");
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

    int rows, cols;
    get_terminal_size(&rows, &cols);
    int per_page = rows - 8;
    if (per_page < 3) per_page = 3;
    int total_pages = (total + per_page - 1) / per_page;
    int page = 0;

    while (1) {
        if (page > 0) clear_screen();
        printf("\033[1;36m");
        printf("╔═══════════════════════════════════════════════════════════╗\n");
        printf("║              NoteShell 历史翻查 [%s]\n", current_notebook);
        printf("╚═══════════════════════════════════════════════════════════╝\n");
        printf("\033[0m\n");

        if (keyword && keyword[0])
            printf("\033[1;35m过滤: \"%s\"\033[0m\n\n", keyword);

        int start = page * per_page;
        int end = start + per_page;
        if (end > total) end = total;

        for (int k = start; k < end; k++) {
            int i = idx[k];
            printf("\033[1;32m[%3d]\033[0m \033[2m%s\033[0m  \033[1;35m#%d/%d\033[0m\n",
                   i + 1, notes[i].timestamp, i + 1, note_count);
            printf("      %s\n\n", notes[i].content);
        }

        printf("\033[1;36m第 %d/%d 页 (共 %d 条", page + 1, total_pages, total);
        if (keyword && keyword[0])
            printf("，匹配 \"%s\"", keyword);
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

void search_notes(const char *keyword) {
    if (api_search_notes(current_notebook, keyword) != 0) {
        printf("\033[1;31m✗ 搜索失败，请检查服务器连接\033[0m\n");
        return;
    }

    printf("\033[1;36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║              NoteShell 搜索结果 [%s]\n", current_notebook);
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");

    if (note_count == 0) {
        printf("\033[1;33m未找到包含 \"%s\" 的笔记\033[0m\n", keyword);
    } else {
        for (int i = 0; i < note_count; i++) {
            printf("\033[1;32m[%3d]\033[0m \033[2m%s\033[0m\n", i + 1, notes[i].timestamp);
            printf("      %s\n\n", notes[i].content);
        }
        printf("\033[1;36m找到 %d 条结果\033[0m\n", note_count);
    }

    printf("\n\033[2m按任意键返回...\033[0m");
    fflush(stdout);
    wait_any_key();

    // 恢复当前笔记本的笔记列表
    api_fetch_notes(current_notebook);
}

void export_notes(const char *filename) {
    const char *path = (filename && filename[0]) ? filename : "noteshell_export.md";
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("\033[1;31m错误: 无法写入文件 %s\033[0m\n", path);
        return;
    }

    fprintf(f, "# NoteShell 笔记导出 - %s\n\n", current_notebook);
    fprintf(f, "> 共 %d 条 ｜ 导出时间: ", note_count);
    char ts[MAX_TIMESTAMP];
    get_timestamp(ts, sizeof(ts));
    fprintf(f, "%s\n\n", ts);
    fprintf(f, "---\n\n");

    for (int i = 0; i < note_count; i++) {
        fprintf(f, "## [%d] %s\n\n%s\n\n", i + 1, notes[i].timestamp, notes[i].content);
    }

    fclose(f);
    printf("\033[1;32m✓ 已导出 %d 条笔记到 %s\033[0m\n", note_count, path);
}

// ---------- 笔记本管理 ----------

void show_notebooks() {
    if (api_fetch_notebooks() != 0) {
        printf("\033[1;31m✗ 获取笔记本列表失败\033[0m\n");
        return;
    }

    clear_screen();
    printf("\033[1;36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    NoteShell 笔记本                      ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");

    if (notebook_count == 0) {
        printf("\033[1;33m暂无笔记本\033[0m\n");
    } else {
        for (int i = 0; i < notebook_count; i++) {
            NotebookInfo *nb = &notebook_list[i];
            int is_current = (strcmp(nb->name, current_notebook) == 0);
            
            if (is_current) {
                printf("  \033[1;32m* %-20s\033[0m  (笔记: %d, vim: %s)\n",
                       nb->name, nb->count, nb->vim_mode ? "on" : "off");
            } else {
                printf("    %-20s  (笔记: %d, vim: %s)\n",
                       nb->name, nb->count, nb->vim_mode ? "on" : "off");
            }
            
            // 显示标题和描述
            if (nb->title[0] && strcmp(nb->title, nb->name) != 0) {
                printf("      \033[1;37m%s\033[0m\n", nb->title);
            }
            if (nb->desc[0]) {
                printf("      \033[2m%s\033[0m\n", nb->desc);
            }
            printf("\n");
        }
    }

    printf("\n\033[2m按任意键返回...\033[0m");
    fflush(stdout);
    wait_any_key();
}

void switch_notebook(const char *name) {
    if (api_fetch_notes(name) == 0) {
        strncpy(current_notebook, name, MAX_NOTEBOOK_NAME - 1);
        current_notebook[MAX_NOTEBOOK_NAME - 1] = '\0';
        current_page = 0;
        printf("\033[1;32m✓ 已切换到笔记本: %s\033[0m (%d 条笔记)\n", name, note_count);
    } else {
        printf("\033[1;31m✗ 笔记本 '%s' 不存在或无法访问\033[0m\n", name);
    }
}

void create_notebook(const char *name) {
    if (!name || !name[0]) {
        printf("\033[1;31m用法: /nbadd <名称>\033[0m\n");
        return;
    }
    if (api_create_notebook(name) == 0) {
        printf("\033[1;32m✓ 笔记本 '%s' 已创建\033[0m\n", name);
    } else {
        printf("\033[1;31m✗ 创建失败（名称可能已存在或包含非法字符）\033[0m\n");
    }
}

void show_vim_mode() {
    int mode = api_get_settings(current_notebook);
    if (mode < 0) {
        printf("\033[1;31m✗ 获取设置失败\033[0m\n");
    } else {
        printf("\033[1;36m笔记本 '%s' 的 vim 模式: \033[0m\033[1;33m%s\033[0m\n",
               current_notebook, mode ? "开启" : "关闭");
    }
}

void set_vim_mode(const char *value) {
    if (!value || !value[0]) {
        show_vim_mode();
        return;
    }
    int mode;
    if (strcmp(value, "on") == 0 || strcmp(value, "1") == 0 || strcmp(value, "true") == 0) {
        mode = 1;
    } else if (strcmp(value, "off") == 0 || strcmp(value, "0") == 0 || strcmp(value, "false") == 0) {
        mode = 0;
    } else {
        printf("\033[1;31m用法: /nbvim on|off\033[0m\n");
        return;
    }
    if (api_update_settings(current_notebook, mode) == 0) {
        printf("\033[1;32m✓ vim 模式已%s\033[0m\n", mode ? "开启" : "关闭");
    } else {
        printf("\033[1;31m✗ 设置失败\033[0m\n");
    }
}

void update_notebook_info(const char *args) {
    if (!args || !args[0]) {
        printf("\033[1;31m用法: /nbinfo <标题> [描述]\033[0m\n");
        return;
    }
    
    char title[MAX_NOTEBOOK_NAME] = "";
    char desc[MAX_CONTENT] = "";
    
    // 解析标题和描述（用 | 分隔）
    char *sep = strchr(args, '|');
    if (sep) {
        *sep = '\0';
        strncpy(title, args, MAX_NOTEBOOK_NAME - 1);
        strncpy(desc, sep + 1, MAX_CONTENT - 1);
    } else {
        strncpy(title, args, MAX_NOTEBOOK_NAME - 1);
    }
    
    // 去除首尾空格
    while (title[0] == ' ') memmove(title, title + 1, strlen(title));
    char *end = title + strlen(title) - 1;
    while (end > title && *end == ' ') *end-- = '\0';
    
    while (desc[0] == ' ') memmove(desc, desc + 1, strlen(desc));
    end = desc + strlen(desc) - 1;
    while (end > desc && *end == ' ') *end-- = '\0';
    
    if (api_update_notebook(current_notebook, title, desc) == 0) {
        printf("\033[1;32m✓ 笔记本信息已更新\033[0m\n");
        if (title[0]) printf("  标题: %s\n", title);
        if (desc[0]) printf("  描述: %s\n", desc);
    } else {
        printf("\033[1;31m✗ 更新失败\033[0m\n");
    }
}

void set_note_priority(int index, const char *priority) {
    if (index < 1 || index > note_count) {
        printf("\033[1;31m错误: 无效的笔记编号\033[0m\n");
        return;
    }
    if (strcmp(priority, "P0") != 0 && strcmp(priority, "P1") != 0 && strcmp(priority, "P2") != 0) {
        printf("\033[1;31m错误: 优先级必须是 P0、P1 或 P2\033[0m\n");
        return;
    }
    
    Note *note = &notes[index - 1];
    char tags_json[512] = "[";
    for (int i = 0; i < note->tag_count; i++) {
        if (i > 0) strcat(tags_json, ",");
        strcat(tags_json, "\"");
        strcat(tags_json, note->tags[i]);
        strcat(tags_json, "\"");
    }
    strcat(tags_json, "]");
    
    if (api_update_note(current_notebook, note->id, priority, tags_json, 
                        note->pinned, note->author, note->status, note->due) == 0) {
        printf("\033[1;32m✓ 优先级已设置为 %s\033[0m\n", priority);
        api_fetch_notes(current_notebook);
    } else {
        printf("\033[1;31m✗ 更新失败\033[0m\n");
    }
}

void set_note_tags(int index, const char *tags_str) {
    if (index < 1 || index > note_count) {
        printf("\033[1;31m错误: 无效的笔记编号\033[0m\n");
        return;
    }
    
    Note *note = &notes[index - 1];
    char tags_json[512] = "[";
    int first = 1;
    
    // 解析逗号分隔的标签
    char buf[512];
    strncpy(buf, tags_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    char *token = strtok(buf, ",");
    while (token && first < MAX_TAGS) {
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';
        
        if (*token) {
            if (!first) strcat(tags_json, ",");
            strcat(tags_json, "\"");
            strcat(tags_json, token);
            strcat(tags_json, "\"");
            first = 0;
        }
        token = strtok(NULL, ",");
    }
    strcat(tags_json, "]");
    
    if (api_update_note(current_notebook, note->id, note->priority, tags_json, 
                        note->pinned, note->author, note->status, note->due) == 0) {
        printf("\033[1;32m✓ 标签已更新\033[0m\n");
        api_fetch_notes(current_notebook);
    } else {
        printf("\033[1;31m✗ 更新失败\033[0m\n");
    }
}

void toggle_note_pinned(int index) {
    if (index < 1 || index > note_count) {
        printf("\033[1;31m错误: 无效的笔记编号\033[0m\n");
        return;
    }
    
    Note *note = &notes[index - 1];
    char tags_json[512] = "[";
    for (int i = 0; i < note->tag_count; i++) {
        if (i > 0) strcat(tags_json, ",");
        strcat(tags_json, "\"");
        strcat(tags_json, note->tags[i]);
        strcat(tags_json, "\"");
    }
    strcat(tags_json, "]");
    
    int new_pinned = !note->pinned;
    if (api_update_note(current_notebook, note->id, note->priority, tags_json, 
                        new_pinned, note->author, note->status, note->due) == 0) {
        printf("\033[1;32m✓ 置顶状态已%s\033[0m\n", new_pinned ? "开启" : "关闭");
        api_fetch_notes(current_notebook);
    } else {
        printf("\033[1;31m✗ 更新失败\033[0m\n");
    }
}

void set_note_author(int index, const char *author) {
    if (index < 1 || index > note_count) {
        printf("\033[1;31m错误: 无效的笔记编号\033[0m\n");
        return;
    }
    if (strcmp(author, "A") != 0 && strcmp(author, "M") != 0) {
        printf("\033[1;31m错误: 作者必须是 A 或 M\033[0m\n");
        return;
    }
    
    Note *note = &notes[index - 1];
    char tags_json[512] = "[";
    for (int i = 0; i < note->tag_count; i++) {
        if (i > 0) strcat(tags_json, ",");
        strcat(tags_json, "\"");
        strcat(tags_json, note->tags[i]);
        strcat(tags_json, "\"");
    }
    strcat(tags_json, "]");
    
    if (api_update_note(current_notebook, note->id, note->priority, tags_json, 
                        note->pinned, author, note->status, note->due) == 0) {
        printf("\033[1;32m✓ 作者已设置为 %s\033[0m\n", author);
        api_fetch_notes(current_notebook);
    } else {
        printf("\033[1;31m✗ 更新失败\033[0m\n");
    }
}

void set_note_due(int index, const char *due) {
    if (index < 1 || index > note_count) {
        printf("\033[1;31m错误: 无效的笔记编号\033[0m\n");
        return;
    }
    
    Note *note = &notes[index - 1];
    char tags_json[512] = "[";
    for (int i = 0; i < note->tag_count; i++) {
        if (i > 0) strcat(tags_json, ",");
        strcat(tags_json, "\"");
        strcat(tags_json, note->tags[i]);
        strcat(tags_json, "\"");
    }
    strcat(tags_json, "]");
    
    if (api_update_note(current_notebook, note->id, note->priority, tags_json, 
                        note->pinned, note->author, note->status, due) == 0) {
        if (due && due[0]) {
            printf("\033[1;32m✓ 截止日期已设置为 %s\033[0m\n", due);
        } else {
            printf("\033[1;32m✓ 截止日期已清除\033[0m\n");
        }
        api_fetch_notes(current_notebook);
    } else {
        printf("\033[1;31m✗ 更新失败\033[0m\n");
    }
}

// ---------- 输入历史 ----------

void push_line_history(const char *line) {
    if (!line || strlen(line) == 0) return;
    if (line_history_count > 0 &&
        strcmp(line_history[line_history_count - 1], line) == 0)
        return;
    if (line_history_count >= MAX_LINE_HISTORY) {
        for (int i = 0; i < line_history_count - 1; i++)
            strcpy(line_history[i], line_history[i + 1]);
        line_history_count--;
    }
    strncpy(line_history[line_history_count], line, MAX_CONTENT - 1);
    line_history[line_history_count][MAX_CONTENT - 1] = '\0';
    line_history_count++;
}

// ---------- 输入处理 ----------

// 简单的单行输入缓冲区
typedef struct {
    char buf[MAX_CONTENT];
    int len;
    int pos;  // 光标位置
} LineBuffer;

void lb_init(LineBuffer *lb) {
    lb->buf[0] = '\0';
    lb->len = 0;
    lb->pos = 0;
}

void lb_redraw(LineBuffer *lb) {
    printf("\r\033[K%s", lb->buf);
    int chars_after = lb->len - lb->pos;
    if (chars_after > 0)
        printf("\033[%dD", chars_after);
    fflush(stdout);
}

// 在光标位置插入换行符
void lb_insert_newline(LineBuffer *lb) {
    if (lb->len >= MAX_CONTENT - 1) return;
    memmove(lb->buf + lb->pos + 1, lb->buf + lb->pos, lb->len - lb->pos + 1);
    lb->buf[lb->pos] = '\n';
    lb->len++;
    lb->pos++;
    // 显示换行
    printf("\n\033[K");
    if (lb->pos < lb->len) {
        int lines_after = 0;
        for (int i = lb->pos; i < lb->len; i++) {
            if (lb->buf[i] == '\n') lines_after++;
        }
        printf("%s", lb->buf + lb->pos);
        int chars_after = lb->len - lb->pos;
        if (chars_after > 0)
            printf("\033[%dD", chars_after);
        if (lines_after > 0)
            printf("\033[%dA", lines_after);
    }
    fflush(stdout);
}

int read_line(char *buffer, size_t size) {
    LineBuffer lb;
    lb_init(&lb);
    int hist_index = line_history_count;

    buffer[0] = '\0';

    while (1) {
        char ch;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n <= 0) return -1;

        if (ch == '\r') {
            // Enter (\r): 提交
            if (lb.len == 0) continue;  // 空输入忽略
            strncpy(buffer, lb.buf, size - 1);
            buffer[size - 1] = '\0';
            push_line_history(lb.buf);
            printf("\n");
            return 0;
        } else if (ch == '\n') {
            // Ctrl+J 或某些终端的 Enter: 也视为提交
            if (lb.len == 0) continue;
            strncpy(buffer, lb.buf, size - 1);
            buffer[size - 1] = '\0';
            push_line_history(lb.buf);
            printf("\n");
            return 0;
        } else if (ch == 3) {
            // Ctrl+C: 强制退出
            printf("\n");
            disable_raw_mode();
            exit(0);
        } else if (ch == 127 || ch == 8) {
            // Backspace
            if (lb.pos > 0) {
                lb.pos--;
                memmove(lb.buf + lb.pos, lb.buf + lb.pos + 1, lb.len - lb.pos);
                lb.len--;
                lb_redraw(&lb);
            }
        } else if (ch == '\033') {
            // 转义序列
            char seq[4];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;

            // 检查 Shift+Enter 序列: \033[13;2~ 或 \033\r
            if (seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
                // 检查是否是 \033[13;2~ (Shift+Enter)
                if (seq[1] == '1') {
                    char more[3];
                    if (read(STDIN_FILENO, &more[0], 1) <= 0) continue;
                    if (more[0] == '3') {
                        if (read(STDIN_FILENO, &more[1], 1) <= 0) continue;
                        if (more[1] == ';') {
                            if (read(STDIN_FILENO, &more[2], 1) <= 0) continue;
                            if (more[2] == '2') {
                                char last;
                                if (read(STDIN_FILENO, &last, 1) <= 0) continue;
                                if (last == '~') {
                                    // Shift+Enter: 换行
                                    lb_insert_newline(&lb);
                                    continue;
                                }
                            }
                        }
                    }
                }
                if (read(STDIN_FILENO, &seq[2], 1) <= 0) continue;
            } else if (seq[0] == '\r' || seq[0] == '\n') {
                // Alt+Enter: 换行
                lb_insert_newline(&lb);
                continue;
            } else {
                if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
            }

            if (seq[0] == '[') {
                if (seq[1] == 'A') {
                    // 上箭头：浏览历史
                    if (hist_index > 0) {
                        hist_index--;
                        strncpy(lb.buf, line_history[hist_index], MAX_CONTENT - 1);
                        lb.buf[MAX_CONTENT - 1] = '\0';
                        lb.len = (int)strlen(lb.buf);
                        lb.pos = lb.len;
                        lb_redraw(&lb);
                    }
                } else if (seq[1] == 'B') {
                    // 下箭头
                    if (hist_index < line_history_count) {
                        hist_index++;
                        if (hist_index == line_history_count) {
                            lb.buf[0] = '\0';
                            lb.len = 0;
                        } else {
                            strncpy(lb.buf, line_history[hist_index], MAX_CONTENT - 1);
                            lb.buf[MAX_CONTENT - 1] = '\0';
                            lb.len = (int)strlen(lb.buf);
                        }
                        lb.pos = lb.len;
                        lb_redraw(&lb);
                    }
                } else if (seq[1] == 'C') {
                    // 右箭头
                    if (lb.pos < lb.len) {
                        lb.pos++;
                        printf("\033[C");
                        fflush(stdout);
                    }
                } else if (seq[1] == 'D') {
                    // 左箭头
                    if (lb.pos > 0) {
                        lb.pos--;
                        printf("\033[D");
                        fflush(stdout);
                    }
                }
            }
        } else if (isprint((unsigned char)ch) || ch == '\t' || (unsigned char)ch > 127) {
            // 可打印字符（包括 UTF-8 中文）
            if (lb.len >= MAX_CONTENT - 1) continue;
            // 在光标位置插入
            memmove(lb.buf + lb.pos + 1, lb.buf + lb.pos, lb.len - lb.pos + 1);
            lb.buf[lb.pos] = ch;
            lb.len++;
            lb.pos++;
            // 重新显示
            printf("%s", lb.buf + lb.pos - 1);
            int chars_after = lb.len - lb.pos;
            if (chars_after > 0)
                printf("\033[%dD", chars_after);
            fflush(stdout);
        }
    }
}

// ---------- 主循环 ----------

void main_loop() {
    char input[MAX_CONTENT];

    while (1) {
        printf("\n\033[1;36mnoteshell\033[0m[\033[1;33m%s\033[0m]> ", current_notebook);
        fflush(stdout);

        if (read_line(input, sizeof(input)) != 0) {
            break;
        }

        if (strlen(input) == 0) continue;

        if (input[0] == '/') {
            if (strcmp(input, "/exit") == 0 || strcmp(input, "/quit") == 0) {
                printf("\033[1;32m再见!\033[0m\n");
                break;
            } else if (strcmp(input, "/help") == 0) {
                show_help();
            } else if (strcmp(input, "/clear") == 0) {
                clear_screen();
            } else if (strcmp(input, "/list") == 0) {
                current_page = 0;
                api_fetch_notes(current_notebook);
                show_notes();
            } else if (strcmp(input, "/history") == 0) {
                show_history(NULL);
            } else if (strncmp(input, "/history ", 9) == 0) {
                show_history(input + 9);
            } else if (strncmp(input, "/search ", 8) == 0) {
                search_notes(input + 8);
            } else if (strncmp(input, "/edit ", 6) == 0) {
                char *rest = input + 6;
                char *sp = strchr(rest, ' ');
                if (!sp) {
                    printf("\033[1;31m用法: /edit <编号> <新内容>\033[0m\n");
                } else {
                    *sp = '\0';
                    int index = atoi(rest);
                    char *new_content = sp + 1;
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
            } else if (strcmp(input, "/notebooks") == 0 || strcmp(input, "/nbs") == 0) {
                show_notebooks();
            } else if (strncmp(input, "/notebook ", 10) == 0 || strncmp(input, "/nb ", 4) == 0) {
                char *name;
                if (strncmp(input, "/nb ", 4) == 0)
                    name = input + 4;
                else
                    name = input + 10;
                while (*name == ' ') name++;
                if (!*name) {
                    printf("\033[1;31m用法: /notebook <名称>\033[0m\n");
                } else {
                    switch_notebook(name);
                }
            } else if (strncmp(input, "/nbadd ", 7) == 0) {
                char *name = input + 7;
                while (*name == ' ') name++;
                create_notebook(name);
            } else if (strcmp(input, "/nbvim") == 0) {
                show_vim_mode();
            } else if (strncmp(input, "/nbvim ", 7) == 0) {
                char *value = input + 7;
                while (*value == ' ') value++;
                set_vim_mode(value);
            } else if (strncmp(input, "/nbinfo ", 8) == 0) {
                char *args = input + 8;
                while (*args == ' ') args++;
                update_notebook_info(args);
            } else if (strncmp(input, "/priority ", 10) == 0) {
                char *rest = input + 10;
                char *sp = strchr(rest, ' ');
                if (!sp) {
                    printf("\033[1;31m用法: /priority <编号> <P0|P1|P2>\033[0m\n");
                } else {
                    *sp = '\0';
                    int index = atoi(rest);
                    char *priority = sp + 1;
                    while (*priority == ' ') priority++;
                    set_note_priority(index, priority);
                }
            } else if (strncmp(input, "/tags ", 6) == 0) {
                char *rest = input + 6;
                char *sp = strchr(rest, ' ');
                if (!sp) {
                    printf("\033[1;31m用法: /tags <编号> <tag1,tag2,...>\033[0m\n");
                } else {
                    *sp = '\0';
                    int index = atoi(rest);
                    char *tags = sp + 1;
                    while (*tags == ' ') tags++;
                    set_note_tags(index, tags);
                }
            } else if (strncmp(input, "/pin ", 5) == 0) {
                int index = atoi(input + 5);
                toggle_note_pinned(index);
            } else if (strncmp(input, "/author ", 8) == 0) {
                char *rest = input + 8;
                char *sp = strchr(rest, ' ');
                if (!sp) {
                    printf("\033[1;31m用法: /author <编号> <A|M>\033[0m\n");
                } else {
                    *sp = '\0';
                    int index = atoi(rest);
                    char *author = sp + 1;
                    while (*author == ' ') author++;
                    set_note_author(index, author);
                }
            } else if (strncmp(input, "/due ", 5) == 0) {
                char *rest = input + 5;
                char *sp = strchr(rest, ' ');
                if (!sp) {
                    printf("\033[1;31m用法: /due <编号> <YYYY-MM-DD>\033[0m\n");
                } else {
                    *sp = '\0';
                    int index = atoi(rest);
                    char *due = sp + 1;
                    while (*due == ' ') due++;
                    set_note_due(index, due);
                }
            } else {
                printf("\033[1;31m未知命令: %s\033[0m\n", input);
                printf("输入 \033[1;32m/help\033[0m 查看帮助\n");
            }
        } else {
            // 检查是否是特殊符号包裹的内容
            char processed[MAX_CONTENT];
            if (process_special_symbols(input, processed, sizeof(processed))) {
                add_note(processed);
            } else {
                add_note(input);
            }
        }
    }
}

int main() {
    // 尝试加载配置
    if (load_config() != 0) {
        // 首次运行，提示配置
        if (prompt_server_config() != 0) {
            fprintf(stderr, "配置失败\n");
            return 1;
        }
    }

    // 设置终端为 raw 模式
    enable_raw_mode();

    // 验证服务器连接
    if (api_verify_connection() != 0) {
        disable_raw_mode();
        printf("\033[1;31m无法连接到服务器 %s:%s\033[0m\n", server_host, server_port);
        printf("请检查:\n");
        printf("  1. 服务器是否正在运行\n");
        printf("  2. IP 地址和端口是否正确\n");
        printf("  3. 防火墙设置\n\n");
        printf("配置文件: ~/.noteshell_config\n");
        printf("你可以编辑该文件修改服务器地址，或删除它重新配置。\n");
        return 1;
    }

    // 加载默认笔记本
    if (api_fetch_notes(current_notebook) != 0) {
        disable_raw_mode();
        printf("\033[1;31m无法加载笔记本数据\033[0m\n");
        return 1;
    }

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

    printf("  服务器: \033[1;32m%s:%s\033[0m\n", server_host, server_port);
    printf("  笔记本: \033[1;33m%s\033[0m (\033[1;32m%d\033[0m 条笔记)\n", current_notebook, note_count);
    printf("  输入 \033[1;32m/help\033[0m 查看帮助，\033[1;32m/exit\033[0m 退出\n");
    printf("  \033[2mEnter 确认，Shift+Enter 换行\033[0m\n");

    main_loop();

    disable_raw_mode();
    return 0;
}
