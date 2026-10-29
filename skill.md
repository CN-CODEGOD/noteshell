# NoteShell 使用指南

## 概述

NoteShell 是一个终端笔记本工具，支持服务器同步、任务管理、Markdown 渲染和网页端访问。提供三种使用方式：
- **CLI 命令行工具** - 快速操作，适合脚本和自动化
- **TUI 交互式界面** - 传统终端界面
- **Web 网页端** - 图形化界面，支持完整编辑

## 快速开始

### 方式一：CLI 命令行（推荐）

```bash
# 配置服务器连接
./noteshell-cli config --host 127.0.0.1 --port 8001

# 查看帮助
./noteshell-cli --help

# 列出笔记本
./noteshell-cli notebooks list

# 创建笔记本
./noteshell-cli notebooks create work

# 添加笔记
./noteshell-cli notes add -c "任务内容" -p P1 -t "工作,紧急" -d 2026-10-30

# 列出笔记
./noteshell-cli notes list

# 快速操作
./noteshell-cli quick pin 1          # 置顶
./noteshell-cli quick priority 1 P0  # 设置优先级
./noteshell-cli quick status 1 done  # 标记完成
```

### 方式二：TUI 交互式界面

```bash
# 启动服务器
python3 server.py

# 首次配置客户端
./noteshell
# 输入服务器 IP 和端口
```

### 方式三：Web 网页端

```
http://127.0.0.1:8001/notes
```

## CLI 命令详解

### 配置管理

```bash
# 查看当前配置
./noteshell-cli config

# 设置服务器
./noteshell-cli config --host 192.168.1.100 --port 8001
```

### 笔记本管理

```bash
# 列出所有笔记本
./noteshell-cli notebooks list

# 创建笔记本
./noteshell-cli notebooks create work

# 查看笔记本信息
./noteshell-cli notebooks info work

# 更新笔记本
./noteshell-cli notebooks update work --title "工作笔记" --desc "日常工作记录"
```

### 笔记操作

```bash
# 列出笔记
./noteshell-cli notes list
./noteshell-cli -n work notes list  # 指定笔记本

# 添加笔记
./noteshell-cli notes add \
  --content "完成项目报告" \
  --priority P1 \
  --tags "工作,紧急" \
  --author A \
  --due 2026-10-30

# 查看笔记详情
./noteshell-cli notes get 1

# 编辑笔记
./noteshell-cli notes edit 1 --content "新内容" --priority P0

# 删除笔记
./noteshell-cli notes delete 1

# 搜索笔记
./noteshell-cli notes search --keyword "项目"
./noteshell-cli notes search --keyword "工作" --tag "紧急"
```

### 快速操作

```bash
# 切换置顶状态
./noteshell-cli quick pin 1

# 设置优先级
./noteshell-cli quick priority 1 P0

# 设置状态
./noteshell-cli quick status 1 done

# 设置截止日期
./noteshell-cli quick due 1 2026-11-01
```

### 使用示例

```bash
# 创建工作笔记本
./noteshell-cli notebooks create work
./noteshell-cli notebooks update work --title "工作笔记"

# 添加紧急任务
./noteshell-cli -n work notes add \
  -c "准备明天的演示" \
  -p P0 \
  -t "工作,紧急" \
  -d 2026-10-30

# 添加日常任务
./noteshell-cli -n work notes add \
  -c "整理文档" \
  -p P2 \
  -t "工作"

# 查看任务列表
./noteshell-cli -n work notes list

# 标记完成
./noteshell-cli -n work quick status 1 done

# 搜索任务
./noteshell-cli -n work notes search -k "演示"
```

## 终端命令

### 笔记操作

| 命令 | 说明 | 示例 |
|------|------|------|
| `/list` | 列出当前笔记本的所有笔记 | `/list` |
| `/edit <编号> <新内容>` | 修改指定笔记 | `/edit 1 新的内容` |
| `/delete <编号>` | 删除指定笔记 | `/delete 3` |
| `/search <关键词>` | 搜索笔记 | `/search 工作` |
| `/history [关键词]` | 按时间翻查历史 | `/history` 或 `/history 项目` |
| `/export [文件名]` | 导出为 Markdown | `/export` 或 `/export notes.md` |
| `/count` | 显示笔记总数 | `/count` |

### 任务管理

| 命令 | 说明 | 示例 |
|------|------|------|
| `/priority <编号> <P0\|P1\|P2>` | 设置优先级 | `/priority 1 P0` |
| `/tags <编号> <标签1,标签2,...>` | 设置标签 | `/tags 1 工作,紧急` |
| `/pin <编号>` | 切换置顶状态 | `/pin 1` |
| `/author <编号> <A\|M>` | 设置作者 | `/author 1 A` |
| `/due <编号> <YYYY-MM-DD>` | 设置截止日期 | `/due 1 2026-10-30` |

**优先级说明：**
- `P0` - 最高优先级（红色）
- `P1` - 高优先级（黄色）
- `P2` - 普通优先级（绿色）

**作者说明：**
- `A` - noteshell 创建
- `M` - 其他来源

### 笔记本管理

| 命令 | 说明 | 示例 |
|------|------|------|
| `/notebooks` 或 `/nbs` | 列出所有笔记本 | `/notebooks` |
| `/notebook <名称>` 或 `/nb <名称>` | 切换笔记本 | `/nb work` |
| `/nbadd <名称>` | 创建新笔记本 | `/nbadd work` |
| `/nbinfo <标题> [\|描述]` | 设置笔记本标题和描述 | `/nbinfo 工作笔记\|日常工作记录` |
| `/nbvim` | 查看 vim 模式 | `/nbvim` |
| `/nbvim on\|off` | 设置 vim 模式 | `/nbvim on` |

