#include "core/ThreadPool.h"
#include <iostream>
#include <chrono>

void work(int id) {
    std::cout << "任务 " << id << " 开始\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "任务 " << id << " 完成\n";
}

int main() {
    ThreadPool pool(4);

    for (int i = 0; i < 10; ++i) {
        pool.Enqueue([i] { work(i); });
    }
    pool.run();

    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::cout << "主线程结束\n";
}
