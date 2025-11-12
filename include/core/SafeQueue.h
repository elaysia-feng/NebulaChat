#pragma once
#include <mutex>
#include <condition_variable>
#include <queue>

template<typename T>
class SafeQueue {
public:
    SafeQueue(int max_event = 1024)
        : max_event_(max_event) {}

    // 阻塞出队；队列空且已 stop 则返回 false
    bool Safepop(T& value) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [&]() {
            return stop_ || !queue_.empty();
        });

        if (stop_ && queue_.empty())
            return false;

        value = std::move(queue_.front());
        queue_.pop();
        // 释放一个可能在“队满”上等待的生产者
        cv_.notify_one();
        return true;
    }
    // 方案A：按值接收，支持移动
    template<class U>
    bool Safepush(U&& value) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [&]() {
            return stop_ || max_event_ == 0 || queue_.size() < max_event_;
        });
        if (stop_) return false;
        queue_.push(std::forward<U>(value));
        cv_.notify_one();
        return true;
    }

    void SetMaxEvent(size_t count) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            max_event_ = count;
        }
        cv_.notify_all();
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
    }

private:
    std::queue<T> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    size_t max_event_{1024};
    bool stop_{false};
};
