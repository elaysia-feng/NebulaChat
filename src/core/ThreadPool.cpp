#include "core/ThreadPool.h"
#include <iostream>
#include <thread>

ThreadPool::ThreadPool(size_t threadCount, int maxCount)
    : stop_(false), tasks_(maxCount)
{
    workers_.reserve(threadCount);
    std::cout << "[ThreadPool::ThreadPool] create thread pool, threads="
              << threadCount << ", maxTasks=" << maxCount << std::endl;
}

ThreadPool::~ThreadPool() {
    std::cout << "[ThreadPool::~ThreadPool] stopping thread pool..." << std::endl;

    // 通知工作线程退出
    stop_ = true;
    tasks_.Stop();

    // 等待所有线程结束
    for (auto& t : workers_) {
        if (t.joinable()) {
            std::cout << "[ThreadPool::~ThreadPool] joining worker thread "
                      << t.get_id() << std::endl;
            t.join();
        }
    }

    std::cout << "[ThreadPool::~ThreadPool] all worker threads joined" << std::endl;
}

void ThreadPool::Enqueue(std::function<void()> task) {
    // 将任务放入安全队列
    if (!tasks_.Safepush(std::move(task))) {
        std::cerr << "[ThreadPool::Enqueue] push task failed (queue stopped?)"
                  << std::endl;
    } else {
        // 这里日志可以视情况注释掉，任务多的话会比较吵
        std::cout << "[ThreadPool::Enqueue] task enqueued" << std::endl;
    }
}

void ThreadPool::RunPool() {
    std::cout << "[ThreadPool::RunPool] worker thread "
              << std::this_thread::get_id() << " start" << std::endl;

    while (!stop_) {
        std::function<void()> task;
        // Safepop 内部会阻塞等待，有任务或 Stop 后才返回
        if (tasks_.Safepop(task)) {
            std::cout << "[ThreadPool::RunPool] worker "
                      << std::this_thread::get_id()
                      << " got one task" << std::endl;
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "[ThreadPool::RunPool] exception in task: "
                          << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[ThreadPool::RunPool] unknown exception in task"
                          << std::endl;
            }
        } else {
            // Safepop 返回 false，说明队列 stop 了
            if (stop_) {
                std::cout << "[ThreadPool::RunPool] worker "
                          << std::this_thread::get_id()
                          << " exits because pool stopped" << std::endl;
                break;
            }
        }
    }

    std::cout << "[ThreadPool::RunPool] worker thread "
              << std::this_thread::get_id() << " exit" << std::endl;
}

void ThreadPool::run() {
    std::cout << "[ThreadPool::run] starting workers, count="
              << workers_.capacity() << std::endl;

    for (size_t i = 0; i < workers_.capacity(); ++i) {
        workers_.emplace_back([this]() {
            RunPool();
        });
        std::cout << "[ThreadPool::run] worker " << i
                  << " started, id=" << workers_.back().get_id() << std::endl;
    }
}
