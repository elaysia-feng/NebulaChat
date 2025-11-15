# 🚀 NebulaChat

NebulaChat 是一个基于 **C++17 + Reactor + ThreadPool + MySQL** 的高性能聊天服务器 demo，
采用多 Reactor + 线程池架构，支持多客户端并发、登录注册、echo/upper 等基础指令。

当前版本主要用于 **学习网络后端开发 / 提升 C++ 工程能力**。

---

## ✨ 主要功能

* 🔌 **Reactor（epoll + eventfd）事件驱动模型**
* 🧵 **线程池（异步任务处理）**
* 📦 **TCP 连接管理（非阻塞 I/O + ET 模式）**
* 🧠 **JSON 协议解析（nlohmann/json）**
* 👤 **登录 / 注册（MySQL 支持）**
* 📤 **业务逻辑异步执行（MessageHandler）**
* 🧵 **安全队列 SafeQueue 实现**
* 🗄️ **MySQL 连接池（DBPool）**

---

## 📁 项目结构

```
NebulaChat/
├── include/
│   ├── core/            # Reactor + ThreadPool + Server
│   ├── db/              # DBPool + DBConnection
│   ├── chat/            # MessageHandler / AuthService
│   └── utils/           
│
├── src/
│   ├── core/
│   ├── db/
│   ├── chat/
│   └── main.cpp         # 程序入口
│
├── scripts/
│   └── test_client.py   # Python 压力测试客户端
│
├── config/              # 后续可加入 YAML 配置
│
├── CMakeLists.txt
└── README.md
```

---

## 🔧 构建方式

需要：

* GCC / Clang（支持 C++17）
* CMake >= 3.10
* MySQL Server / libmysqlclient-dev

### ① 安装 MySQL 开发依赖

```bash
sudo apt install libmysqlclient-dev mysql-client
```

### ② 编译项目

```bash
mkdir build
cd build
cmake ..
make -j4
```

编译完成后，生成：

```
./NebulaChat
```

---

## 🚀 运行服务器

```bash
./NebulaChat
```

如果启动成功，你会看到：

```
[Reactor] loop start...
Server is running on port 8888
```

---

## 🛢️ 配置 MySQL（登录 / 注册 必须）

连接 MySQL 后创建：

```sql
CREATE DATABASE serverlogin;

USE serverlogin;

CREATE TABLE users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    username VARCHAR(50) NOT NULL,
    password VARCHAR(50) NOT NULL
);
```

插入示例：

```sql
INSERT INTO users(username, password) VALUES('Elias', '1234');
```

---

## 🧪 测试方式（手动）

### 1）使用 nc（netcat）

```bash
nc 127.0.0.1 8888
```

发送：

```json
{"cmd":"login","user":"Elias","pass":"1234"}
```

或者：

```json
{"cmd":"upper","msg":"hello"}
```

---

## 🧪 Python 自动测试（已提供）

运行：

```bash
python3 scripts/test_client.py
```

支持：

* 自动注册 + 登录
* 手动交互模式
* 多线程压力测试（1k~5k 客户端）
* 自动发送 JSON 消息

示例输入：

```
1) 自动注册 + 登录测试
2) 手动测试
3) 压力测试（多线程）
```

---

## ⚙️ Linux 调优（压测必做）

提升最大文件描述符：

```
/etc/security/limits.conf
/etc/pam.d/common-session
/etc/systemd/system.conf
/etc/systemd/user.conf
```

设置：

```
nofile = 100000
```

确认：

```bash
ulimit -n
```

---

## 🏗️ 下一步计划（Roadmap）

* [ ] 多聊天室 room 功能
* [ ] 群聊 / 私聊 message 分发
* [ ] 完整的 JSON 协议：心跳、消息类型、房间管理
* [ ] 异步日志（Logger）
* [ ] 定时任务（时间轮 TimerWheel）
* [ ] 使用 Protobuf 替换 JSON
* [ ] epoll + multi-reactor + sub-thread 模式

---

## 📚 致谢

此项目用于学习现代 C++ 网络编程与高性能服务器架构，
感谢你坚持到这里，NebulaChat 将持续扩展更多功能。