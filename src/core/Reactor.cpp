#include "core/reactor.h"
#include "core/Logger.h"   // 新增：日志头文件
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <errno.h>
#include <sys/eventfd.h>
#include <stdexcept>
#include <cstring> 
using namespace std;

namespace{
  // DrainEventfd：把 eventfd 的计数“读空”
  // 目的：如果不读空，eventfd 会持续保持可读，epoll_wait 会在下一轮立刻返回——主循环就会空转吃满 CPU。
    
  /*这里的reactor由于用不到信号量模式，
  因为我们只要唤醒了epoll一次就能把事情全部处理完了，
  直接read就行，不用循环读，循环读是在信号量里面用到的，
  因为信号量模式里面我的counter每次读只会减1，所以要循环读，
  为什么信号量模式里面我们赌读一次才减1？
  想象 1 个生产者线程，5 个消费者线程。

  生产者做了 10 个任务：

  write(fd, 1) * 10  → counter = 10
  消费者线程们轮流：
  read(fd) → 拿到 1 个任务 → counter -= 1
  read(fd) → 拿到 1 个任务 → counter -= 1
  read(fd) → …

  最终：
  10 次 read 对应 10 个任务
  每个 worker 拿 1 次
  直到 counter = 0
  你看到问题了吗？
  如果用普通模式呢？
  ❌ 普通模式：read 一次会拿走全部 10
  这意味着：
  一个线程读 eventfd → 一次性拿走全部任务
  剩下的消费者全被饿死（没任务了）
  多线程同步完全失败
  所以普通模式根本不能用于线程同步。
  而信号量模式可以：
  多个线程不需要锁
  谁先抢到 read 就先拿走 1 个任务
  counter -= 1
  counter==0 时不再成功 read（任务耗尽）
  这就是 信号量（semaphore） 的核心用途。*/
  static void DrainEvent(int evfd) {
      uint64_t counter;
      // 读一次就够 – 多次 write 会累加到 counter 里
      ssize_t n = ::read(evfd, &counter, sizeof(counter));
      (void)n;
      LOG_DEBUG("[Reactor::DrainEvent] drained eventfd=" << evfd
                << " counter=" << counter);
  }
}

reactor::reactor(int MaxEvent, bool useET)
    : epfd_(-1),
      evfd_(-1),
      eventList_(MaxEvent),
      //因为 刚构造 Reactor 对象的时候，它还没开始事件循环啊！
      running_(false),
      useET(useET)
{   
    LOG_INFO("[Reactor::ctor] create reactor, MaxEvent="
             << MaxEvent << " useET=" << (useET ? "true" : "false"));

    epfd_ = epoll_create1(EPOLL_CLOEXEC);

    if (epfd_ == -1) {
        perror("epoll_create1");
        LOG_ERROR("[Reactor::ctor] epoll_create1 failed: "
                  << strerror(errno));
        throw std::runtime_error("epoll_create1 failed");
    }

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evfd_ == -1) {
        perror("eventfd");
        LOG_ERROR("[Reactor::ctor] eventfd create failed: "
                  << strerror(errno));
        ::close(epfd_);
        throw std::runtime_error("eventfd failed");
    }

    // 把 eventfd 纳入 epoll，作为跨线程唤醒/提交修改的触发源
    epoll_event ev{};
    ev.data.fd = evfd_;
    ev.events  = EPOLLIN;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev) == -1) {
        perror("epoll_ctl ADD evfd");
        LOG_ERROR("[Reactor::ctor] epoll_ctl add evfd failed: "
                  << strerror(errno));
        ::close(evfd_);
        ::close(epfd_);
        throw std::runtime_error("epoll_ctl add eventfd failed");
    }

    LOG_INFO("[Reactor::ctor] reactor init OK, epfd=" << epfd_
             << " evfd=" << evfd_);
}

reactor::~reactor(){
    LOG_INFO("[Reactor::dtor] destroy reactor");
    stop();
    if(evfd_ != -1) {
        ::close(evfd_);
        LOG_INFO("[Reactor::dtor] evfd_ closed: " << evfd_);
        evfd_ = -1;
    }
    if(epfd_ != -1) {
        ::close(epfd_);
        LOG_INFO("[Reactor::dtor] epfd_ closed: " << epfd_);
        epfd_ = -1;
    }
}

bool reactor::addFd(int fd, uint32_t events, void* user){
    if(fd < 0) return  false;
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events  = events | (useET ? EPOLLET : 0);
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl ADD");
        LOG_ERROR("[Reactor::addFd] epoll_ctl ADD fd=" << fd
                  << " failed: " << strerror(errno));
        return false;
    }

    {
        std::lock_guard<mutex> lock(user_mtx_);
        users_[fd] = user;
    }

    LOG_DEBUG("[Reactor::addFd] fd=" << fd
              << " events=0x" << std::hex << events << std::dec
              << " useET=" << (useET ? "true" : "false"));

    // 建议外部自行保证 fd 已非阻塞
    return true;
}


bool reactor::modFd(int fd, uint32_t events, void* user){
    if(fd < 0) return  false;
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events  = events | (useET ? EPOLLET : 0);
    if (::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        perror("epoll_ctl MOD");
        LOG_ERROR("[Reactor::modFd] epoll_ctl MOD fd=" << fd
                  << " failed: " << strerror(errno));
        return false;
    }

    if(user){
        std::lock_guard<mutex> lock(user_mtx_);
        users_[fd] = user;
    }

    LOG_DEBUG("[Reactor::modFd] fd=" << fd
              << " events=0x" << std::hex << events << std::dec
              << " useET=" << (useET ? "true" : "false"));

    // 建议外部自行保证 fd 已非阻塞
    return true;
}

