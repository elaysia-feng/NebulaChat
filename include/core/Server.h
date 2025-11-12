#pragma once
#include "Reactor.h"
#include <unordered_map>
#include <memory>
#include <string>
#include <mutex>
#include <cstdint>

struct Connection {
    int fd{-1};
    std::string inbuf;
    std::string outbuf;
    bool wantWrite{false};
};

class ThreadPool; // 可选：若没有线程池，可以不包含实现，只用指针

class Server {
public:
    Server(Reactor& r, uint16_t port, bool useET = true, ThreadPool* pool = nullptr);
    ~Server();

    bool start();   // 创建监听并注册到 Reactor
    void stop();    // 停止监听并关闭所有连接

private:
    // Reactor 回调入口（用 lambda 绑定到 this）
    void onEvent(int fd, uint32_t events, void* user);

    // 具体处理
    void onAccept();
    void onConnRead(Connection& c);
    void onConnWrite(Connection& c);
    void closeConn(int fd);

    // 业务处理与回写
    std::string processLine(const std::string& line); // 业务逻辑（示例：echo）
    void postWrite(int fd, std::string data);         // 可被工作线程调用，线程安全

    // 工具
    bool setNonBlock(int fd);
    bool setTcpNoDelay(int fd);

private:
    Reactor& reactor_;
    ThreadPool* pool_;   // 可为空；为空则在 I/O 线程内直接处理
    int listenfd_{-1};
    uint16_t port_{0};
    bool useET_{true};

    std::unordered_map<int, std::unique_ptr<Connection>> conns_;
    std::mutex conns_mtx_; // 保护 conns_ 以及 Connection.outbuf 等跨线程访问
    std::atomic<bool> running_{false};
};
