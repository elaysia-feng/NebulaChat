#pragma once 
#include <infra/redis/redis_client.h>
#include <nlohmann/json.hpp>
#include <functional>
#include <thread>


namespace infra::redis {

class CacheClient {
public:
    using Clock     = std::chrono::steady_clock;
    using Seconds   = std::chrono::seconds;
    using Json      = nlohmann::json;
    using BackgroundSubmit = std::function<void(std::function<void()>)>;

    explicit CacheClient (RedisClient& redis, BackgroundSubmit submit = {})
    : redis_(redis), submitBackground_(submit){}

    // 方法1: 物理TTL缓存
    template <typename T>
    void set (const std::string& key, const T& value, Seconds ttl);

    // 方法2: 逻辑过期缓存
    template <typename T>
     void setLogicalExprie(const std::string& key,
                    const T& value,
                    Seconds logicalTtl);
    // 方法3: 防缓存穿透（空值缓存）
    template <typename T>
    using OptionalLoader = std::function<std::optional<T>()>;
    template <typename T>
    std::optional<T> getWithPassThrough (const std::string& key, Seconds nullTtl, Seconds normalTtl, OptionalLoader<T>&& loader);

    
    // 方法4: 防缓存击穿（逻辑过期 + 异步重建）
    template <typename T>
    std::optional<T> getWithLogicalExpire(const std::string& key,
                                          Seconds logicalTtl,
                                          OptionalLoader<T>&& loader);



private:
    RedisClient& redis_;
    BackgroundSubmit submitBackground_;
    static constexpr const char* NULL_MARK = "_NULL_";

    void submitBackground (std::function<void()> task) {
        if (submitBackground_) {
            submitBackground_(std::move(task));
        } else {
            std::thread th(std::move(task));
            th.detach();
        }
    }
};

// ================= 模板实现 =================
template<typename T>
void CacheClient::set(const std::string& key,
                      const T& value,
                      Seconds ttl)
{
    json j = value; // 要求存在 to_json/from_json
    redis_.set(key, j.dump(), ttl);
}

template <typename T>
void CacheClient::setLogicalExprie(const std::string& key,
                    const T& value,
                    Seconds logicalTtl) 
{
    using namespace std::chrono;
    auto expireTime = Clock::now() + logicalTtl;
    auto expireSec  = duration_cast<Seconds>(expireTime.time_since_epoch()).count();

    Json j;
    j["data"]     = value;
    j["expireAt"] = expireSec;

    redis_.set(key, j.dump()); // 不设置物理 TTL
}

template <typename T>
std::optional<T> CacheClient::getWithPassThrough(
    const std::string& key,
    Seconds nullTtl,
    Seconds normalTtl,
    OptionalLoader<T>&& loader) 
{
    auto cacheVal = redis_.get(key);

    if (cacheVal.has_value()) {
        if (*cacheVal == NULL_MAKE) {
            return std::nullopt;
        }
        try {
            Json j = Json::parse(*cacheVal);
            return j.get<T>();
        } catch (...) {
            // 解析失败，当作未命中
        }

        // 2. DB
        auto dbRes = loader();

        if (! dbRes.has_value()) {
            redis_.set(key, NULL_MARK, nullTtl);
            return std::nullopt;
        }

        
        Json j = *dbRes;
        redis_.set(key, j.dump(), normalTtl);
        return  dbRes;
    }
}


template<typename T>
std::optional<T> CacheClient::getWithLogicalExpire(
    const std::string& key,
    Seconds logicalTtl,
    OptionalLoader<T>&& loader)
{
    auto cacheVal = redis_.get(key);
    if (!cacheVal.has_value()) {
        auto dbRes = loader();
        if (!dbRes.has_value()) {
            return std::nullopt;
        }

        setLogicalExprie<T> (key, *dbRes, logicalTtl);
        return dbRes;
    }
    Json j;
    try {
        j = Json::parse(*cacheVal);
    } catch(const Json::parse_error& e){
        // json解析失败
        auto dbRes = loader();
        if (!dbRes.has_value()) {
            return std::nullopt;
        }
        
        setLogicalExprie<T> (key, *dbRes, logicalTtl);
        return dbRes;
    }
    //    兼容：如果 Redis 里存的不是逻辑过期结构（没有 data / expireAt），
    //    说明可能是旧版，直接把整个 JSON 当成 T 来解析。
    if (!j.contains("data") || !j.contains("expireAt")) {
        try {
            return j.get<T>;
        } catch(const Json::type_error& e) {
            auto dbRes = loader();
            if (!dbRes.has_value()) {
                return std::nullopt;
            }
            
            setLogicalExprie<T> (key, *dbRes, logicalTtl);
            return dbRes;
        }

    }

    long long expireSec = 0;
    try { expireSec = j["expireAt"].get<long long>();}catch (...) {}
    
    T data;
    try {
        data = j["data"].get<T>()
    } catch(...) {
        auto dbRes = loader();
        if (!dbRes.has_value()) {
            return std::nullopt;
        }
        
        setLogicalExprie<T> (key, *dbRes, logicalTtl);
        return dbRes;
    }

    auto nowSec = duration_cast<Seconds>(
        Clock::now().time_since_epoch()).count();

    if (nowSec < expireSec) {
        return data; // 未过期
    }

    submitBackground([this, key, logicalTtl, loader = std::forwoard<OptionalLoader>(loader)]() mutable {
        auto fresh = loader();
        if (fresh.has_value()) {
            this->setLogicalExprie<T>(key, *fresh, logicalTtl);
        }
    });

    return data;

}

