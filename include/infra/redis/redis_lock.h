#pragma once
#include "infra/redis/redis_client.h"
#include <random>
#include <atomic>
#include <thread>
namespace infra::redis {

class RedisLock {
public:
    using Seconds = std::chrono::seconds;

    RedisLock(RedisClient& redis, const std::string& key, Seconds ttl)
    : redis_(redis), key_(key), ttl_(ttl), ownerId_(genOwnerId()), locked_(false) {}
    ~RedisLock() {
        if (locked_) {
            unlock();
        }
    }

    /**
     * @brief 尝试获取锁
     * 
     * @return true 当前已经有锁
     * @return false 否者要去redis上获取
     */
    bool tryLock() {
        if (locked_) {
            return true;
        }
        locked_ = redis_.setNxEx(key_, ownerId_, ttl_);
        return locked_;
    }

    void unlock () {
        if (!locked_) {
            return ;
        }
        static const std::string script = R"(
            if redis.call("GET", KEYS[1]) == ARGV[1] then
                return redis.call("DEL", KEYS[1])
            else
                return 0
            end
        )";

        redis_.eval(script, {key_}, {ownerId_});
        locked_ = false;
    }

    const std::string& getOwnerId () const { return ownerId_; }
    const std::string& getKey () const { return key_; }
private:
    bool locked_{false};
    std::string ownerId_;
    RedisClient& redis_;
    std::string key_;
    Seconds ttl_;

    /**
     * @brief 生成 ownerId
     * @return std::string 给这把分布式锁生成一个“几乎不会重复”的随机 ownerId，
     * 格式是 16 位十六进制字符串，
     * 比如：3fa92bc01d7e4c55
     */
     static std::string genOwnerId() {
        static thread_local std::mt19937_64 rng{std::random_device{}()};
        uint64_t r = rng();
        char buf[32];
        //把整数格式化成字符串
        //"%016llx"%x：把整数按十六进制（小写 a~f）输出
        // ll：对应的参数类型是 long long
        // 016：宽度至少 16 位，不足前面填充 0
        std::snprintf(buf, sizeof(buf), "%016llx",
                      static_cast<long long>(r));
        return std::string(buf);
    }
};

    /**
     * @brief 启动一个独占锁的守护线程
     * 
     * @param redis 因为我的RedisClient是个抽象类，所以说只能引用，不能实例化
     * @param stopFlag 原子变量，用于控制守护线程的退出
     * 
     */
    void startWatchDog(RedisClient& redis,
                   const std::string& key,
                   const std::string& ownerId,
                   RedisClient::Seconds ttl,
                   std::atomic<bool>& stopFlag,
                   std::atomic<bool>& lostFlag) {
    std::thread([&redis, key, ownerId, ttl, &stopFlag, &lostFlag]() {
        auto sleepDur = ttl / 2;
        while (!stopFlag.load()) {
            std::this_thread::sleep_for(sleepDur);
            auto val = redis.get(key);
            if (!val.has_value() || *val != ownerId) {
                // 锁丢了 / 被别人抢走
                lostFlag.store(true);  // 单独的“告警信号”
                break;
            }
            redis.expire(key, ttl);
        }
    }).detach();
    }


}