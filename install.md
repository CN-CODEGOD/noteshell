# NoteShell 安装指南

> 面向 AI Agent 的完整安装与配置说明

---

## 快速开始（3 步）

```bash
# 1. 启动服务器
docker run -d -p 8080:8080 -v noteshell_data:/data dockercncodegod/noteshell-server

# 2. 配置客户端
python3 noteshell-cli.py config --host localhost --port 8080

# 3. 验证
curl http://localhost:8080/api/notebooks
```

---

## 安装方式

### 方式一：Docker（推荐）

#### 服务器

```bash
# 拉取镜像
docker pull dockercncodegod/noteshell-server

# 运行容器
docker run -d \
  --name noteshell \
  -p 8080:8080 \
  -v noteshell_data:/data \
  dockercncodegod/noteshell-server
```

**环境变量：**
- `NOTESHELL_PORT`：服务器端口（默认 8080）
- `NOTESHELL_DATA_DIR`：数据目录（默认 /data）

#### Docker Compose

```yaml
version: '3.8'
services:
  noteshell:
    image: dockercncodegod/noteshell-server
    ports:
      - "8080:8080"
    volumes:
      - noteshell_data:/data
    environment:
      - NOTESHELL_PORT=8080
    restart: unless-stopped

volumes:
  noteshell_data:
```

```bash
docker-compose up -d
```

---

### 方式二：源码编译

#### 服务器

```bash
# 无需编译，直接运行
python3 server.py
```

#### TUI 客户端（C 程序）

```bash
# 编译
gcc -o noteshell noteshell.c -Wall -O2

# 运行
./noteshell --host localhost --port 8080
```

---

### 方式三：纯 Python（无编译）

#### 服务器

```bash
python3 server.py
```

#### CLI 客户端

```bash
python3 noteshell-cli.py
```

---

## 客户端安装

### TUI 客户端（noteshell）

```bash
# 编译
gcc -o noteshell noteshell.c -Wall -O2

# 安装到系统路径（可选）
sudo cp noteshell /usr/local/bin/
```

**使用：**
```bash
noteshell --host localhost --port 8080
```

### CLI 客户端（noteshell-cli.py）

```bash
# 直接使用
python3 noteshell-cli.py

# 或创建别名
alias noteshell-cli='python3 /path/to/noteshell-cli.py'
```

---

## 配置

### 服务器配置

**环境变量：**
```bash
export NOTESHELL_PORT=8080        # 服务器端口
export NOTESHELL_DATA_DIR=/data   # 数据存储目录
```

### CLI 客户端配置

```bash
# 首次配置
python3 noteshell-cli.py config --host localhost --port 8080

# 配置文件位置
~/.noteshell_config
```

**环境变量（覆盖配置文件）：**
```bash
export NOTESHELL_HOST=localhost
export NOTESHELL_PORT=8080
```

### 数据存储

**服务器数据结构：**
```
/data/
  ├── notebook1.json
  ├── notebook2.json
  └── ...
```

每个笔记本是一个独立的 JSON 文件，包含笔记列表和元数据。

---

## 验证安装

### 1. 检查服务器状态

```bash
curl http://localhost:8080/api/notebooks
```

**预期响应：**
```json
{
  "notebooks": []
}
```

### 2. 创建测试笔记本

```bash
# 使用 CLI
python3 noteshell-cli.py notebooks create test_notebook --title "测试笔记本"

# 或直接 API
curl -X POST http://localhost:8080/api/notebooks \
  -H "Content-Type: application/json" \
  -d '{"name": "test_notebook", "title": "测试笔记本"}'
```

### 3. 添加测试笔记

```bash
# 使用 CLI
python3 noteshell-cli.py notes add test_notebook --content "测试笔记" --priority P1

# 或直接 API
curl -X POST http://localhost:8080/api/test_notebook/notes \
  -H "Content-Type: application/json" \
  -d '{"content": "测试笔记", "priority": "P1"}'
```

