#include "core/Server.h"
#include "db/RedisPool.h"
#include "core/ThreadPool.h"
#include "db/DBpool.h"
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
    //  初始化 MySQL 连接池
    bool ok = DBPool::Instance().init(
        "127.0.0.1",    // MySQL host
        3306,           // port
        "root",         // user
        "1234",         // password
        "serverlogin",  // database（确保已创建）
        10              // pool size
    );

    if (!ok) {
        std::cerr << "[main] DBPool init FAILED!" << std::endl;
        return -1;
    } 
    std::cout << "[main] DBPool init OK\n";

    
    bool redisOk = RedisPool::Instance().init(
        "127.0.0.1",
        6379,
        10
    );
    if (!redisOk) {
        std::cerr << "[main] RedisPool init FAILED!" << std::endl;
        return -1;
    } 
    std::cout << "[main] RedisPool init OK\n";

    // ① 创建 Reactor（事件循环）
    reactor rect(1024, true);
    g_reactor = &rect;

    // ② 创建线程池
    ThreadPool pool(4, 1024);
    pool.run();

    // ③ 创建 Server
    Server server(rect, 8888, true, &pool);
    g_server = &server;

    // ④ 启动 Server
    if (!server.start()) {
        std::cerr << "Server start failed\n";
        return -1;
    }

    // ⑤ 注册 Ctrl+C
    std::signal(SIGINT, handleSigint);

    // ⑥ 开始事件循环
    std::cout << "Server is running on port 8888\n";
    rect.loop();

    return 0;
}
