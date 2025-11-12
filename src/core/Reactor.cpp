#include "core/Reactor.h"
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <errno.h>
#include <sys/eventfd.h>

using namespace std;

namespace{
// SetNonBlock：把 fd 设为非阻塞 + close-on-exec
  // 目的：配合 epoll 的 ET/LT 写法，read()/write() 不会把线程阻塞住

  
    static bool SetNonBlock(int fd){
        int flflags = fcntl(fd, F_GETFL, 0);
        if(flflags == -1) return false;
        if(fcntl(fd, F_GETFL, flflags || O_NONBLOCK) == -1) return false;
        int fdflags = fcntl(fd, F_GETFD, 0);
        if (fdflags != -1) fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC);
        return true;
    }
  // DrainEventfd：把 eventfd 的计数“读空”
  // 目的：如果不读空，eventfd 会持续保持可读，epoll_wait 会在下一轮立刻返回——主循环就会空转吃满 CPU。
    static void DrainEventfd(int evfd) {
    uint64_t cnt;
    for (;;) {
        ssize_t n = ::read(evfd, &cnt, sizeof(cnt));
        if (n == sizeof(cnt)) continue; // 可能还有残留计数，继续读
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n <= 0) break; // EOF 或其他错误就退出
    }
}
}
Reactor::Reactor(int maxEvents, bool useET)
    : epfd_(-1),
      evfd_(-1),
      evlist_(maxEvents),
      running_(false),
      useET_(useET) {

    epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd_ == -1) {
        perror("epoll_create1");
        throw std::runtime_error("epoll_create1 failed");
    }

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evfd_ == -1) {
        perror("eventfd");
        ::close(epfd_);
        throw std::runtime_error("eventfd failed");
    }

    // 把 eventfd 纳入 epoll，作为跨线程唤醒/提交修改的触发源
    epoll_event ev{};
    ev.events = EPOLLIN;           // 不需要 ET
    ev.data.fd = evfd_;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev) == -1) {
        perror("epoll_ctl ADD evfd");
        ::close(evfd_);
        ::close(epfd_);
        throw std::runtime_error("epoll_ctl add eventfd failed");
    }
}
Reactor::~Reactor(){
    stop();
    if(evfd_ != -1) ::close(evfd_);
    if(epfd_ != -1) ::close(epfd_);
}


bool Reactor::addFd(int fd, uint32_t events, void* user){
    if(fd < 0) return false;
    epoll_event ev{};
    ev.events = events | (useET_ ? EPOLLET : 0);
    ev.data.fd = fd;
     if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl ADD");
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(users_mtx_);
        users_[fd] = user;
    }
    // 建议外部自行保证 fd 已非阻塞
    return true;

}

bool Reactor::modFd(int fd, uint32_t events, void* user) {
    if (fd < 0) return false;
    epoll_event ev{};
    ev.events = events | (useET_ ? EPOLLET : 0);
    ev.data.fd = fd;
    if (::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        perror("epoll_ctl MOD");
        return false;
    }
    if (user) { // 仅当传入非空时更新 user
        std::lock_guard<std::mutex> lk(users_mtx_);
        users_[fd] = user;
    }
    return true;
}

bool Reactor::delFd(int fd) {
    if (fd < 0) return false;
    epoll_event ev{};
    if (::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, &ev) == -1) {
        // 若已被对端关闭，DEL 失败不致命
        if (errno != EBADF && errno != ENOENT) {
            perror("epoll_ctl DEL");
            return false;
        }
    }
    std::lock_guard<std::mutex> lk(users_mtx_);
    users_.erase(fd);
    return true;
}

void Reactor::setDispatcher(DispatchFn d) {
    dispatcher_ = std::move(d);
}

int Reactor::wakeupFd() const {
    return evfd_;
}

void Reactor::wakeup() {
    uint64_t one = 1;
    // 在高并发下，EAGAIN 极少出现（计数溢出），忽略即可
    ssize_t n = ::write(evfd_, &one, sizeof(one));
    (void)n;
}

void Reactor::loop(){
     if (!dispatcher_) {
        std::cerr << "Reactor: dispatcher not set\n";
        return;
    }
    running_.store(true, std::memory_order_release);
while (running_.load(std::memory_order_acquire)) {
        int n = ::epoll_wait(epfd_, evlist_.data(),
                             static_cast<int>(evlist_.size()), -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = evlist_[i].data.fd;
            uint32_t ev = evlist_[i].events;

            if (fd == evfd_) {
                // 消耗唤醒信号
                DrainEventfd(evfd_);
                continue;
            }

            void* user = nullptr;
            {
                std::lock_guard<std::mutex> lk(users_mtx_);
                auto it = users_.find(fd);
                if (it != users_.end()) user = it->second;
            }

            // 交给上层派发（Server::Dispatch）
            dispatcher_(fd, ev, user);
        }
    }
}

void Reactor::stop(){
    bool expected = true;
    if(running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)){
        wakeup();// 唤醒 epoll_wait 退出
    }
}