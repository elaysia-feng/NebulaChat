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
#include <nlohmann/json.hpp>

using json = nlohmann::json;


Server::Server(reactor& rect, uint16_t port, bool useET, ThreadPool* pool)
    : reactor_(rect), Threadpool_(pool), port_(port), useET_(useET) {}

Server::~Server(){stop();}

bool Server::start(){
    //这个exchange就是去给running赋值的，然后返回的是running没赋值之前的
    bool expected = false;
    if(!running_.compare_exchange_strong(expected, true)){
        // 已经在运行了，直接返回 true
        return true;
    }
    bool ok = false; // 用于统一收尾
    do{
        listenFd_ = socket(AF_INET, SOCK_STREAM, 0);

        if(listenFd_ < 0) { perror("socket"); break; }
        int opt = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #ifdef SO_REUSEPORT
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    #endif
        if (!setNonBlock(listenFd_)) {
            std::cerr << "setNonBlock(listenfd) failed\n";
            ::close(listenFd_);
            listenFd_ = -1;
            // 失败走统一收尾逻辑
            break;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

       /*static_cast = 合法、安全的转换
        → “本来就能这样做，编译器帮你检查,
        但是得有对应类型间的关系，sockaddr_in* -> sockaddr*,明显没有这个关系”

        reinterpret_cast = 野蛮、按字节解释的转换
        → “我知道风险，我硬要把 A 当 B 用”*/
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("bind");
            break;
        }
        //n -> 允许同时连接的客户端数量
        if (::listen(listenFd_, 128) < 0) {
            perror("listen");
            break;
        }

        // 把监听 fd 加入 epoll，才能收到连接事件
        if (!reactor_.addFd(listenFd_, EPOLLIN, nullptr)) {
            std::cerr << "reactor add listenFd_ failed\n";
            break;
        }
        
        // 绑定分发回调（建议只设置一次）
        reactor_.setDispatcher([this](int fd, uint32_t events, void* user){
            this->onEvent(fd, events, user);
        });

        std::cout << "Server listening on port " << port_
                  << " (ET=" << (useET_ ? "on" : "off") << ")\n";
        ok = true;
    }while (false);

    if(!ok){
        if(listenFd_ != -1){
            reactor_.delFd(listenFd_);
            ::close(listenFd_);
            listenFd_ = -1;
        }
        running_.store(false); // 失败要把 running_ 还原
    }
    return ok;
}



