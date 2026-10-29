
‘# NoteShell - 终端笔记工具

一个功能完整的终端笔记工具，支持服务器同步、任务管理、Markdown 渲染和多端访问。


## 特性

- ✅ **多端访问** - CLI 命令行、TUI 交互式界面、Web 网页端
- ✅ **任务管理** - 优先级、标签、截止日期、状态追踪
- ✅ **Markdown 支持** - 完整的 Markdown 渲染，待办事项复选框
- ✅ **实时同步** - SSE 实时推送，多窗口同步
- ✅ **笔记本管理** - 多个笔记本，标题和描述
- ✅ **中文支持** - 完整的 UTF-8 中文输入支持
- ✅ **Docker 部署** - 一键容器化部署
- ✅ **agent 访问** - 一键agent部署

## 快速开始
# agent 使用
```
请阅读 https://github.com/CN-CODEGOD/noteshell/blob/main/install.md 并帮我设置安装noteshell agent工具
```
### 1. 启动服务器

```bash
python3 server.py
```

### 2. 使用 CLI（推荐）

```bash
# 配置连接
./noteshell-cli config --host 127.0.0.1 --port 8001

# 添加笔记
./noteshell-cli notes add -c "任务" -p P1 -t "工作" -d 2026-10-30

# 查看笔记
./noteshell-cli notes list

# 快速操作
./noteshell-cli quick status 1 done
```

### 3. 使用 TUI

```bash
./noteshell
```

### 4. 访问网页

```
http://127.0.0.1:8001/notes
```

## 文件说明

- `server.py` - Python 服务器（仅标准库）
- `noteshell.c` - C 语言 TUI 客户端
- `noteshell-cli.py` - Python CLI 命令行工具
- `note.html` - Web 网页端
- `skill.md` - 完整使用文档
- `Dockerfile` - Docker 镜像构建
- `docker-compose.yml` - Docker Compose 配置

## 文档

详细使用说明请查看 [skill.md](skill.md)

## License

MIT
