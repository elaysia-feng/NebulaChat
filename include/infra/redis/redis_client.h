#pragma once 
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>

namespace infra::redis {

class RedisClient {
public:
    using Seconds = std::chrono::seconds;

    virtual ~RedisClient() = default;

    // 普通 set，带可选 TTL
    virtual void set (std::string_view key, std::string_view value, std::optional<Seconds> ttl = std::nullopt) = 0;

    // SET key value NX EX ttlSeconds
    virtual void setNX (std::string_view key, std::string_view value, Seconds ttlSeconds) = 0;

    // GET key
    virtual std::optional<std::string> get (std::string_view key) = 0;

    // DEL key
    virtual long long del (std::string_view key) = 0;

    //EXPIRE key seconds
    virtual bool expire (std::string_view key, Seconds ttl) = 0;

    // INCRBY key
    virtual long long incrBy (std::string_view key, long long delta) = 0;

    // EVAL script numKeys key... arg...
    virtual long long eval (const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args) = 0;
};


}//end namespace