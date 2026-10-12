# NoteShell 同步服务器 —— 精简镜像
# 纯 Python 标准库，无第三方依赖，因此用 alpine 也能跑，这里用 slim 兼顾兼容性
FROM python:3.12-slim

# 避免 .pyc、开启无缓冲输出（方便 docker logs 实时看到）
ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    NOTESHELL_HOST=0.0.0.0 \
    NOTESHELL_PORT=8001

# 工作目录：存放 server.py 与两个网页
WORKDIR /app

# 只复制运行所需文件（源码 + 静态页）
COPY server.py note.html noteWeb.html ./

# 数据目录：笔记真源 ~/.noteshell_history 会挂到这里
# 容器内 HOME=/data，于是文件落在 /data/.noteshell_history
ENV HOME=/data
RUN mkdir -p /data
VOLUME ["/data"]

EXPOSE 8001

# 健康检查：/api/seq 能返回 JSON 即视为健康
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
  CMD python3 -c "import urllib.request,sys; \
    sys.exit(0 if urllib.request.urlopen('http://127.0.0.1:8001/api/seq', timeout=4).status==200 else 1)"

CMD ["python3", "server.py"]
