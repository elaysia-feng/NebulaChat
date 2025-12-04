// RedisConnection.h
#pragma once

#include <hiredis/hiredis.h>
#include <string>
#include <memory>

class RedisConnection
{
private:
    //创建redisContext
    redisContext* ctx_{nullptr};
public:
    RedisConnection();
    ~RedisConnection();

    //禁止浅拷贝
    RedisConnection(const RedisConnection& )            = delete;
    RedisConnection& operator= (const RedisConnection&) = delete;

    // 连接到 Redis
    bool connect(const std::string& host, int port);

    // 常用操作封装
    bool set(const std::string& key, const std::string& value);
    bool setEX(const std::string& key, const std::string& value, int seconds);
    //out传进来肯定是为空的要改变所以不用const
    bool get(const std::string& key, std::string& out);
    bool del(const std::string& key);

    // 暴露原始指针（如有需要）
    redisContext* raw() { return ctx_; }
};


using RedisConnPtr = std::shared_ptr<RedisConnection>;
