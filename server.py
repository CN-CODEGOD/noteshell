#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NoteShell 同步服务器
--------------------
作用：
  1) 托管 noteWeb.html（线上可访问的网页）
  2) 提供 /api/notes  —— 网页读取 noteshell 的笔记列表（真源：~/.noteshell_history）
  3) 提供 /api/push   —— noteshell 新增/删除笔记后，通过 curl POST 推送，触发广播
  4) 提供 /api/seq    —— 当前同步序号（网页用于判断是否有更新）
  5) 提供 /api/events —— SSE 长连接，笔记变化时实时推给所有打开的网页

数据源就是 noteshell 自己的 ~/.noteshell_history 文件，保证两边永远一致。
仅依赖 Python 标准库，无需安装任何第三方包。
"""

import json
import os
import queue
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.environ.get("NOTESHELL_HOST", "0.0.0.0")
PORT = int(os.environ.get("NOTESHELL_PORT", "8001"))


def history_path():
    home = os.environ.get("HOME", os.path.expanduser("~"))
    return os.path.join(home, ".noteshell_history")


def read_notes():
    """读取 ~/.noteshell_history，返回 [(timestamp, content), ...]。"""
    path = history_path()
    notes = []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")
    except FileNotFoundError:
        return notes, 0.0
    i = 0
    while i + 1 < len(lines):
        ts = lines[i].strip()
        content = lines[i + 1].rstrip("\r")
        if ts == "" and content == "":
            i += 2
            continue
        notes.append((ts, content))
        i += 2
    mtime = 0.0
    try:
        mtime = os.path.getmtime(path)
    except OSError:
        pass
    return notes, mtime


# ---------- 简易 SSE 广播器 ----------
class Broadcaster:
    def __init__(self):
        self._lock = threading.Lock()
        self._clients = []          # list of (queue, )
        self.seq = 0

    def add(self):
        q = queue.Queue()
        with self._lock:
            self._clients.append(q)
        return q

    def remove(self, q):
        with self._lock:
            if q in self._clients:
                self._clients.remove(q)

    def broadcast(self, payload):
        data = "data: " + json.dumps(payload, ensure_ascii=False) + "\n\n"
        with self._lock:
            clients = list(self._clients)
        for q in clients:
            try:
                q.put_nowait(data)
            except Exception:
                pass


BROADCASTER = Broadcaster()


class Handler(BaseHTTPRequestHandler):
    server_version = "NoteShellSync/1.0"

    # ---- 工具方法 ----
    def _send(self, code, body, ctype="text/plain; charset=utf-8", extra=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        if extra:
            for k, v in extra.items():
                self.send_header(k, v)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj, ensure_ascii=False),
                   "application/json; charset=utf-8")

    def log_message(self, fmt, *args):
        ts = time.strftime("%H:%M:%S")
        print(f"[{ts}] {self.address_string()} {fmt % args}", flush=True)

    # ---- 路由 ----
    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html", "/noteWeb.html"):
            self._serve_html()
        elif path in ("/notes", "/note", "/note.html"):
            # 专门的 note 页面：同步显示 noteshell 的消息
            self._serve_file("note.html", "text/html; charset=utf-8")
        elif path == "/api/notes":
            notes, mtime = read_notes()
            self._json({
                "count": len(notes),
                "mtime": mtime,
                "seq": BROADCASTER.seq,
                "notes": [{"ts": t, "content": c} for t, c in notes],
            })
        elif path == "/api/seq":
            self._json({"seq": BROADCASTER.seq})
        elif path == "/api/events":
            self._serve_events()
        else:
            self._send(404, "404 Not Found\n")

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path == "/api/push":
            length = int(self.headers.get("Content-Length", "0") or 0)
            raw = self.rfile.read(length) if length else b""
            try:
                text = raw.decode("utf-8", errors="replace")
            except Exception:
                text = ""
            BROADCASTER.seq += 1
            notes, _ = read_notes()

            # noteshell 推送格式: seq \t count \t timestamp \t content
            parts = text.split("\t", 3)
            note = None
            if len(parts) == 4:
                note = {"ts": parts[2], "content": parts[3]}
            elif len(parts) >= 1 and text:
                # 纯文本推送，也当成一条消息
                note = {"ts": "", "content": text}

            payload = {
                "type": "notes",
                "seq": BROADCASTER.seq,
                "count": len(notes),
                "note": note,
            }
            BROADCASTER.broadcast(payload)
            self._json({"ok": True, "seq": BROADCASTER.seq, "count": len(notes)})
        else:
            self._send(404, "404 Not Found\n")

    # ---- 静态 / 页面 ----
    def _serve_file(self, name, ctype):
        full = os.path.join(HERE, name)
        if not os.path.isfile(full):
            self._send(404, f"{name} not found\n")
            return
        with open(full, "rb") as f:
            data = f.read()
        self._send(200, data, ctype)

    def _serve_html(self):
        self._serve_file("noteWeb.html", "text/html; charset=utf-8")

    # ---- SSE ----
    def _serve_events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

        q = BROADCASTER.add()
        try:
            # 首帧：告诉客户端当前状态
            notes, _ = read_notes()
            hello = {"type": "hello", "seq": BROADCASTER.seq, "count": len(notes)}
            self.wfile.write(("data: " + json.dumps(hello, ensure_ascii=False) +
                              "\n\n").encode("utf-8"))
            self.wfile.flush()
            last_ping = time.time()
            while True:
                try:
                    item = q.get(timeout=1.0)
                    self.wfile.write(item.encode("utf-8"))
                    self.wfile.flush()
                except Exception:
                    # 超时：发心跳保活
                    if time.time() - last_ping > 15:
                        self.wfile.write(b": ping\n\n")
                        self.wfile.flush()
                        last_ping = time.time()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            BROADCASTER.remove(q)

    # 长连接需要 HTTP/1.1 keep-alive
    protocol_version = "HTTP/1.1"


class Server(ThreadingHTTPServer):
    # ThreadingHTTPServer 本身即 ThreadingMixIn + HTTPServer，已支持多线程
    daemon_threads = True
    allow_reuse_address = True


def main():
    print(f"NoteShell sync server")
    print(f"  history : {history_path()}")
    print(f"  listening: http://{HOST}:{PORT}/")
    print(f"  notes   : http://{HOST}:{PORT}/notes")
    print("按 Ctrl+C 退出")
    httpd = Server((HOST, PORT), Handler)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
