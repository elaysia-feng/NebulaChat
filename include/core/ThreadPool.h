#pragma once
#include "SafeQueue.h"
#include <thread>
#include <atomic>
#include <functional>
#include <vector>

class ThreadPool {
public:
    ThreadPool(size_t threadCount = 4, int maxCount = 1024);
    ~ThreadPool();
    void Enqueue(std::function<void()> task);
    void run();

private:
    std::atomic<bool> stop_;
    std::vector<std::thread> workers_;
    SafeQueue<std::function<void()>> tasks_;

private:
    void RunPool();
};
