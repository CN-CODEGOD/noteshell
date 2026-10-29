#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NoteShell 服务器 v2
-------------------
- 笔记本数据存储在 ~/.noteshell_data/ 目录下
- _manifest.json: 笔记本注册表（name, title, desc, vim_mode, created）
- <notebook>.json: 每个笔记本的笔记数据
- 笔记支持扩展字段：priority, tags, pinned, author, updated, status
- 提供 REST API 供客户端和网页使用
- SSE 实时推送笔记变更
- 仅依赖 Python 标准库
"""

import json
import os
import queue
import re
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse, unquote

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.environ.get("NOTESHELL_HOST", "0.0.0.0")
PORT = int(os.environ.get("NOTESHELL_PORT", "8001"))

DATA_DIR = os.path.join(os.environ.get("HOME", os.path.expanduser("~")), ".noteshell_data")
MANIFEST_FILE = "_manifest.json"


# ---------- 存储层 ----------

def ensure_data_dir():
    os.makedirs(DATA_DIR, exist_ok=True)
    manifest = os.path.join(DATA_DIR, MANIFEST_FILE)
    if not os.path.exists(manifest):
        _write_json(manifest, {
            "notebooks": [
                {
                    "name": "default",
                    "title": "默认笔记本",
                    "desc": "",
                    "vim_mode": False,
                    "created": _now_str()
                }
            ]
        })
    nb_path = os.path.join(DATA_DIR, "default.json")
    if not os.path.exists(nb_path):
        _write_json(nb_path, {"notes": [], "next_id": 1})


def _now_str():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def _read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return None


def _write_json(path, data):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.flush()
        os.fsync(f.fileno())
    os.rename(tmp, path)


def read_manifest():
    ensure_data_dir()
    path = os.path.join(DATA_DIR, MANIFEST_FILE)
    data = _read_json(path)
    if data is None:
        data = {"notebooks": []}
        _write_json(path, data)
    return data


def write_manifest(data):
    path = os.path.join(DATA_DIR, MANIFEST_FILE)
    _write_json(path, data)


def notebook_path(name):
    safe = re.sub(r'[^\w\-]', '_', name)
    return os.path.join(DATA_DIR, safe + ".json")


def read_notebook(name):
    path = notebook_path(name)
    data = _read_json(path)
    if data is None:
        return None
    return data


def write_notebook(name, data):
    path = notebook_path(name)
    _write_json(path, data)


def get_notebook_names():
    manifest = read_manifest()
    return [nb["name"] for nb in manifest.get("notebooks", [])]


def get_notebook_info(name):
    manifest = read_manifest()
    for nb in manifest.get("notebooks", []):
        if nb["name"] == name:
            return nb
    return None


def create_notebook(name, title="", desc=""):
    manifest = read_manifest()
    for nb in manifest.get("notebooks", []):
        if nb["name"] == name:
            return False
    manifest["notebooks"].append({
        "name": name,
        "title": title or name,
        "desc": desc,
        "vim_mode": False,
        "created": _now_str()
    })
    write_manifest(manifest)
    write_notebook(name, {"notes": [], "next_id": 1})
    return True


def update_notebook(name, **kwargs):
    manifest = read_manifest()
    for nb in manifest.get("notebooks", []):
        if nb["name"] == name:
            for key, value in kwargs.items():
                if key in ("title", "desc", "vim_mode"):
                    nb[key] = value
            write_manifest(manifest)
            return True
    return False


# ---------- SSE 广播器 ----------

class Broadcaster:
    def __init__(self):
        self._lock = threading.Lock()
        self._clients = []
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

_file_locks = {}
_file_locks_lock = threading.Lock()


def _get_file_lock(name):
    with _file_locks_lock:
        if name not in _file_locks:
            _file_locks[name] = threading.Lock()
        return _file_locks[name]


# ---------- HTTP 处理器 ----------

class Handler(BaseHTTPRequestHandler):
    server_version = "NoteShellServer/2.0"
    protocol_version = "HTTP/1.1"

    def _send(self, code, body, ctype="text/plain; charset=utf-8"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj, ensure_ascii=False),
                   "application/json; charset=utf-8")

    def _read_body(self):
        length = int(self.headers.get("Content-Length", "0") or 0)
        raw = self.rfile.read(length) if length else b""
        try:
            return json.loads(raw.decode("utf-8")) if raw else {}
        except json.JSONDecodeError:
            return {}

    def log_message(self, fmt, *args):
        ts = time.strftime("%H:%M:%S")
        print(f"[{ts}] {self.address_string()} {fmt % args}", flush=True)

    def _route(self, method):
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        query = parse_qs(parsed.query)

        if method == "GET":
            if path in ("/", "/index.html", "/noteWeb.html"):
                return self._serve_file("noteWeb.html", "text/html; charset=utf-8")
            if path in ("/notes", "/note", "/note.html"):
                return self._serve_file("note.html", "text/html; charset=utf-8")

        # 笔记本列表
        if path == "/api/notebooks":
            if method == "GET":
                return self._api_list_notebooks()
            if method == "POST":
                return self._api_create_notebook()

        # 单个笔记本
        m = re.match(r'^/api/notebook/([^/]+)$', path)
        if m:
            nb_name = unquote(m.group(1))
            if method == "GET":
                return self._api_get_notebook(nb_name)
            if method == "PUT":
                return self._api_update_notebook(nb_name)

        # 笔记操作
        m = re.match(r'^/api/notebook/([^/]+)/notes?$', path)
        if m:
            nb_name = unquote(m.group(1))
            if method == "POST":
                return self._api_add_note(nb_name)

        m = re.match(r'^/api/notebook/([^/]+)/note/(\d+)$', path)
        if m:
            nb_name = unquote(m.group(1))
            note_id = int(m.group(2))
            if method == "GET":
                return self._api_get_note(nb_name, note_id)
            if method == "PUT":
                return self._api_edit_note(nb_name, note_id)
            if method == "DELETE":
                return self._api_delete_note(nb_name, note_id)

        # 搜索
        m = re.match(r'^/api/notebook/([^/]+)/search$', path)
        if m:
            nb_name = unquote(m.group(1))
            keyword = query.get("q", [""])[0]
            tag = query.get("tag", [None])[0]
            if method == "GET":
                return self._api_search_notes(nb_name, keyword, tag)

        # 设置
        m = re.match(r'^/api/notebook/([^/]+)/settings$', path)
        if m:
            nb_name = unquote(m.group(1))
            if method == "GET":
                return self._api_get_settings(nb_name)
            if method == "PUT":
                return self._api_update_settings(nb_name)

        # 兼容旧 API
        if path == "/api/seq" and method == "GET":
            return self._json({"seq": BROADCASTER.seq})
        if path == "/api/notes" and method == "GET":
            return self._api_get_notebook("default")
        if path == "/api/push" and method == "POST":
            return self._api_compat_push()
        if path == "/api/events" and method == "GET":
            nb_filter = query.get("notebook", [None])[0]
            return self._serve_events(nb_filter)

        self._send(404, "404 Not Found\n")

    def do_GET(self):
        self._route("GET")

    def do_POST(self):
        self._route("POST")

    def do_PUT(self):
        self._route("PUT")

    def do_DELETE(self):
        self._route("DELETE")

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Content-Length", "0")
        self.end_headers()

    # ---------- API 实现 ----------

    def _api_list_notebooks(self):
        manifest = read_manifest()
        result = []
        for nb in manifest.get("notebooks", []):
            data = read_notebook(nb["name"])
            count = len(data["notes"]) if data else 0
            result.append({
                "name": nb["name"],
                "title": nb.get("title", nb["name"]),
                "desc": nb.get("desc", ""),
                "vim_mode": nb.get("vim_mode", False),
                "created": nb.get("created", ""),
                "count": count
            })
        self._json({"notebooks": result})

    def _api_create_notebook(self):
        body = self._read_body()
        name = body.get("name", "").strip()
        title = body.get("title", "").strip()
        desc = body.get("desc", "").strip()
        if not name:
            return self._json({"error": "name is required"}, 400)
        if not re.match(r'^[\w\-]+$', name):
            return self._json({"error": "invalid name"}, 400)
        if create_notebook(name, title, desc):
            self._json({"ok": True, "name": name, "title": title or name})
        else:
            self._json({"error": "notebook already exists"}, 409)

    def _api_get_notebook(self, name):
        info = get_notebook_info(name)
        if info is None:
            return self._json({"error": "notebook not found"}, 404)
        data = read_notebook(name)
        if data is None:
            data = {"notes": [], "next_id": 1}
        
        # 排序：置顶优先，然后按更新时间
        notes = data["notes"]
        notes_sorted = sorted(notes, key=lambda n: (
            not n.get("pinned", False),
            n.get("updated", n.get("ts", "")),
            n.get("id", 0)
        ))
        
        self._json({
            "name": name,
            "title": info.get("title", name),
            "desc": info.get("desc", ""),
            "vim_mode": info.get("vim_mode", False),
            "count": len(notes),
            "notes": notes_sorted
        })

    def _api_update_notebook(self, name):
        info = get_notebook_info(name)
        if info is None:
            return self._json({"error": "notebook not found"}, 404)
        body = self._read_body()
        kwargs = {}
        if "title" in body:
            kwargs["title"] = body["title"]
        if "desc" in body:
            kwargs["desc"] = body["desc"]
        if "vim_mode" in body:
            kwargs["vim_mode"] = bool(body["vim_mode"])
        if update_notebook(name, **kwargs):
            self._json({"ok": True})
        else:
            self._json({"error": "update failed"}, 500)

    def _api_add_note(self, name):
        info = get_notebook_info(name)
        if info is None:
            return self._json({"error": "notebook not found"}, 404)
        body = self._read_body()
        content = body.get("content", "")
        if not content.strip():
            return self._json({"error": "content is required"}, 400)

        lock = _get_file_lock(name)
        with lock:
            data = read_notebook(name)
            if data is None:
                data = {"notes": [], "next_id": 1}
            ts = _now_str()
            note_id = data["next_id"]
            data["next_id"] = note_id + 1
            
            note = {
                "id": note_id,
                "ts": ts,
                "content": content,
                "priority": body.get("priority", "P2"),
                "tags": body.get("tags", []),
                "pinned": body.get("pinned", False),
                "author": body.get("author", "A"),
                "updated": ts,
                "updated_by": body.get("author", "A"),
                "status": "todo",
                "due": body.get("due", "")
            }
            data["notes"].append(note)
            write_notebook(name, data)

        BROADCASTER.seq += 1
        BROADCASTER.broadcast({
            "type": "update",
            "notebook": name,
            "action": "add",
            "seq": BROADCASTER.seq,
            "note": note
        })
        self._json({"ok": True, "id": note_id, "ts": ts})

    def _api_get_note(self, name, note_id):
        info = get_notebook_info(name)
        if info is None:
            return self._json({"error": "notebook not found"}, 404)
        data = read_notebook(name)
        if data is None:
            return self._json({"error": "note not found"}, 404)
        for note in data["notes"]:
            if note["id"] == note_id:
                return self._json(note)
        return self._json({"error": "note not found"}, 404)

    def _api_edit_note(self, name, note_id):
        info = get_notebook_info(name)
        if info is None:
            return self._json({"error": "notebook not found"}, 404)
        body = self._read_body()

        lock = _get_file_lock(name)
        with lock:
            data = read_notebook(name)
            if data is None:
                return self._json({"error": "notebook not found"}, 404)
            for note in data["notes"]:
                if note["id"] == note_id:
                    if "content" in body:
                        note["content"] = body["content"]
                    if "priority" in body:
                        note["priority"] = body["priority"]
                    if "tags" in body:
                        note["tags"] = body["tags"]
                    if "pinned" in body:
                        note["pinned"] = bool(body["pinned"])
                    if "status" in body:
                        note["status"] = body["status"]
                        if body["status"] == "done":
                            note["completed"] = _now_str()
                            note["completed_by"] = body.get("author", "A")
                        elif body["status"] == "todo":
                            note.pop("completed", None)
                            note.pop("completed_by", None)
                    if "author" in body:
                        note["author"] = body["author"]
                    if "due" in body:
                        note["due"] = body["due"]
                    
                    note["updated"] = _now_str()
                    note["updated_by"] = body.get("author", note.get("author", "A"))
                    
                    write_notebook(name, data)
                    BROADCASTER.seq += 1
                    BROADCASTER.broadcast({
                        "type": "update",
                        "notebook": name,
                        "action": "edit",
                        "seq": BROADCASTER.seq,
                        "note": note
                    })
                    return self._json({"ok": True})
            return self._json({"error": "note not found"}, 404)

    def _api_delete_note(self, name, note_id):
        info = get_notebook_info(name)
        if info is None:
            return self._json({"error": "notebook not found"}, 404)

        lock = _get_file_lock(name)
        with lock:
            data = read_notebook(name)
            if data is None:
                return self._json({"error": "notebook not found"}, 404)
            original_count = len(data["notes"])
            data["notes"] = [n for n in data["notes"] if n["id"] != note_id]
            if len(data["notes"]) == original_count:
                return self._json({"error": "note not found"}, 404)
            write_notebook(name, data)

        BROADCASTER.seq += 1
        BROADCASTER.broadcast({
            "type": "update",
            "notebook": name,
            "action": "delete",
            "seq": BROADCASTER.seq,
            "note_id": note_id
        })
        self._json({"ok": True})

    def _api_search_notes(self, name, keyword, tag=None):
        info = get_notebook_info(name)
        if info is None:
            return self._json({"error": "notebook not found"}, 404)
        data = read_notebook(name)
        if data is None:
            return self._json({"name": name, "count": 0, "notes": []})
        
        results = []
        for n in data["notes"]:
            match = True
            if keyword and keyword not in n.get("content", ""):
                match = False
            if tag and tag not in n.get("tags", []):
                match = False
            if match:
                results.append(n)
        
        self._json({"name": name, "count": len(results), "notes": results})

    def _api_get_settings(self, name):
        info = get_notebook_info(name)
        if info is None:
            return self._json({"error": "notebook not found"}, 404)
        self._json({
            "name": name,
            "title": info.get("title", name),
            "desc": info.get("desc", ""),
            "vim_mode": info.get("vim_mode", False)
        })

    def _api_update_settings(self, name):
        info = get_notebook_info(name)
        if info is None:
            return self._json({"error": "notebook not found"}, 404)
        body = self._read_body()
        kwargs = {}
        if "vim_mode" in body:
            kwargs["vim_mode"] = bool(body["vim_mode"])
        if "title" in body:
            kwargs["title"] = body["title"]
        if "desc" in body:
            kwargs["desc"] = body["desc"]
        if update_notebook(name, **kwargs):
            self._json({"ok": True})
        else:
            self._json({"error": "update failed"}, 500)

    def _api_compat_push(self):
        body = self._read_body()
        raw_text = body.get("content", "")
        if not raw_text:
            raw = self.rfile.read(int(self.headers.get("Content-Length", "0") or 0))
            raw_text = raw.decode("utf-8", errors="replace")

        parts = raw_text.split("\t", 3)
        content = parts[3] if len(parts) == 4 else raw_text

        lock = _get_file_lock("default")
        with lock:
            data = read_notebook("default")
            if data is None:
                data = {"notes": [], "next_id": 1}
            note_id = data["next_id"]
            data["next_id"] = note_id + 1
            ts = _now_str()
            note = {
                "id": note_id, "ts": ts, "content": content,
                "priority": "P2", "tags": [], "pinned": False,
                "author": "A", "updated": ts, "updated_by": "A", "status": "todo"
            }
            data["notes"].append(note)
            write_notebook("default", data)

        BROADCASTER.seq += 1
        BROADCASTER.broadcast({
            "type": "update",
            "notebook": "default",
            "action": "add",
            "seq": BROADCASTER.seq,
            "note": note
        })
        self._json({"ok": True, "seq": BROADCASTER.seq})

    def _serve_file(self, name, ctype):
        full = os.path.join(HERE, name)
        if not os.path.isfile(full):
            self._send(404, f"{name} not found\n")
            return
        with open(full, "rb") as f:
            data = f.read()
        self._send(200, data, ctype)

    def _serve_events(self, notebook_filter=None):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

        q = BROADCASTER.add()
        try:
            hello = {"type": "hello", "seq": BROADCASTER.seq}
            self.wfile.write(("data: " + json.dumps(hello) + "\n\n").encode("utf-8"))
            self.wfile.flush()
            last_ping = time.time()
            while True:
                try:
                    item = q.get(timeout=1.0)
                    if notebook_filter:
                        try:
                            payload = json.loads(item.split("data: ", 1)[1].rsplit("\n\n", 1)[0])
                            if payload.get("notebook") != notebook_filter:
                                continue
                        except Exception:
                            pass
                    self.wfile.write(item.encode("utf-8"))
                    self.wfile.flush()
                except Exception:
                    if time.time() - last_ping > 15:
                        self.wfile.write(b": ping\n\n")
                        self.wfile.flush()
                        last_ping = time.time()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            BROADCASTER.remove(q)


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    ensure_data_dir()
    print(f"NoteShell server v2")
    print(f"  data dir : {DATA_DIR}")
    print(f"  listening: http://{HOST}:{PORT}/")
    print(f"  notes    : http://{HOST}:{PORT}/notes")
    print("Press Ctrl+C to exit")
    httpd = Server((HOST, PORT), Handler)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
