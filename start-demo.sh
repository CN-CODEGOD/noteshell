#!/usr/bin/env bash
# start-demo.sh —— NoteShell 端到端 demo 一键启动
#
# 用法:
#   ./start-demo.sh              # 用默认 HOME ($HOME)，端口 8000
#   PORT=9000 ./start-demo.sh    # 自定义端口
#   DEMO_HOME=/tmp/x ./start-demo.sh  # 自定义数据目录（隔离演示）
#
# 作用:
#   1) 编译 noteshell
#   2) 启动 server.py（托管 noteWeb.html 与 /notes 同步页）
#   3) 打印访问地址与 "让 noteshell 推送" 所需的环境变量
#
# 停止: Ctrl+C

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

PORT="${PORT:-8000}"
export NOTESHELL_PORT="$PORT"
export NOTESHELL_SYNC_URL="${NOTESHELL_SYNC_URL:-http://127.0.0.1:$PORT}"
# 默认使用当前用户的真实 HOME（笔记持久化在 ~/.noteshell_history）
[ -n "${DEMO_HOME:-}" ] && export HOME="$DEMO_HOME"

echo "==> 编译 noteshell"
make >/dev/null

echo "==> 启动同步服务器 (端口 $PORT)"
echo "    数据文件: $HOME/.noteshell_history"
echo
echo "  网页主页 : http://127.0.0.1:$PORT/"
echo "  同步页面 : http://127.0.0.1:$PORT/notes"
echo "  API      : http://127.0.0.1:$PORT/api/notes"
echo
echo "  在另一个终端里跑 noteshell（会自动推送到本服务器）:"
echo "      export NOTESHELL_SYNC_URL=$NOTESHELL_SYNC_URL"
echo "      ./noteshell"
echo
echo "  按 Ctrl+C 停止服务器"
echo "--------------------------------------------------------------"

exec python3 server.py