void Server::stop(){
    // 如果之前已经是 false，说明本来就没在跑，直接返回
    if(!running_.exchange(false)) return;

    if (listenFd_ != -1){
        reactor_.delFd(listenFd_);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    //conns_ 是多线程共享容器，必须加锁，否则会在 stop() 清理时被其他线程同时修改导致崩溃。
    std::lock_guard<std::mutex> lock(conns_mtx_);
    for(auto& kv : conns_){
        int fd = kv.first;
        reactor_.delFd(fd);
        ::close(fd);
    }
    conns_.clear();
}
/*处理监听事件，有端口想要访问就加入reactor管理,反之就是有加入的客户端想要完成写或者读*/
void Server::onEvent(int fd, uint32_t events, void* user){
    // 错误/挂起优先处理
    if(events & (EPOLLERR | EPOLLHUP)){
        closeConn(fd);
        return;
    }

    if(fd == listenFd_){
        /*如果事件events 位与(全1为1)上,
        用位与来判断events里面是否有这个事件，
        如果没有这个事件，
        我的events & “事件”就是为0不会执行这个语句了*/
        if(events & EPOLLIN) onAccept();
        return;
    }

    // 普通连接
    /*ptr是指向这个独立智能指针对象的指针，
    这个unique_ptr是一个类对象，
    比如: auto uPtr = make_unique<Connection> (10);
    uPtr->fd = 10;//这个只是实现得像指针但是uPtr实际上还是只是个对象，
    只是封装这个unique_ptr的人想把他变得跟指针一样的用法才这样写的
    */
    std::unique_ptr<Connection>* p_holder = nullptr;
    {
        std::lock_guard<std::mutex> lock(conns_mtx_);
        auto it = conns_.find(fd);
        if(it == conns_.end()) return;
        p_holder = &it->second;
    }

    Connection& conn = *p_holder->get();

    if(events & EPOLLIN) onConnRead(conn);
    if(events & EPOLLOUT) onConnWrite(conn);
}
/*处理新的连接事件，加入我的reactor管理*/
void Server::onAccept() {
    for(;;){
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int clientfd = accept(listenFd_, reinterpret_cast<sockaddr*> (&client_addr), &len);
        if(clientfd < 0){
            //客户端全部被我的加完了，已经没有客户端去加了
            if(errno == EAGAIN || errno == EWOULDBLOCK) break;// 全部接完
            perror("accpet");
            break;
        }

        if(!setNonBlock(clientfd)){
            ::close(clientfd);
            continue;
        }

        setTcpNoDelay(clientfd);

        auto conn = std::make_unique<Connection>();
        conn->fd = clientfd;
        // 现在先默认都登录
        conn->authed = true;
        conn->userId = 0;           // 暂时不用
        conn->name.clear();         // 暂时为空
        conn->roomId = 0;           // 暂时不分房间
        Connection* user_ptr = conn.get();

        {
            std::lock_guard<std::mutex> lock(conns_mtx_);
            conns_.emplace(clientfd, std::move(conn));
        }

        if(!reactor_.addFd(clientfd, EPOLLIN, user_ptr)) {
            std::lock_guard<std::mutex> lock(conns_mtx_);
            conns_.erase(clientfd);
            ::close(clientfd);
            continue;
        }
    }
}


/*读客户端发送的东西，解析并回复*/
void Server::onConnRead(Connection& conn){
    char buff[1024];
    while(true){
        ssize_t n = ::read(conn.fd, buff, sizeof(buff));
        if(n > 0){
            conn.inbuf.append(buff, n);
            continue;
        }
        if(n == 0){
            // 对端关闭
            closeConn(conn.fd);
            return;
        }
        if(errno == EAGAIN || errno == EWOULDBLOCK) break; // 读尽

        perror("read");
        closeConn(conn.fd);
        return; // 读出错直接结束，不再解析 inbuf
    }

    // 行协议：按 '\n' 拆包，剥掉末尾 '\r'
    size_t pos = 0;
    for(;;){
        auto nlocation = conn.inbuf.find('\n', pos);
        //npos->not position,没找到子串/字符
        if (nlocation == std::string::npos) {
            // 不完整，保留尚未处理的
            conn.inbuf.erase(0, pos);
            break;
        }

        std::string line = conn.inbuf.substr(pos, nlocation - pos);
        if(!line.empty() && line.back() == '\r') line.pop_back();
        pos = nlocation + 1;

    /*现在这个版本加入了线程池*/
    Threadpool_->Enqueue([this, fd = conn.fd, line](){
        Connection* c = nullptr;
        {
            std::lock_guard<std::mutex> lock(conns_mtx_);
            auto it = conns_.find(fd);
            if(it == conns_.end()) return; //表示连接关闭
            c = it->second.get();
        }

        // 业务处理（耗时部分）
        std::cout << "[" << fd << "]" << "-> " << line << std::endl;
        std::string out = msgHandler_.handleMessage(*c, line);
        json resp = json::parse(out);
        
        bool isClose = resp.value("close", false);
        
        // 写回事件一定要在 Server 线程安全里做
        postWrite(fd, std::move(out));
        if(isClose){
            std::lock_guard<std::mutex> lock(conns_mtx_);
            auto it = conns_.find(fd);
            if(it != conns_.end()){
                it->second->shortClose.store(true);
            }
        }

    });

    }

}

/*把想回复给客户端的信息写进缓存区*/
void Server::onConnWrite(Connection& c){
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
    if(c.outbuf.empty() && c.shortClose){
        closeConn(c.fd);
    }

    if (c.outbuf.empty() && c.wantWrite) {
        c.wantWrite.store(false);
        // 只保留读事件（user 传 nullptr 表示不改）
        reactor_.modFd(c.fd, EPOLLIN, nullptr);
    }
}

/*关闭客户端的连接*/
void Server::closeConn(int fd){
    {
        std::lock_guard<std::mutex> lock(conns_mtx_);
        auto it = conns_.find(fd);
        if(it == conns_.end()) return;
    }
    reactor_.delFd(fd);
    ::close(fd);
    {
        std::lock_guard<std::mutex> lock(conns_mtx_);
        conns_.erase(fd);
    }
}

/*把服务端想写的通过多线程先放在Connect里的outbuf*/
void Server::postWrite(int fd, std::string data){
    std::unique_lock<std::mutex> lk(conns_mtx_);
    auto it = conns_.find(fd);
    if (it == conns_.end()) return; // 连接已关
    Connection& c = *it->second;

    c.outbuf.append(data);
    if(!c.wantWrite){
        c.wantWrite.store(true);
        // 正确顺序：先改 epoll 事件，再唤醒 reactor
        reactor_.modFd(fd, EPOLLIN | EPOLLOUT, &c);
        reactor_.wakeup();
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

/*设置端口多路复用*/
bool Server::setTcpNoDelay(int fd){
    int one = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0;
}
