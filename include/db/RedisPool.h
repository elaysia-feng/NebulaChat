#pragma once
#include "db/RedisConnection.h"
#include "core/SafeQueue.h"
#include <string>
#include <mutex>

class RedisPool
{
public:
    static RedisPool& Instance();

    bool init(const std::string& host, int port, int poolSize);

    // 从池子中取一个连接（shared_ptr，自动归还）
    RedisConnPtr getConnection();

private:
    RedisPool()  = default;
    ~RedisPool() = default;

    RedisPool(const RedisPool&)            = delete;
    RedisPool& operator=(const RedisPool&) = delete;

private:
    SafeQueue<RedisConnPtr> pool_;
    std::once_flag initFlag_;
    bool inited_{false};

    std::string host_;
    int         port_{0};
    int         poolSize_{0};
};
