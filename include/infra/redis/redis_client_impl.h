// redis_client_impl.h
#pragma once
#include "infra/redis/redis_client.h"
#include "db/RedisPool.h"

namespace infra::redis {

class RedisClientImpl : public RedisClient {
public:
    RedisClientImpl() = default;

    void set(std::string_view key,
             std::string_view value,
             std::optional<Seconds> ttl = std::nullopt) override;

    bool setNxEx(std::string_view key,
                 std::string_view value,
                 Seconds ttlSeconds) override;

    std::optional<std::string> get(std::string_view key) override;
    long long                  del(std::string_view key) override;
    bool                       expire(std::string_view key, Seconds ttl) override;
    long long                  incrBy(std::string_view key, long long delta) override;
    long long                  eval(const std::string& script,
                                    const std::vector<std::string>& keys,
                                    const std::vector<std::string>& args) override;

private:
    // 每次操作临时从连接池拿一个连接即可
    RedisConnPtr getConn() {
        return RedisPool::Instance().getConnection();
    }
};

} // namespace infra::redis
