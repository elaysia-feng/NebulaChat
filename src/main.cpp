#include "core/Server.h"
#include "core/Reactor.h"
#include <iostream>

int main() {

    // 1. 创建 Reactor（事件循环）
    Reactor reactor;

    // 2. 创建 Server（监听 socket）
    Server server(8888);

    // 3. 给 Reactor 安装回调函数（dispatcher）
    reactor.setDispatcher(
        [&](int fd, uint32_t events) {

            // case 1: listenfd 有新连接
            if (fd == server.listenfd()) {
                int connfd = server.acceptClient();
                if (connfd != -1) {
                    reactor.addReadEvent(connfd);   // 让 Reactor 监控这个 fd
                    std::cout << "New Client: " << connfd << std::endl;
                }
                return;
            }

            // case 2: 普通连接可读
            if (events & EPOLLIN) {
                char buf[1024];
                int n = ::read(fd, buf, sizeof(buf));
                if (n <= 0) {
                    std::cout << "Client " << fd << " disconnected\n";
                    reactor.removeEvent(fd);
                    ::close(fd);
                } else {
                    buf[n] = '\0';
                    std::cout << "Recv("<<fd<<"): " << buf << std::endl;
                    ::write(fd, buf, n); // echo 回去
                }
            }
        }
    );

    // 4. Server 创建监听 socket 并加入 Reactor
    if (!server.start()) {
        std::cerr << "server.start failed\n";
        return 1;
    }

    reactor.addReadEvent(server.listenfd());

    // 5. 启动 Reactor 循环（阻塞）
    std::cout << "Server run on 8888..." << std::endl;
    reactor.loop();

    return 0;
}