bool reactor::delFd(int fd){
    if(fd < 0) return false;
    epoll_event ev{};
    if(::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, &ev) == -1){
        // 若已被对端关闭，DEL 失败不致命, ebadf, enoent
        if (errno != EBADF && errno != ENOENT) {
            perror("epoll_ctl DEL");
            LOG_ERROR("[Reactor::delFd] epoll_ctl DEL fd=" << fd
                      << " failed: " << strerror(errno));
            return false;
        } else {
            LOG_DEBUG("[Reactor::delFd] epoll_ctl DEL fd=" << fd
                      << " ignored errno=" << errno);
        }
    }
    {
        std::lock_guard<mutex> lock(user_mtx_);
        users_.erase(fd);
    }

    LOG_INFO("[Reactor::delFd] fd=" << fd << " removed from epoll and users_");
    return true;
}

void reactor::wakeup(){
    uint64_t one = 1;
    // 在高并发下，EAGAIN 极少出现（计数溢出），忽略即可
    //写入唤醒epoll_wait();
    ssize_t n = ::write(evfd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        LOG_ERROR("[Reactor::wakeup] write to evfd_ failed, n=" << n
                  << " errno=" << errno << " (" << strerror(errno) << ")");
    } else {
        LOG_DEBUG("[Reactor::wakeup] wakeup sent to evfd_=" << evfd_);
    }
    // (void)n 的真正作用：消除未使用变量警告
    (void) n;
}

void reactor::loop(){
    if (!dispatcher_) {
        LOG_ERROR("Reactor: dispatcher not set");
        return;
    }
    LOG_INFO("[Reactor::loop] event loop start");

    running_.store(true, std::memory_order_release);
    //这是为了让其它线程修改 running_ 时，
    // Reactor.loop() 能立刻退出，并保证跨线程内存可见性。
    while(running_.load(std::memory_order_acquire)){
        //static_cast 是 C++ 中 最常用、最安全、最应该使用的显式类型转换运算符。
        //这个eventList_是用来接受epoll看到哪里io变化的，
        // 比如我的epoll看到有2个io变化了，
        // n = 2,
        // 例eventList[0].data.fd = 6; evenrList_[1].data.fd = 19; 
        int n = ::epoll_wait(epfd_, eventList_.data(), static_cast<int>(eventList_.size()), -1);
        if (n < 0) {
            if (errno == EINTR) {
                LOG_DEBUG("[Reactor::loop] epoll_wait interrupted by signal, retry");
                continue;
            }
            perror("epoll_wait");
            LOG_ERROR("[Reactor::loop] epoll_wait error: "
                      << strerror(errno));
            break;
        }

        if (n == 0) {
            // 理论上 timeout=-1 不会到这里，这里只是保险
            LOG_DEBUG("[Reactor::loop] epoll_wait returns 0 (no events)");
            continue;
        }

        LOG_DEBUG("[Reactor::loop] epoll_wait returns n=" << n << " events");
        
        for(int i = 0; i < n; ++i){
            int fd = eventList_[i].data.fd;
            uint32_t events = eventList_[i].events;

            //这里是因为如果我的Fd == evfd，
            // 说明有其它的io想要唤醒我的epoll_wait来办事,
            // 必须先通过eventfd来中间处理，去唤醒这个epoll,
            // 就像你有需求像领导汇报，
            // 肯定得汇报给某个leader然后他再去汇报
            if(fd == evfd_){
                LOG_DEBUG("[Reactor::loop] got wakeup event on evfd_=" << evfd_
                          << " events=0x" << std::hex << events << std::dec);
                // 消耗唤醒信号
                //因为本生我的eventFd就是用来唤醒epoll这一个作用，
                // 如果这次不读完eventfd里面的计数的话，
                // 我的epoll就会一直醒，
                // 因为它看我的eventFd里面有计数是可读的，
                // 这样会导致cup占用100%
                DrainEvent(evfd_);
                continue;
            }

            void* user = nullptr;
            {
                std::lock_guard<mutex> lock(user_mtx_);
                auto temp = users_.find(fd);
                if (temp != users_.end()) user = temp->second;
            }

            LOG_DEBUG("[Reactor::loop] dispatch fd=" << fd
                      << " events=0x" << std::hex << events << std::dec
                      << " user=" << user);

            // 交给上层派发（Server::Dispatch）
            dispatcher_(fd, events, user);
        }
    }

    LOG_INFO("[Reactor::loop] event loop exit");
}

void reactor::stop(){
    bool expected = true;
    if(running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)){
        LOG_INFO("[Reactor::stop] set running_=false, wakeup loop");
        wakeup();// 唤醒 epoll_wait 退出, stop -> loop(),running_.load(std::memory_order_acquire)
    } else {
        LOG_DEBUG("[Reactor::stop] already stopped (running_ was false)");
    }
}

int reactor::wakeUpFd()const{
    return  evfd_;
}

bool reactor::setDispatcher(std::function<void(int, uint32_t, void*)> cb)
{
    dispatcher_ = std::move(cb);
    if (!dispatcher_) {
        LOG_ERROR("[Reactor::setDispatcher] dispatcher is empty");
        return false;
    }
    LOG_INFO("[Reactor::setDispatcher] dispatcher set OK");
    return true;
}