### 其他命令

| 命令 | 说明 |
|------|------|
| `/help` | 显示帮助信息 |
| `/clear` | 清屏 |
| `/exit` 或 `/quit` | 退出程序 |

## 输入操作

### 基本输入

- **Enter** - 提交笔记
- **Shift+Enter** - 换行（多行输入）
- **Ctrl+C** - 强制退出
- **↑/↓** - 浏览输入历史
- **←/→** - 移动光标
- **Backspace** - 删除字符

### 特殊符号

- `"文本"` - 引号包裹的文本（支持多行）
- `` `代码` `` - 反引号包裹的代码块（支持多行）

### 示例

```bash
# 单行笔记
这是一条笔记

# 多行笔记（使用 Shift+Enter）
第一行
第二行
第三行

# 使用引号包裹多行
"这是
多行
内容"

# 使用反引号包裹代码
`def hello():
    print("Hello")`
```

## Markdown 支持

### 基本语法

```markdown
# 一级标题
## 二级标题
### 三级标题

**粗体** 和 *斜体*

- 无序列表项 1
- 无序列表项 2

1. 有序列表项 1
2. 有序列表项 2

> 引用文本

[链接](https://example.com)

`行内代码`

```python
代码块
```
```

### 待办事项

```markdown
- [ ] 未完成的任务
- [x] 已完成的任务
```

在网页端可以直接点击复选框切换状态。

## 网页端功能

### 访问

```
http://服务器IP:端口/notes
```

### 功能

1. **笔记本列表** - 左侧边栏显示所有笔记本
2. **笔记列表** - 右侧显示当前笔记本的笔记
3. **添加笔记** - 点击右上角"+ 添加笔记"按钮
4. **编辑笔记** - 悬停显示编辑按钮（✎）
5. **删除笔记** - 悬停显示删除按钮（🗑）
6. **快速操作**：
   - 点击优先级标签循环切换（P0→P1→P2）
   - 点击状态标签循环切换（待办→已完成→已归档）
   - 点击复选框切换任务完成状态
7. **实时同步** - SSE 实时更新，多窗口同步

### 显示的信息

- 优先级（彩色标签）
- 标签（#标签名）
- 作者（A/M）
- 状态（待办/已完成/已归档）
- 截止日期（📅 图标，过期显示红色）
- 置顶标记（📌）
- 创建/修改时间

## API 接口

### 笔记本

```bash
# 列出所有笔记本
GET /api/notebooks

# 创建笔记本
POST /api/notebooks
Body: {"name": "work"}

# 获取笔记本
GET /api/notebook/{name}

# 更新笔记本
PUT /api/notebook/{name}
Body: {"title": "标题", "desc": "描述"}
```

### 笔记

```bash
# 添加笔记
POST /api/notebook/{name}/notes
Body: {
  "content": "内容",
  "priority": "P1",
  "tags": ["工作"],
  "pinned": true,
  "author": "A",
  "due": "2026-10-30"
}

# 获取单条笔记
GET /api/notebook/{name}/note/{id}

# 更新笔记
PUT /api/notebook/{name}/note/{id}
Body: {
  "content": "新内容",
  "priority": "P0",
  "status": "done"
}

# 删除笔记
DELETE /api/notebook/{name}/note/{id}

# 搜索笔记
GET /api/notebook/{name}/search?q=关键词&tag=标签
```

### 实时推送

```bash
# SSE 事件流
GET /api/events?notebook={name}
```

## 数据存储

### 服务器端

```
~/.noteshell_data/
├── _manifest.json          # 笔记本注册表
├── default.json            # 默认笔记本数据
├── work.json              # 工作笔记本数据
└── ...
```

### 客户端配置

```
~/.noteshell_config        # 服务器连接配置
```

## Docker 部署

```bash
# 构建镜像
docker build -t noteshell-server .

# 运行容器
docker run -d \
  --name noteshell-server \
  -p 8001:8001 \
  -v ./data:/data \
  -e NOTESHELL_HOST=0.0.0.0 \
  -e NOTESHELL_PORT=8001 \
  noteshell-server

# 使用 docker-compose
docker-compose up -d
```

## 常见问题

### Q: 中文输入不显示？
A: 确保使用最新版本的 noteshell，已支持 UTF-8。

### Q: 按 Enter 没有反应？
A: 某些终端的 Enter 键发送 \n 而不是 \r，已修复。如果仍有问题，尝试 Ctrl+J。

### Q: 如何输入多行笔记？
A: 使用 Shift+Enter 换行，或使用 "" 或 `` 包裹多行内容。

### Q: 网页端无法访问？
A: 检查服务器是否运行，防火墙是否开放端口。

### Q: 如何备份数据？
A: 备份 `~/.noteshell_data/` 目录即可。

## 快捷键速查

| 快捷键 | 功能 |
|--------|------|
| Enter | 提交 |
| Shift+Enter | 换行 |
| Ctrl+C | 退出 |
| ↑/↓ | 历史 |
| ←/→ | 移动光标 |
| q | 从列表返回 |

## 环境变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `NOTESHELL_HOST` | 服务器监听地址 | `0.0.0.0` |
| `NOTESHELL_PORT` | 服务器端口 | `8001` |
| `HOME` | 数据目录位置 | `~` |

## 更多信息

- 源代码：`/vol1/1000/WorkSpace/noteshell/`
- 服务器：`server.py`
- 客户端：`noteshell.c`
- 网页端：`note.html`
