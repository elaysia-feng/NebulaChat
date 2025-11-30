#pragma once 

#include <nlohmann/json.hpp>
#include "infra/redis/redis_client.h"
#include <functional>
#include <thread>
#include <optional>
#include <chrono>
#include <utility>
#include <string>

namespace infra::redis {

/**
 * @brief 统一的缓存客户端封装
 * 
 * 基于 RedisClient 提供常见的缓存策略：
 * - 物理 TTL 缓存
 * - 逻辑过期缓存
 * - 防缓存穿透（空值缓存）
 * - 防缓存击穿（逻辑过期 + 异步重建）
 */
using Json = nlohmann::json;

class CacheClient {
public:
    using Clock   = std::chrono::steady_clock;
    using Seconds = std::chrono::seconds;

    /// 后台任务提交函数：接收一个 task（void()），由调用方决定如何调度执行
    using BackgroundSubmit = std::function<void(std::function<void()>)>;

    /**
     * @brief DB Loader 类型：缓存未命中时，用该函数去查询 DB
     * 
     * @tparam T 要加载的数据类型
     * 
     * 函数语义：
     * - 查询成功 → 返回 std::optional<T> 包裹的值
     * - 查询失败 / 不存在 → 返回 std::nullopt
     */
    template <typename T>
    /**
     * @brief std::optional<T> 是返回类型
     * 
     */
    using OptionalLoader = std::function<std::optional<T>()>;

    /**
     * @brief 构造 CacheClient
     * 
     * @param redis   底层 Redis 客户端引用
     * @param submit  后台任务提交函数（可选）。
     *                - 若传入：用于提交异步重建任务（如线程池）
     *                - 若不传：内部会在需要时 new std::thread + detach
     */
    explicit CacheClient(RedisClient& redis,
                         BackgroundSubmit submit = {})
        : redis_(redis)
        , submitBackground_(std::move(submit)) {}

    /**
     * @brief 方法 1：使用物理 TTL 写入缓存
     * 
     * 直接依赖 Redis 的过期机制：
     * - key 在 Redis 中设置 EX（过期时间）
     * - 到期后 key 直接被 Redis 删除
     * 
     * @tparam T       需要缓存的类型，需支持 nlohmann::json 的序列化
     * @param key      缓存键名
     * @param value    要写入的业务数据
     * @param ttl      物理 TTL（过期时间）
     */
    template <typename T>
    void set(const std::string& key,
             const T& value,
             Seconds ttl);

    /**
     * @brief 方法 2：逻辑过期缓存写入
     * 
     * 将数据包装成结构：
     * @code
     * {
     *   "data":     ...真实业务数据...,
     *   "expireAt": ...逻辑过期时间戳（秒）...
     * }
     * @endcode
     * 
     * 特点：
     * - 不设置 Redis TTL，key 持久存在
     * - 过期判断由业务自己根据 expireAt 与当前时间来决定
     * 
     * @tparam T            需要缓存的类型，需支持 nlohmann::json 序列化
     * @param key           缓存键名
     * @param value         业务数据
     * @param logicalTtl    逻辑过期时间（从当前时间起算）
     */
    template <typename T>
    void setLogicalExpire(const std::string& key,
                          const T& value,
                          Seconds logicalTtl);

    /**
     * @brief 方法 3：防缓存穿透（空值缓存）
     * 
     * 访问流程：
     * 1. 读取 Redis：
     *    - 命中空值标记（NULL_MARK）→ 直接返回 std::nullopt，避免打到 DB
     *    - 命中正常值 → 解析 JSON 成 T 返回
     *    - 解析失败 / 未命中 → 进入 DB 查询
     * 2. 读取 DB（通过 loader）：
     *    - 若 DB 也无数据 → 写入空值标记 + 短 TTL（nullTtl），返回 std::nullopt
     *    - 若 DB 有数据   → 写入正常缓存 + normalTtl，返回数据
     * 
     * 适用场景：
     * - 防止恶意请求 / 高频访问不存在的 key 导致 DB 被打爆
     * 
     * @tparam T            业务数据类型
     * @param key           缓存键名
     * @param nullTtl       空值标记的 TTL（一般较短）
     * @param normalTtl     正常数据的 TTL
     * @param loader        DB 加载函数，在缓存未命中时调用
     * @return std::optional<T> 
     *         - 有值：返回缓存/DB 中的数据
     *         - 无值：数据不存在（包括命中空值缓存）
     */
    template <typename T>
    std::optional<T> getWithPassThrough(const std::string& key,
                                        Seconds nullTtl,
                                        Seconds normalTtl,
                                        OptionalLoader<T>&& loader);

