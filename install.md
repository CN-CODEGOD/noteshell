# NoteShell 安装指南





> 面向 AI Agent 的完整安装与配置说明

## 首先克隆仓库



git clone https://github.com/CN-CODEGOD/noteshell.git

## 确认安装scope

```
Scope	Path	Visible to
Per-agent	<workspace>/skills	Only that agent
Project-agent	<workspace>/.agents/skills	Only that workspace's agent
Personal-agent	~/.agents/skills	Agents using the default state
Shared managed	<state-dir>/skills	All agents using that state
Workshop	<state-dir>/agents/<agentId>/agent/workshop-skills	Only that agent
Extra dirs	skills.load.extraDirs	All agents using that config

```



## 需要提供服务器来连接server 和网页

“请你提供服务器的地址给我，我才可以继续设置，如果是在本地直接搭建，需要继续搭建在本地，请确保本地是一个服务器，我才可以在本地搭建如果你希望在远程服务器上搭建。”


具体的搭建过程：
---
## 服务端

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





---


## 客户端



### build
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
