#pragma once
#include <mutex>
#include <condition_variable>
#include <queue>

template<typename T>
class SafeQueue {
public:
    SafeQueue(int max_event = 1024)
        : max_event_(max_event) {}

    bool Safepop(T& value) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [&]() {
            return stop_ || !queue_.empty();
        });

        if (stop_ && queue_.empty())
            return false;

        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool Safepush(const T& value) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [&]() {
            return stop_ || queue_.size() < max_event_;
        });

        if (stop_)
            return false;

        queue_.push(value);
        cv_.notify_one();
        return true;
    }

    void SetMaxEvent(int count) {
        max_event_ = count;
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
    int max_event_{1024};
    bool stop_{false};
};
