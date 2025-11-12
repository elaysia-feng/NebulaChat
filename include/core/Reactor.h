#pragma once
#include <sys/epoll.h>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>

class Reactor
{


public:
    //“事件分发函数类型”。
    using DispatchFn = std::function<void(int fd, uint32_t events, void* user)>;
    explicit Reactor(int maxEvents = 1024, bool useET = true);
    ~Reactor();

    bool addFd(int fd, uint32_t events, void* user);
    bool modFd(int fd, uint32_t events, void* user);
    bool delFd(int fd);

    void setDispatcher(DispatchFn d);   // 仅设置一次
    void loop();
    void stop();

    // 跨线程唤醒
    int  wakeupFd() const;              // eventfd
    void wakeup(); // 向 eventfd 写入
                         
private:
    int epfd_{-1};
    int evfd_{-1}; //eventfriend
    std::vector<epoll_event> evlist_;
    std::atomic<bool> running_{false};
    bool useET_{true};
    DispatchFn dispatcher_;
    std::mutex users_mtx_;
    std::unordered_map<int, void* > users_;
};