### 4. 访问 Web 界面

浏览器打开：`http://localhost:8080/`

应该能看到：
- 左侧边栏显示 "测试笔记本"
- 右侧显示刚创建的笔记

---

## 完整测试脚本

```bash
#!/bin/bash
set -e

echo "=== NoteShell 安装验证 ==="

# 1. 检查服务器
echo "1. 检查服务器..."
curl -s http://localhost:8080/api/notebooks > /dev/null && echo "✓ 服务器正常"

# 2. 创建笔记本
echo "2. 创建笔记本..."
curl -s -X POST http://localhost:8080/api/notebooks \
  -H "Content-Type: application/json" \
  -d '{"name": "verify_test", "title": "验证测试"}' > /dev/null
echo "✓ 笔记本已创建"

# 3. 添加笔记
echo "3. 添加笔记..."
curl -s -X POST http://localhost:8080/api/verify_test/notes \
  -H "Content-Type: application/json" \
  -d '{"content": "# 测试\n- [ ] 待办项", "priority": "P1", "tags": ["test"]}' > /dev/null
echo "✓ 笔记已添加"

# 4. 查询笔记
echo "4. 查询笔记..."
NOTES=$(curl -s http://localhost:8080/api/verify_test/notes)
echo "$NOTES" | grep -q "测试" && echo "✓ 笔记查询成功"

# 5. 清理
echo "5. 清理测试数据..."
curl -s -X DELETE http://localhost:8080/api/notebooks/verify_test > /dev/null
echo "✓ 清理完成"

echo ""
echo "=== 所有测试通过！==="
```

保存为 `verify.sh` 并运行：
```bash
chmod +x verify.sh
./verify.sh
```

---

## 故障排除

### 1. 端口被占用

**错误：** `Address already in use`

**解决：**
```bash
# 更换端口
export NOTESHELL_PORT=8081
python3 server.py

# 或查找占用进程
lsof -i :8080
```

### 2. 权限不足

**错误：** `Permission denied`

**解决：**
```bash
# 确保数据目录可写
chmod 755 /data

# 或使用其他目录
export NOTESHELL_DATA_DIR=/path/to/writable/dir
```

### 3. Docker 容器无法启动

**检查：**
```bash
# 查看日志
docker logs noteshell

# 检查容器状态
docker ps -a
```

**常见问题：**
- 端口冲突：更换映射端口 `-p 8081:8080`
- 数据卷权限：确保 `/data` 可写

### 4. CLI 无法连接服务器

**检查：**
```bash
# 测试连接
curl http://localhost:8080/api/notebooks

# 检查配置
cat ~/.noteshell_config
```

**解决：**
```bash
# 重新配置
python3 noteshell-cli.py config --host localhost --port 8080
```

### 5. TUI 客户端中文显示乱码

**确保终端支持 UTF-8：**
```bash
# 检查 locale
locale

# 设置为 UTF-8
export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8
```

---

## 卸载

### Docker 卸载

```bash
# 停止并删除容器
docker stop noteshell
docker rm noteshell

# 删除数据卷（可选）
docker volume rm noteshell_data

# 删除镜像
docker rmi dockercncodegod/noteshell-server
```

### 源码卸载

```bash
# 删除编译的二进制文件
rm -f noteshell

# 删除服务器文件
rm -rf /path/to/noteshell

# 删除 CLI 配置（可选）
rm -f ~/.noteshell_config
```

### 清理数据

```bash
# 删除所有数据
rm -rf /data/noteshell_*
# 或
rm -rf $NOTESHELL_DATA_DIR/*
```

---

## 下一步

安装完成后，参考以下文档：

- **[skill.md](skill.md)**：完整功能使用指南
- **[README.md](README.md)**：项目概述和快速入门

---

## 支持

- **GitHub Issues**：https://github.com/CN-CODEGOD/noteshell/issues
- **Docker Hub**：https://hub.docker.com/r/dockercncodegod/noteshell-server
