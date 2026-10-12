# NoteShell

一个运行在终端的笔记本程序，可以随时记录想法，浏览历史记录。

## 功能特点

- ✨ 简洁的终端界面
- 📝 快速记录笔记（自动添加时间戳）
- 🔍 搜索笔记内容
- 📋 分页浏览历史笔记（`/list`）
- 🕘 按时间翻查历史（`/history [关键词]`，分页 + 过滤）
- 💾 原子写入本地文件（临时文件 + `rename`，防崩溃丢数据）
- 🗄️ 多版本归档（`~/.noteshell_history.d/`，保留最近 10 份）+ `.bak` 快速回退
- ⌨️ 支持键盘快捷键（↑/↓ 历史）
- 🌐 与网页实时同步（见下文）

## 编译

```bash
make
```

## 运行

```bash
./noteshell
```

## 使用方法

### 基本操作

- **添加笔记**: 直接输入文本，按 Enter 保存
- **查看帮助**: `/help`
- **列出笔记**: `/list`（分页，↑/↓ 翻页）
- **翻查历史**: `/history` 或 `/history <关键词>`（分页，↑/↓ 或 n/p 翻页，q 返回）
- **搜索笔记**: `/search <关键词>`
- **删除笔记**: `/delete <编号>`
- **清屏**: `/clear`
- **退出**: `/exit` 或 `/quit`

### 快捷键

- `↑/↓` - 浏览历史记录
- `Enter` - 保存笔记
- `Ctrl+C` - 强制退出

## 数据存储

笔记自动保存到 `~/.noteshell_history` 文件（每条两行：时间戳 + 内容），下次启动时自动加载。

写入采用**原子操作**，避免崩溃/磁盘满时把文件截断：

1. 先写临时文件 `~/.noteshell_history.tmp` 并 `fsync` 落盘
2. 把旧的 `~/.noteshell_history` 归档为 `~/.noteshell_history.d/<时间戳>`（多版本），
   同时保留一份 `~/.noteshell_history.bak`（单份快速回退）
3. 最后 `rename` 临时文件为正式文件

归档目录只保留最近 **10** 份（`ARCHIVE_KEEP`），超出自动清理最旧的。

## 一键 Demo

```bash
./start-demo.sh              # 默认端口 8000，使用当前 $HOME
PORT=9000 ./start-demo.sh    # 自定义端口
DEMO_HOME=/tmp/demo ./start-demo.sh   # 隔离数据目录做演示
```

脚本会编译 noteshell 并启动同步服务器，打印主页/同步页地址。

## 网页同步（noteWeb + note）

`server.py` 是一个零依赖（仅 Python 标准库）的同步服务器：

- 托管 `noteWeb.html`（主页：`/`）与专用 note 页面（`/notes`）
- 提供 REST API，数据源就是 noteshell 的 `~/.noteshell_history`，两边永远一致
- 提供 SSE 长连接，noteshell 一有新笔记，网页立刻刷新

### 启动服务器

```bash
python3 server.py            # 默认 0.0.0.0:8000
# 或自定义端口
NOTESHELL_PORT=9000 python3 server.py
```

打开 <http://127.0.0.1:8000/> 看主页，打开 <http://127.0.0.1:8000/notes> 看 noteshell 消息同步页。

### 让 noteshell 推送到网页

noteshell 在你保存/删除笔记时会自动尝试推送一次（直接 TCP 发 HTTP POST，不经过 shell，双引号/反斜杠/中文都安全；失败静默不影响本地使用）。

```bash
export NOTESHELL_SYNC_URL=http://127.0.0.1:8000
./noteshell
```

未设置 `NOTESHELL_SYNC_URL` 时默认推送到 `http://127.0.0.1:8000`。

### API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET  | `/api/notes` | 返回全部笔记（JSON） |
| GET  | `/api/seq`   | 当前同步序号 |
| GET  | `/api/events`| SSE 事件流（新笔记实时推送） |
| POST | `/api/push`  | noteshell 推送入口（格式 `seq\tcount\ttimestamp\tcontent`） |

## 界面预览

```
╔══════════════════════════════════════════════════════════╗
║                                                          ║
║   ███╗   ██╗ ██████╗ ████████╗███████╗██╗  ██╗         ║
║   ████╗  ██║██╔═══██╗╚══██╔══╝██╔════╝██║  ██║         ║
║   ██╔██╗ ██║██║   ██║   ██║   ███████╗███████║         ║
║   ██║╚██╗██║██║   ██║   ██║   ╚════██║██╔══██║         ║
║   ██║ ╚████║╚██████╔╝   ██║   ███████║██║  ██║         ║
║   ╚═╝  ╚═══╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝         ║
║                                                          ║
║              终端笔记本 - 记录你的想法                    ║
║                                                          ║
╚══════════════════════════════════════════════════════════╝
```

## 系统要求

- Linux/Unix 系统
- GCC 编译器
- 支持 ANSI 转义码的终端

## 许可证

MIT License
