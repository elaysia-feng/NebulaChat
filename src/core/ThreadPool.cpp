#include "core/ThreadPool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t threadCount, int maxCount)
    : stop_(false), tasks_(maxCount)
{
    workers_.reserve(threadCount);
}

ThreadPool::~ThreadPool() {
    stop_ = true;
    tasks_.Stop();

    for (auto& t : workers_) {
        if (t.joinable())
            t.join();
    }
}

void ThreadPool::Enqueue(std::function<void()> task) {
    tasks_.Safepush(task);
}

void ThreadPool::RunPool() {
    while (!stop_) {
        std::function<void()> task;
        if (tasks_.Safepop(task)) {
            task();
        }
    }
}

void ThreadPool::run() {
    for (size_t i = 0; i < workers_.capacity(); ++i) {
        workers_.emplace_back([this]() {
            RunPool();
        });
    }
}
