#!/usr/bin/env bash
# start-demo.sh —— NoteShell 端到端 demo 一键启动
#
# 用法:
#   ./start-demo.sh              # 用默认 HOME，端口 8001
#   PORT=9000 ./start-demo.sh    # 自定义端口
#   DEMO_HOME=/tmp/x ./start-demo.sh  # 自定义数据目录（隔离演示）
#
# 作用:
#   1) 编译 noteshell 客户端
#   2) 启动 server.py（笔记本数据服务器 + Web 同步页）
#   3) 打印访问地址
#
# 停止: Ctrl+C

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

PORT="${PORT:-8001}"
export NOTESHELL_PORT="$PORT"
[ -n "${DEMO_HOME:-}" ] && export HOME="$DEMO_HOME"

echo "==> 编译 noteshell 客户端"
make >/dev/null

echo "==> 启动 NoteShell 服务器 (端口 $PORT)"
echo "    数据目录: $HOME/.noteshell_data/"
echo
echo "  Web 主页  : http://127.0.0.1:$PORT/"
echo "  同步页面  : http://127.0.0.1:$PORT/notes"
echo "  API       : http://127.0.0.1:$PORT/api/notebooks"
echo
echo "  在另一个终端里运行客户端:"
echo "      ./noteshell"
echo "  首次运行会提示输入服务器 IP 地址和端口"
echo
echo "  按 Ctrl+C 停止服务器"
echo "--------------------------------------------------------------"

exec python3 server.py
