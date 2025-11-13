#include "core/Server.h"
#include "core/ThreadPool.h"

#include <iostream>
#include <csignal>

static reactor* g_reactor = nullptr;
static Server*  g_server  = nullptr;

void handleSigint(int) {
    std::cout << "\n[Ctrl-C] stopping server...\n";
    if (g_server)  g_server->stop();
    if (g_reactor) g_reactor->stop();
}

int main() {
    // ① 创建 Reactor（事件循环）
    reactor reactor(1024, true);
    g_reactor = &reactor;

    // ② 创建线程池（Server 的任务异步执行）
    ThreadPool pool(4, 1024);
    pool.run();

    // ③ 创建 Server（内部会 setDispatcher）
    Server server(reactor, 8888, true, &pool);
    g_server = &server;

    // ④ 启动 Server（会创建 listenfd 并 addFd 到 Reactor）
    if (!server.start()) {
        std::cerr << "Server start failed\n";
        return 1;
    }

    // ⑤ 注册 Ctrl+C
    std::signal(SIGINT, handleSigint);

    // ⑥ 开始事件循环（阻塞）
    std::cout << "Server is running on port 8888\n";
    reactor.loop();

    return 0;
}
