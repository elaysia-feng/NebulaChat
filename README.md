<div align="center">

# 🚀 NebulaChat

**NebulaChat** 是一个基于 **C++17 + Reactor + ThreadPool + MySQL + Redis** 的高性能聊天服务器 Demo。  
专注于 **现代 C++ 后端开发实践**，覆盖从网络 IO 到认证、缓存、限流的一整套链路。

</div>

---

## ✨ 特性总览

- 🔁 **Reactor 事件驱动模型**
  - 基于 `epoll` + `eventfd`，支持 ET/非阻塞 IO
  - 自定义 `Reactor` + `Server` 抽象，方便扩展

- 🧵 **线程池 + 安全任务队列**
  - `ThreadPool` + `SafeQueue` 实现生产者/消费者模型
  - 业务处理与网络 IO 解耦，提升吞吐量

- 🧠 **多级缓存认证系统**
  - 用户名密码登录：**Redis 缓存 + 空对象防穿透**
  - 手机号登录：**本地 LRU 小缓存 + Redis + MySQL 多级缓存**
  - Redis 宕机时：**降级 + 简单 QPS 限流保护 MySQL**

- 📲 **短信验证码登录 / 注册 / 找回密码**
  - `SmsService` 使用 Redis 存储验证码（带 TTL）
  - 支持：
    - 手机 + 短信登录
    - 手机 + 短信注册
    - 手机 + 短信重置密码

- 👤 **用户账号体系（AuthService）**
  - 用户名 + 密码登录
  - 手机号 + 短信登录
  - 注册（phone + username + password）
  - 修改昵称 `update_name`
  - 忘记密码 `reset_pass`

- 📦 **连接池 & 组件化**
  - `DBPool`：MySQL 连接池
  - `RedisPool`：Redis 连接池（基于 hiredis）
  - `Random`：线程安全随机工具

- 📡 **JSON 文本协议（nlohmann/json）**
  - 所有请求/响应均为一行一条 JSON
  - 易于前端/脚本客户端对接

- 📜 **轻量日志系统 Logger**
  - 支持 `DEBUG/INFO/WARN/ERROR`
  - 统一前缀，便于排查问题

---

## 🧱 项目结构

```bash
NebulaChat/
├── include/
│   ├── core/                  # 核心框架
│   │   ├── Reactor.h          # epoll + eventfd Reactor
│   │   ├── Server.h           # TCP 监听 + 连接管理
│   │   ├── ThreadPool.h       # 线程池
│   │   ├── SafeQueue.h        # 线程安全队列
│   │   └── Logger.h           # 日志工具
│   │
│   ├── db/                    # 数据库 & 缓存
│   │   ├── DBconnection.h     # MySQL 连接封装
│   │   ├── DBpool.h           # MySQL 连接池
│   │   ├── RedisConnection.h  # Redis 连接封装（hiredis）
│   │   └── RedisPool.h        # Redis 连接池 + 降级标记
│   │
│   ├── chat/                  # 业务逻辑层
│   │   ├── MessageHandler.h   # 解析 JSON、路由 cmd
│   │   ├── AuthService.h      # 登录/注册/改名/重置密码
│   │   └── SmsService.h       # 短信验证码逻辑
│   │
│   └── utils/                 
│       ├── Random.h           # RandInt 等工具
│       └── UserCacheVal.h     # 本地 LRU + TTL 缓存 & QPS 限流
│
├── src/
│   ├── core/                  # core 对应实现
│   ├── db/                    # db 对应实现
│   ├── chat/                  # chat 对应实现
│   └── main.cpp               # 程序入口（初始化 + 启动）
│
├── scripts/
│   └── test_client.py         # Python 测试/压测客户端
│
├── config/                    # 预留配置目录（如 YAML/JSON）
│
├── CMakeLists.txt
└── README.md
