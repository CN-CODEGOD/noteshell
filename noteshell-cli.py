#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NoteShell CLI - 命令行工具
"""

import argparse
import json
import os
import sys
import urllib.request
import urllib.error
import urllib.parse
from datetime import datetime

# 默认配置
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = "8001"
CONFIG_FILE = os.path.expanduser("~/.noteshell_config")


def load_config():
    """加载配置"""
    config = {"host": DEFAULT_HOST, "port": DEFAULT_PORT}
    if os.path.exists(CONFIG_FILE):
        with open(CONFIG_FILE, "r") as f:
            for line in f:
                line = line.strip()
                if "=" in line:
                    key, value = line.split("=", 1)
                    config[key.replace("server_", "")] = value
    # 环境变量优先
    config["host"] = os.environ.get("NOTESHELL_HOST", config["host"])
    config["port"] = os.environ.get("NOTESHELL_PORT", config["port"])
    return config


def save_config(host, port):
    """保存配置"""
    with open(CONFIG_FILE, "w") as f:
        f.write(f"server_host={host}\nserver_port={port}\n")
    print(f"✓ 配置已保存到 {CONFIG_FILE}")


def api_request(method, path, data=None):
    """发送 API 请求"""
    config = load_config()
    url = f"http://{config['host']}:{config['port']}{path}"
    
    headers = {"Content-Type": "application/json"}
    
    if data:
        data = json.dumps(data).encode("utf-8")
    
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    
    try:
        with urllib.request.urlopen(req, timeout=5) as response:
            body = response.read().decode("utf-8")
            if body:
                return json.loads(body)
            return {"ok": True}
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8")
        try:
            error = json.loads(body)
            print(f"✗ 错误: {error.get('error', '未知错误')}", file=sys.stderr)
        except:
            print(f"✗ HTTP {e.code}: {body}", file=sys.stderr)
        sys.exit(1)
    except urllib.error.URLError as e:
        print(f"✗ 无法连接到服务器 {config['host']}:{config['port']}", file=sys.stderr)
        print(f"  请检查服务器是否运行，或运行 'noteshell-cli config' 配置连接", file=sys.stderr)
        sys.exit(1)


def cmd_config(args):
    """配置服务器连接"""
    if args.host and args.port:
        save_config(args.host, args.port)
    else:
        config = load_config()
        print(f"当前配置:")
        print(f"  服务器: {config['host']}:{config['port']}")
        print(f"  配置文件: {CONFIG_FILE}")


def cmd_notebooks(args):
    """笔记本管理"""
    if args.action == "list":
        result = api_request("GET", "/api/notebooks")
        notebooks = result.get("notebooks", [])
        if not notebooks:
            print("暂无笔记本")
            return
        
        print(f"共 {len(notebooks)} 个笔记本:\n")
        for nb in notebooks:
            current = " * " if nb.get("name") == args.notebook else "   "
            title = nb.get("title", nb["name"])
            desc = nb.get("desc", "")
            print(f"{current}{nb['name']:20s} {title}")
            if desc:
                print(f"      {desc}")
            print(f"      笔记: {nb.get('count', 0)}, vim: {'on' if nb.get('vim_mode') else 'off'}")
            print()
    
    elif args.action == "create":
        if not args.name:
            print("✗ 请指定笔记本名称", file=sys.stderr)
            sys.exit(1)
        result = api_request("POST", "/api/notebooks", {"name": args.name})
        if result.get("ok"):
            print(f"✓ 笔记本 '{args.name}' 已创建")
    
    elif args.action == "info":
        name = args.name or args.notebook
        result = api_request("GET", f"/api/notebook/{name}")
        print(f"笔记本: {result['name']}")
        print(f"标题: {result.get('title', '')}")
        print(f"描述: {result.get('desc', '')}")
        print(f"笔记数: {result.get('count', 0)}")
        print(f"vim 模式: {'开启' if result.get('vim_mode') else '关闭'}")
    
    elif args.action == "update":
        name = args.name or args.notebook
        data = {}
        if args.title:
            data["title"] = args.title
        if args.desc:
            data["desc"] = args.desc
        if data:
            api_request("PUT", f"/api/notebook/{name}", data)
            print(f"✓ 笔记本 '{name}' 已更新")
        else:
            print("✗ 请指定要更新的字段 (--title 或 --desc)", file=sys.stderr)
            sys.exit(1)


def cmd_notes(args):
    """笔记操作"""
    notebook = args.notebook
    
    if args.action == "list":
        result = api_request("GET", f"/api/notebook/{notebook}")
        notes = result.get("notes", [])
        
        if not notes:
            print(f"笔记本 '{notebook}' 暂无笔记")
            return
        
        # 排序：置顶优先，然后按更新时间
        notes.sort(key=lambda n: (not n.get("pinned", False), n.get("updated", n.get("ts", ""))), reverse=False)
        
        print(f"笔记本: {notebook} (共 {len(notes)} 条笔记)\n")
        
        for i, note in enumerate(notes, 1):
            pinned = "📌 " if note.get("pinned") else ""
            priority = note.get("priority", "P2")
            prio_colors = {"P0": "🔴", "P1": "🟡", "P2": "🟢"}
            prio_icon = prio_colors.get(priority, "")
            
            tags = " ".join([f"#{tag}" for tag in note.get("tags", [])])
            author = note.get("author", "A")
            status = note.get("status", "todo")
            status_icons = {"todo": "⏳", "done": "✓", "archived": "📦"}
            status_icon = status_icons.get(status, "")
            
            due = note.get("due", "")
            due_str = f" 📅{due}" if due else ""
            
            print(f"{i:3d}. {pinned}{prio_icon} {status_icon} [{priority}] {note.get('ts', '')}")
            if tags:
                print(f"      {tags}")
            print(f"      {note.get('content', '')[:60]}")
            print(f"      作者: {author}{due_str}")
            print()
    
    elif args.action == "add":
        if not args.content:
            print("✗ 请指定笔记内容", file=sys.stderr)
            sys.exit(1)
        
        data = {"content": args.content}
        if args.priority:
            data["priority"] = args.priority
        if args.tags:
            data["tags"] = [t.strip() for t in args.tags.split(",")]
        if args.pinned:
            data["pinned"] = True
        if args.author:
            data["author"] = args.author
        if args.due:
            data["due"] = args.due
        
        result = api_request("POST", f"/api/notebook/{notebook}/notes", data)
        if result.get("ok"):
            print(f"✓ 笔记已添加 (ID: {result.get('id')})")
    
    elif args.action == "get":
        if not args.id:
            print("✗ 请指定笔记 ID", file=sys.stderr)
            sys.exit(1)
        
        result = api_request("GET", f"/api/notebook/{notebook}/note/{args.id}")
        print(f"ID: {result.get('id')}")
        print(f"时间: {result.get('ts')}")
        print(f"优先级: {result.get('priority')}")
        print(f"标签: {', '.join(result.get('tags', []))}")
        print(f"作者: {result.get('author')}")
        print(f"状态: {result.get('status')}")
        print(f"截止日期: {result.get('due', '无')}")
        print(f"置顶: {'是' if result.get('pinned') else '否'}")
        print(f"\n内容:\n{result.get('content')}")
    
    elif args.action == "edit":
        if not args.id:
            print("✗ 请指定笔记 ID", file=sys.stderr)
            sys.exit(1)
        
        data = {}
        if args.content:
            data["content"] = args.content
        if args.priority:
            data["priority"] = args.priority
        if args.tags:
            data["tags"] = [t.strip() for t in args.tags.split(",")]
        if args.pinned is not None:
            data["pinned"] = args.pinned
        if args.author:
            data["author"] = args.author
        if args.due:
            data["due"] = args.due
        if args.status:
            data["status"] = args.status
        
        if not data:
            print("✗ 请指定要更新的字段", file=sys.stderr)
            sys.exit(1)
        
        api_request("PUT", f"/api/notebook/{notebook}/note/{args.id}", data)
        print(f"✓ 笔记 {args.id} 已更新")
    
    elif args.action == "delete":
        if not args.id:
            print("✗ 请指定笔记 ID", file=sys.stderr)
            sys.exit(1)
        
        api_request("DELETE", f"/api/notebook/{notebook}/note/{args.id}")
        print(f"✓ 笔记 {args.id} 已删除")
    
    elif args.action == "search":
        if not args.keyword:
            print("✗ 请指定搜索关键词", file=sys.stderr)
            sys.exit(1)
        
        path = f"/api/notebook/{notebook}/search?q={urllib.parse.quote(args.keyword)}"
        if args.tag:
            path += f"&tag={urllib.parse.quote(args.tag)}"
        
        result = api_request("GET", path)
        notes = result.get("notes", [])
        
        if not notes:
            print(f"未找到包含 '{args.keyword}' 的笔记")
            return
        
        print(f"找到 {len(notes)} 条结果:\n")
        for i, note in enumerate(notes, 1):
            print(f"{i}. [{note.get('priority', 'P2')}] {note.get('ts', '')}")
            print(f"   {note.get('content', '')[:60]}")
            print()


def cmd_quick(args):
    """快速操作"""
    notebook = args.notebook
    note_id = args.id
    
    if args.action == "pin":
        # 获取当前状态
        note = api_request("GET", f"/api/notebook/{notebook}/note/{note_id}")
        new_pinned = not note.get("pinned", False)
        api_request("PUT", f"/api/notebook/{notebook}/note/{note_id}", {"pinned": new_pinned})
        print(f"✓ 置顶状态: {'开启' if new_pinned else '关闭'}")
    
    elif args.action == "priority":
        if not args.value:
            print("✗ 请指定优先级 (P0/P1/P2)", file=sys.stderr)
            sys.exit(1)
        api_request("PUT", f"/api/notebook/{notebook}/note/{note_id}", {"priority": args.value})
        print(f"✓ 优先级已设置为 {args.value}")
    
    elif args.action == "status":
        if not args.value:
            print("✗ 请指定状态 (todo/done/archived)", file=sys.stderr)
            sys.exit(1)
        api_request("PUT", f"/api/notebook/{notebook}/note/{note_id}", {"status": args.value})
        print(f"✓ 状态已设置为 {args.value}")
    
    elif args.action == "due":
        if not args.value:
            print("✗ 请指定截止日期 (YYYY-MM-DD)", file=sys.stderr)
            sys.exit(1)
        api_request("PUT", f"/api/notebook/{notebook}/note/{note_id}", {"due": args.value})
        print(f"✓ 截止日期已设置为 {args.value}")


def main():
    parser = argparse.ArgumentParser(
        description="NoteShell CLI - 命令行笔记工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  noteshell-cli config --host 127.0.0.1 --port 8001
  noteshell-cli notebooks list
  noteshell-cli notebooks create work
  noteshell-cli notes add --content "任务" --priority P1 --tags "工作,紧急"
  noteshell-cli notes list
  noteshell-cli notes edit 1 --status done
  noteshell-cli quick pin 1
  noteshell-cli quick priority 1 P0
        """
    )
    
    parser.add_argument("--notebook", "-n", default="default", help="笔记本名称 (默认: default)")
    
    subparsers = parser.add_subparsers(dest="command", help="命令")
    
    # config 命令
    config_parser = subparsers.add_parser("config", help="配置服务器连接")
    config_parser.add_argument("--host", help="服务器地址")
    config_parser.add_argument("--port", help="服务器端口")
    
    # notebooks 命令
    nb_parser = subparsers.add_parser("notebooks", help="笔记本管理")
    nb_parser.add_argument("action", choices=["list", "create", "info", "update"], help="操作")
    nb_parser.add_argument("name", nargs="?", help="笔记本名称")
    nb_parser.add_argument("--title", help="笔记本标题")
    nb_parser.add_argument("--desc", help="笔记本描述")
    
    # notes 命令
    notes_parser = subparsers.add_parser("notes", help="笔记操作")
    notes_parser.add_argument("action", choices=["list", "add", "get", "edit", "delete", "search"], help="操作")
    notes_parser.add_argument("id", nargs="?", type=int, help="笔记 ID")
    notes_parser.add_argument("--content", "-c", help="笔记内容")
    notes_parser.add_argument("--priority", "-p", choices=["P0", "P1", "P2"], help="优先级")
    notes_parser.add_argument("--tags", "-t", help="标签 (逗号分隔)")
    notes_parser.add_argument("--pinned", action="store_true", help="置顶")
    notes_parser.add_argument("--author", "-a", choices=["A", "M"], help="作者")
    notes_parser.add_argument("--due", "-d", help="截止日期 (YYYY-MM-DD)")
    notes_parser.add_argument("--status", "-s", choices=["todo", "done", "archived"], help="状态")
    notes_parser.add_argument("--keyword", "-k", help="搜索关键词")
    notes_parser.add_argument("--tag", help="按标签搜索")
    
    # quick 命令
    quick_parser = subparsers.add_parser("quick", help="快速操作")
    quick_parser.add_argument("action", choices=["pin", "priority", "status", "due"], help="操作")
    quick_parser.add_argument("id", type=int, help="笔记 ID")
    quick_parser.add_argument("value", nargs="?", help="值")
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        sys.exit(0)
    
    if args.command == "config":
        cmd_config(args)
    elif args.command == "notebooks":
        cmd_notebooks(args)
    elif args.command == "notes":
        cmd_notes(args)
    elif args.command == "quick":
        cmd_quick(args)


if __name__ == "__main__":
    main()
