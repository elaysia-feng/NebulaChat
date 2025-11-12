// include/Server.h
#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include "Reactor.h"
#include "ThreadPool.h"

struct Connection {
    int fd{-1};
    std::string inbuf;
    std::string outbuf;
    bool wantWrite{false};
};

class Server {
public:
    Server(Reactor& r, ThreadPool& p, uint16_t port, bool useET = true);
    ~Server();

    bool start();
    void stop();

private:
    // 统一派发入口（供 Reactor 调用）
    static void Dispatch(int fd, uint32_t events, void* user);
    void onEvent(int fd, uint32_t events, void* user);

    // 具体处理
    void onAccept();
    void onConnRead(Connection& c);
    void onConnWrite(Connection& c);
    void closeConn(int fd);

    // 业务处理与回写
    void handleLineAsync(int fd, std::string line);
    void postWrite(int fd, std::string data); // 线程安全

    // 工具
    bool setNonBlock(int fd);
    bool setTcpNoDelay(int fd);

private:
    Reactor& reactor_;
    ThreadPool& pool_;
    int listenfd_{-1};
    uint16_t port_{0};
    bool useET_{true};

    std::unordered_map<int, std::unique_ptr<Connection>> conns_;
    std::mutex conns_mtx_; // 仅在跨线程访问 outbuf/状态时持有
};
