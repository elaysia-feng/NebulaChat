#include "core/ThreadPool.h"
#include "core/Logger.h"   // 新增：日志头文件
#include <thread>

ThreadPool::ThreadPool(size_t threadCount, int maxCount)
    : stop_(false), tasks_(maxCount)
{
    workers_.reserve(threadCount);
    LOG_INFO("[ThreadPool::ThreadPool] create thread pool, threads="
             << threadCount << ", maxTasks=" << maxCount);
}

ThreadPool::~ThreadPool() {
    LOG_INFO("[ThreadPool::~ThreadPool] stopping thread pool...");

    // 通知工作线程退出
    stop_ = true;
    tasks_.Stop();

    // 等待所有线程结束
    for (auto& t : workers_) {
        if (t.joinable()) {
            LOG_DEBUG("[ThreadPool::~ThreadPool] joining worker thread "
                      << t.get_id());
            t.join();
        }
    }

    LOG_INFO("[ThreadPool::~ThreadPool] all worker threads joined");
}

void ThreadPool::Enqueue(std::function<void()> task) {
    // 将任务放入安全队列
    if (!tasks_.Safepush(std::move(task))) {
        LOG_ERROR("[ThreadPool::Enqueue] push task failed (queue stopped?)");
    } else {
        // 这里日志可以视情况注释掉，任务多的话会比较吵
        LOG_DEBUG("[ThreadPool::Enqueue] task enqueued");
    }
}

void ThreadPool::RunPool() {
    LOG_INFO("[ThreadPool::RunPool] worker thread "
             << std::this_thread::get_id() << " start");

    while (!stop_) {
        std::function<void()> task;
        // Safepop 内部会阻塞等待，有任务或 Stop 后才返回
        if (tasks_.Safepop(task)) {
            LOG_DEBUG("[ThreadPool::RunPool] worker "
                      << std::this_thread::get_id()
                      << " got one task");
            try {
                task();
            } catch (const std::exception& e) {
                LOG_ERROR("[ThreadPool::RunPool] exception in task: "
                          << e.what());
            } catch (...) {
                LOG_ERROR("[ThreadPool::RunPool] unknown exception in task");
            }
        } else {
            // Safepop 返回 false，说明队列 stop 了
            if (stop_) {
                LOG_INFO("[ThreadPool::RunPool] worker "
                         << std::this_thread::get_id()
                         << " exits because pool stopped");
                break;
            }
        }
    }

    LOG_INFO("[ThreadPool::RunPool] worker thread "
             << std::this_thread::get_id() << " exit");
}

void ThreadPool::run() {
    LOG_INFO("[ThreadPool::run] starting workers, count="
             << workers_.capacity());

    for (size_t i = 0; i < workers_.capacity(); ++i) {
        workers_.emplace_back([this]() {
            RunPool();
        });
        LOG_DEBUG("[ThreadPool::run] worker " << i
                  << " started, id=" << workers_.back().get_id());
    }
}