    /**
     * @brief 方法 4：防缓存击穿（逻辑过期 + 异步重建）
     * 
     * 综合逻辑：
     * 1. Redis 无数据：
     *    - 调用 loader 查询 DB
     *    - 若 DB 无数据：返回 std::nullopt
     *    - 若 DB 有数据：setLogicalExpire 写入逻辑过期缓存，返回数据
     * 
     * 2. Redis 有数据但 JSON 解析失败：
     *    - 视为“坏数据”，调用 loader 查询 DB，成功则重建缓存并返回
     * 
     * 3. Redis 有逻辑过期结构（data + expireAt）：
     *    - 若未过期（now < expireAt）：
     *        → 直接返回 data
     *    - 若已过期：
     *        → 先返回旧 data 兜底，不阻塞当前请求
     *        → 后台线程异步调用 loader 重建缓存（更新 data + expireAt）
     * 
     * 适用场景：
     * - 热点 key，有一定读压力，不能让大量请求在 key 失效瞬间直接打 DB
     * 
     * @tparam T            业务数据类型
     * @param key           缓存键名
     * @param logicalTtl    逻辑过期时间，用于重建时写入
     * @param loader        DB 加载函数，用于缓存未命中或数据过期时重建
     * @return std::optional<T> 
     *         - 有值：可能是未过期的缓存 或 已过期但兜底返回的旧值
     *         - 无值：缓存和 DB 都不存在该数据
     */
    template <typename T>
    std::optional<T> getWithLogicalExpire(const std::string& key,
                                          Seconds logicalTtl,
                                          OptionalLoader<T>&& loader);

private:
    /// 底层 Redis 客户端引用
    RedisClient& redis_;

    /// 后台任务提交回调（可由上层注入线程池等实现）
    BackgroundSubmit submitBackground_;

    /// 空值标记，用于防止缓存穿透
    static constexpr const char* NULL_MARK = "_NULL_";

