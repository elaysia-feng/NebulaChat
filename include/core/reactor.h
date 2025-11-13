#pragma once
#include <mutex>
#include <sys/epoll.h>
#include <vector>
#include <atomic>
#include <functional>
#include <unordered_map>
class reactor
{
public:
// 当 epoll_wait 检测到某个文件描述符（socket）发生了事件，
// Reactor 就会调用你设置的 Dispatch Function（事件分发函数） 来处理它。
    using DispatchFunction = std::function<void(int fd, uint32_t events, void* user)>;
private:
    int epfd_{-1};
    int evfd_{-1};
    std::vector<epoll_event> eventList_;
    std::atomic<bool> running_{false};
    bool useET;
    DispatchFunction dispatcher_;
    std::mutex user_mtx_;
    std::unordered_map<int, void*> users_;
public:
    explicit reactor(int MaxEvent, bool useET = true);
    ~reactor();

    bool addFd(int fd, uint32_t events, void* user);
    bool modFd(int fd, uint32_t events, void* user);
    bool delFd(int fd);
    
    bool setDispatcher(DispatchFunction dispatchFunc);

// Reactor 的主循环，程序会一直在里面等待 I/O 事件，并在事件发生时调用你的 DispatchFunction 处理它。
// loop() 不结束 → 服务器就一直在线
// loop() 结束 → 服务器停止运行
    void loop();
    void stop();


// wakeupFd() 只是“取出这个 eventfd 的文件描述符（FD）”。

// 类似意义：

// int fd = obj.wakeupFd();


// 得到一个数字，比如 5 或 7，不做任何操作。

// 它不会唤醒 epoll，只是返回这个数字：

// 方便别人把 eventfd 加入 epoll

// 方便别人知道这个 eventfd 是哪个 fd

// 它本身不产生任何事件，也不会写数据。
    int wakeUpFd() const; //eventfd
// 真正唤醒 epoll 的是：

// wakeup()

// 因为 wakeup() 会向 eventfd 写入 8 字节数据，
// 而 epoll 监视了这个 eventfd，
// 写入事件会立即导致：

// epoll_wait() → 立刻返回

// 这就是“唤醒 epoll”。
    void wakeup(); 
};

