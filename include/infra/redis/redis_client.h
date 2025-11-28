#pragma once 

#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>

namespace infra::redis {

/**
 * @brief Redis 客户端抽象接口
 * 
 * 上层（例如 CacheClient、业务代码）只依赖这个接口，
 * 具体实现可以用 hiredis / redis-plus-plus / 自己封装。
 */
class RedisClient {
public:
    using Seconds = std::chrono::seconds;

    /**
     * @brief 虚析构，保证通过基类指针删除时行为正确
     */
    virtual ~RedisClient() = default;

    /**
     * @brief 设置字符串键值，可选过期时间（物理 TTL）
     * 
     * 等价于：
     * - 无 ttl 时：SET key value
     * - 有 ttl 时：SET key value EX ttl
     * 
     * @param key   键名
     * @param value 键对应的字符串值
     * @param ttl   可选过期时间，为 std::nullopt 时表示不过期
     */
    virtual void set(std::string_view key,
                     std::string_view value,
                     std::optional<Seconds> ttl = std::nullopt) = 0;

    /**
     * @brief 仅当 key 不存在时写入值，并设置过期时间
     * 
     * 语义上对应：SET key value NX EX ttlSeconds
     * 常用于分布式锁、抢占式初始化等场景。
     * 
     * @param key         键名
     * @param value       写入的字符串值
     * @param ttlSeconds  过期时间（秒级）
     */
    virtual void setNX(std::string_view key,
                       std::string_view value,
                       Seconds ttlSeconds) = 0;

    /**
     * @brief 获取字符串键值
     * 
     * @param key 键名
     * @return std::optional<std::string> 
     *         - 有值：返回对应字符串
     *         - std::nullopt：key 不存在
     */
    virtual std::optional<std::string> get(std::string_view key) = 0;

    /**
     * @brief 删除指定键
     * 
     * 对应 Redis 命令：DEL key
     * 
     * @param key 键名
     * @return long long 
     *         - 返回实际删除的 key 数量（通常为 0 或 1）
     */
    virtual long long del(std::string_view key) = 0;

    /**
     * @brief 为已有 key 设置过期时间
     * 
     * 对应 Redis 命令：EXPIRE key seconds
     * 
     * @param key 键名
     * @param ttl 过期时间（秒级）
     * @return true  设置成功（key 存在且 TTL 已更新）
     * @return false 设置失败（例如 key 不存在）
     */
    virtual bool expire(std::string_view key, Seconds ttl) = 0;

    /**
     * @brief 将 key 对应的整数值按增量进行自增/自减
     * 
     * 对应 Redis 命令：INCRBY key delta
     * 
     * - key 不存在时，视为 0 先创建再自增
     * - delta 可为负值，相当于自减
     * 
     * @param key   键名
     * @param delta 增量（可为负）
     * @return long long 
     *         - 自增后的最新值
     */
    virtual long long incrBy(std::string_view key, long long delta) = 0;

    /**
     * @brief 执行 Lua 脚本（EVAL）
     * 
     * 对应 Redis 命令：EVAL script numKeys key... arg...
     * 
     * 常用于需要原子性保证的复杂逻辑，例如：
     * - 分布式锁
     * - 计数与限流
     * - 多 key 原子修改
     * 
     * @param script Lua 脚本内容
     * @param keys   作为 KEYS 传入脚本的键列表
     * @param args   作为 ARGV 传入脚本的参数列表
     * @return long long 
     *         - 脚本返回值转换成的整数（适用于脚本以整数为返回值的场景）
     */
    virtual long long eval(const std::string& script,
                           const std::vector<std::string>& keys,
                           const std::vector<std::string>& args) = 0;
};

} // end namespace infra::redis
