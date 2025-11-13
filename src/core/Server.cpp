#include "core/Server.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <iostream>

Server::Server(Reactor& r, uint16_t port, bool useET, ThreadPool* pool)
    : reactor_(r), pool_(pool), port_(port), useET_(useET) {}

Server::~Server() { stop(); }

bool Server::start() {
    //exchange-> return running_(old vale, also say if your running_ == true return true)
    if (running_.exchange(true)) return true;

    listenfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd_ < 0) { perror("socket"); return false; }

    int opt = 1;
    //SO_REUSEADDR = 允许快速重启服务器，不等 TIME_WAIT。
    ::setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    //SO_REUSEPORT = 高性能负载均衡，多个线程监听同一个端口。
    ::setsockopt(listenfd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    if (!setNonBlock(listenfd_)) {
        std::cerr << "setNonBlock(listenfd) failed\n";
        ::close(listenfd_);
        listenfd_ = -1;
        return false;
    }

    sockaddr_in addr{}; 
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(listenfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        ::close(listenfd_);
        listenfd_ = -1;
        return false;
    }

    if (::listen(listenfd_, 128) < 0) {
        perror("listen");
        ::close(listenfd_);
        listenfd_ = -1;
        return false;
    }

    // 绑定分发回调（建议只设置一次）
    reactor_.setDispatcher([this](int fd, uint32_t events, void* user) {
        this->onEvent(fd, events, user);
    });

    if (!reactor_.addFd(listenfd_, EPOLLIN, this)) {
        std::cerr << "reactor.addFd(listenfd) failed\n";
        ::close(listenfd_);
        listenfd_ = -1;
        return false;
    }

    std::cout << "Server listening on port " << port_ 
              << " (ET=" << (useET_ ? "on" : "off") << ")\n";
    return true;
}

void Server::stop() {
    if (!running_.exchange(false)) return;

    if (listenfd_ != -1) {
        reactor_.delFd(listenfd_);
        ::close(listenfd_);
        listenfd_ = -1;
    }

    std::lock_guard<std::mutex> lk(conns_mtx_);
    for (auto& kv : conns_) {
        int fd = kv.first;
        reactor_.delFd(fd);
        ::close(fd);
    }
    conns_.clear();
}

void Server::onEvent(int fd, uint32_t events, void* user) {
    // 错误/挂起优先处理
    if (events & (EPOLLERR | EPOLLHUP)) {
        closeConn(fd);
        return;
    }

    if (fd == listenfd_) {
        if (events & EPOLLIN) onAccept();
        return;
    }

    // 普通连接
    std::unique_ptr<Connection>* p_holder = nullptr;
    {
        std::lock_guard<std::mutex> lk(conns_mtx_);
        auto it = conns_.find(fd);
        if (it == conns_.end()) return; // 可能已被关闭
        p_holder = &it->second;
    }
    Connection& c = *p_holder->get();

    if (events & EPOLLIN)  onConnRead(c);
    if (events & EPOLLOUT) onConnWrite(c);
}

void Server::onAccept() {
    for (;;) {
        sockaddr_in cli{}; socklen_t len = sizeof(cli);
        int cfd = ::accept(listenfd_, reinterpret_cast<sockaddr*>(&cli), &len);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 全部接完
            perror("accept");
            break;
        }
        if (!setNonBlock(cfd)) {
            ::close(cfd);
            continue;
        }
        setTcpNoDelay(cfd);

        auto conn = std::make_unique<Connection>();
        conn->fd = cfd;

        {
            std::lock_guard<std::mutex> lk(conns_mtx_);
            conns_.emplace(cfd, std::move(conn));
        }

        // 注册读事件（写事件按需开启）
        // 注意：用户指针传 Connection*，方便派发时取到
        Connection* user_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lk(conns_mtx_);
            user_ptr = conns_[cfd].get();
        }
        if (!reactor_.addFd(cfd, EPOLLIN, user_ptr)) {
            std::lock_guard<std::mutex> lk(conns_mtx_);
            conns_.erase(cfd);
            ::close(cfd);
            continue;
        }
    }
}

void Server::onConnRead(Connection& c) {
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(c.fd, buf, sizeof(buf));
        if (n > 0) {
            c.inbuf.append(buf, n);
            continue;
        }
        if (n == 0) { // 对端关闭
            closeConn(c.fd);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 读尽
        perror("read");
        closeConn(c.fd);
        return;
    }

    // 行协议：按 '\n' 拆包，剥掉末尾 '\r'
    size_t pos = 0;
    for (;;) {
        auto nl = c.inbuf.find('\n', pos);
        if (nl == std::string::npos) {
            // 把未处理的前缀保留
            c.inbuf.erase(0, pos);
            break;
        }
        std::string line = c.inbuf.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = nl + 1;

        // 同步处理（示例：echo）。如需异步，改成提交到线程池：
        if (pool_) {
            int fd = c.fd;
            std::string msg = std::move(line);
            // 假设 ThreadPool 有 submit(fn)
            // pool_->submit([this, fd, m = std::move(msg)]() mutable {
            //     auto out = processLine(m);
            //     postWrite(fd, std::move(out));
            // });
            // 这里先直接同步，免得你没接好线程池接口：
            auto out = processLine(msg);
            postWrite(fd, std::move(out));
        } else {
            auto out = processLine(line);
            postWrite(c.fd, std::move(out));
        }
    }
}

void Server::onConnWrite(Connection& c) {
    for (;;) {
        if (c.outbuf.empty()) break;
        ssize_t n = ::write(c.fd, c.outbuf.data(), c.outbuf.size());
        if (n > 0) {
            c.outbuf.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 还能写下次再来
        perror("write");
        closeConn(c.fd);
        return;
    }

    if (c.outbuf.empty() && c.wantWrite) {
        c.wantWrite = false;
        // 只保留读事件（user 传 nullptr 表示不改）
        reactor_.modFd(c.fd, EPOLLIN, nullptr);
    }
}

void Server::closeConn(int fd) {
    {
        std::lock_guard<std::mutex> lk(conns_mtx_);
        auto it = conns_.find(fd);
        if (it == conns_.end()) return;
    }
    reactor_.delFd(fd);
    ::close(fd);
    {
        std::lock_guard<std::mutex> lk(conns_mtx_);
        conns_.erase(fd);
    }
}

std::string Server::processLine(const std::string& line) {
    // 你可以在这里做业务处理；演示：简单 echo
    if (line == "quit" || line == "exit") {
        return std::string("bye\n");
    }
    return std::string("echo: ") + line + "\n";
}

void Server::postWrite(int fd, std::string data) {
    std::unique_lock<std::mutex> lk(conns_mtx_);
    auto it = conns_.find(fd);
    if (it == conns_.end()) return; // 连接已关
    Connection& c = *it->second;

    c.outbuf.append(std::move(data));
    if (!c.wantWrite) {
        c.wantWrite = true;
        // 开启写事件；跨线程调用时先唤醒，再修改更稳妥
        reactor_.wakeup();
        reactor_.modFd(fd, EPOLLIN | EPOLLOUT, &c);
    }
    // 锁在作用域末尾释放
}

bool Server::setNonBlock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl == -1) return false;
    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) == -1) return false;
    int fdflags = fcntl(fd, F_GETFD, 0);
    if (fdflags != -1) fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC);
    return true;
}

bool Server::setTcpNoDelay(int fd) {
    int one = 1;
    return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0;
}