    /**
     * @brief 提交后台任务
     * 
     * 行为：
     * - 若调用方注入了 submitBackground_（如线程池），则通过它提交任务
     * - 否则退化为手动创建 std::thread 并 detach
     * 
     * @param task 后台执行的任务函数
     */
    void submitBackground(std::function<void()> task) {
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
    // T 需要支持 nlohmann::json 的序列化（to_json/from_json）
    Json j = value;
    // 直接依赖 Redis 的 TTL 做“物理过期”
    redis_.set(key, j.dump(), ttl);
}

template <typename T>
void CacheClient::setLogicalExpire(const std::string& key,
                                   const T& value,
                                   Seconds logicalTtl)
{
    using namespace std::chrono;

    // 当前时间 + 逻辑 TTL → 逻辑过期时间戳（秒）
    auto expireTime = Clock::now() + logicalTtl;
    auto expireSec  = duration_cast<Seconds>(
        expireTime.time_since_epoch()
    ).count();

    Json j;
    j["data"]     = value;     // 真正业务数据
    j["expireAt"] = expireSec; // 逻辑过期时间（秒）

    // 不设置 Redis TTL，Key 持久存在，由我们自己判断是否过期
    redis_.set(key, j.dump());
}

template <typename T>
std::optional<T> CacheClient::getWithPassThrough(
    const std::string& key,
    Seconds nullTtl,
    Seconds normalTtl,
    OptionalLoader<T>&& loader)
{
    // 1. 先查 Redis
    auto cacheVal = redis_.get(key);

    if (cacheVal.has_value()) {
        // 1.1 命中空值标记：之前查过 DB 是空的，直接返回 nullopt
        if (*cacheVal == NULL_MARK) {
            return std::nullopt;
        }

        // 1.2 命中正常数据，尝试反序列化
        try {
            Json j = Json::parse(*cacheVal);
            return j.get<T>();
        } catch (...) {
            // 解析失败，当作未命中，继续往下走 DB 分支
        }
    }

    // 2. 缓存未命中 / JSON 异常 → 查 DB
    auto dbRes = loader();

    // 2.1 DB 也没数据：写入空值标记 + 短 TTL 防穿透
    if (!dbRes.has_value()) {
        redis_.set(key, NULL_MARK, nullTtl);
        return std::nullopt;
    }

    // 2.2 DB 有数据：写回缓存 + 正常 TTL
    Json j = *dbRes;
    redis_.set(key, j.dump(), normalTtl);
    return dbRes;
}

template<typename T>
std::optional<T> CacheClient::getWithLogicalExpire(
    const std::string& key,
    Seconds logicalTtl,
    OptionalLoader<T>&& loader)
{
    using namespace std::chrono;

    // 1. 先查 Redis
    auto cacheVal = redis_.get(key);

    // 1.1 完全没命中 → 直接查 DB + 构建逻辑过期结构
    if (!cacheVal.has_value()) {
        auto dbRes = loader();
        if (!dbRes.has_value()) {
            return std::nullopt;
        }

        setLogicalExpire<T>(key, *dbRes, logicalTtl);
        return dbRes;
    }

    // 2. 尝试解析 JSON
    Json j;
    try {
        j = Json::parse(*cacheVal);
    } catch (const Json::parse_error&) {
        // Redis 里存的是垃圾 / 旧数据 → 查 DB + 重建
        auto dbRes = loader();
        if (!dbRes.has_value()) {
            return std::nullopt;
        }

        setLogicalExpire<T>(key, *dbRes, logicalTtl);
        return dbRes;
    }

    // 3. 兼容旧格式：没有 data/expireAt，就当它是直接存了一个 T
    if (!j.contains("data") || !j.contains("expireAt")) {
        try {
            return j.get<T>();
        } catch (const Json::type_error&) {
            // 旧数据也解析不了 → 当作坏数据 → 查 DB + 重建
            auto dbRes = loader();
            if (!dbRes.has_value()) {
                return std::nullopt;
            }

            setLogicalExpire<T>(key, *dbRes, logicalTtl);
            return dbRes;
        }
    }

    // 4. 正常逻辑过期结构：取出 expireAt 和 data
    long long expireSec = 0;
    try {
        expireSec = j["expireAt"].get<long long>();
    } catch (...) {
        // expireAt 异常，直接视为已过期
        expireSec = 0;
    }

    T data;
    try {
        data = j["data"].get<T>();
    } catch (...) {
        // data 字段异常，同样查 DB + 重建
        auto dbRes = loader();
        if (!dbRes.has_value()) {
            return std::nullopt;
        }

        setLogicalExpire<T>(key, *dbRes, logicalTtl);
        return dbRes;
    }

    auto nowSec = duration_cast<Seconds>(
        Clock::now().time_since_epoch()
    ).count();

    // 4.1 逻辑上未过期 → 直接返回 data
    if (nowSec < expireSec) {
        return data;
    }

    // 4.2 已过期 → 先返回旧 data，再异步重建缓存
    submitBackground([this,
                      key,
                      logicalTtl,
                      loader = std::forward<OptionalLoader<T>>(loader)]() mutable {
        auto fresh = loader();
        if (fresh.has_value()) {
            this->setLogicalExpire<T>(key, *fresh, logicalTtl);
        }
    });

    // 对调用方来说：虽然逻辑已过期，但这里仍兜底返回旧 data
    return data;
}

} // namespace infra::redis
