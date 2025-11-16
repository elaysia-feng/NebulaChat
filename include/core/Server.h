#pragma once
#include "reactor.h"
#include "ThreadPool.h"
#include "chat/MessageHandler.h"
#include <unordered_map>
#include <string>
struct Connection
{
    /*为每个连接创建 Session（会话状态）*/
    int fd{-1};
    std::string inbuf;
    std::string outbuf;

    // I/O 状态
    std::atomic<bool> wantWrite{false};
    std::atomic<bool> shortClose{false};

    // Session 状态
    //标记这个连接的用户是否“已经登录成功”
    bool authed{false};     // 是否已登录
    int userId{0};          // 用户ID
    std::string name;            // 用户名
    int roomId{0};          // 所在聊天室
};

class Server
{
private:
/*onEvent 的功能就是：

判断这个事件是不是 listenfd

如果是 listenfd 且可读 → 说明有客户端连接 → 交给 onAccept()

如果是普通 fd → 按 events 类型分别进入：

EPOLLIN → onConnRead()

EPOLLOUT → onConnWrite()

EPOLLERR / EPOLLHUP → closeConn()

它是事件类型 → 函数选择器。*/
    void onEvent(int fd, uint32_t events, void* user);
    //处理新连接到来的事件，并把连接纳入 Server 管理。
    void onAccept();
    //从客户端读取数据、解析数据、交给业务层处理。
    void onConnRead(Connection& conn);
    //把 outbuf 里的数据在循环内尽量 write 完
    void onConnWrite(Connection& conn);
    void closeConn(int fd);

    /*这部分是业务逻辑。

    真实应用里，这里可以是：

    登录逻辑

    注册逻辑

    JSON 解析

    协议解析

    数据库查询

    游戏逻辑

    聊天室广播 etc.*/
    //这个函数的逻辑移动到了Messagehandler
    // std::string processLine(Connection& c, const std::string& line);

    //这是业务线程安全投递“要写的数据”的入口，用状态机和 EPOLLOUT 驱动真正的写回
    void postWrite(int fd, std::string data);

    //tool
    bool setNonBlock(int fd);
    bool setTcpNoDelay(int fd);

private:
    reactor& reactor_;
    ThreadPool* Threadpool_;
    int listenFd_{-1};
    uint16_t port_{0};
    bool useET_{true};

    std::unordered_map<int, std::unique_ptr<Connection>> conns_;
    std::mutex conns_mtx_; // 保护 conns_ 以及 Connection.outbuf 等跨线程访问
    std::atomic<bool> running_{false};
    MessageHandler msgHandler_;   //  新增：业务处理器

public:
    Server(reactor& rect, uint16_t port, bool useET = true, ThreadPool* pool = nullptr);
    ~Server();

    bool start();   // 创建监听并注册到 Reactor
    void stop();  // 停止监听并关闭所有连接
};

