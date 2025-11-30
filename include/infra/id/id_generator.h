// include/infra/id/id_generator.h
#pragma once

#include "infra/redis/redis_client.h"
#include <chrono>
#include <string>

namespace infra::id {

class IdGenerator {
public:
    using Clock   = std::chrono::system_clock;
    using Seconds = std::chrono::seconds;

    /**
     * @brief IdGenerator 构造函數
     * 
     * @param redis  redis 客戶端
     * @param workerId  worker id，0~31
     */
    IdGenerator(infra::redis::RedisClient& redis,
                int workerId)
        : redis_(redis),
          workerId_(workerId),
          epoch_(makeEpoch(2023, 1, 1)) {}

    long long nextId(const std::string& bizKey) {
        using namespace std::chrono;

        auto now  = Clock::now();
        auto diff = duration_cast<Seconds>(now.time_since_epoch()) -
                    duration_cast<Seconds>(epoch_.time_since_epoch());

        long long timePart = diff.count(); // 31 bit 时间

        std::string dateStr = formatDate(now); // "20251126"
        std::string seqKey  = "id:" + bizKey + ":" + dateStr;
        long long seq = redis_.incrBy(seqKey, 1);

        // 拼：时间31 + worker10 + seq22 = 63bit
        const int WORKER_BITS = 10;
        const int SEQ_BITS    = 22;

        long long workerPart = (static_cast<long long>(workerId_) &
                               ((1LL << WORKER_BITS) - 1));
        long long seqPart = (seq & ((1LL << SEQ_BITS) - 1));

        long long id = (timePart << (WORKER_BITS + SEQ_BITS)) |
                       (workerPart << SEQ_BITS) |
                       seqPart;
        return id;
    }

private:
    infra::redis::RedisClient& redis_;
    int                        workerId_;
    Clock::time_point          epoch_;

    /**
     * @brief 生成时间点
     * 
     * @param y    年（如 2023）
     * @param m    月（1-12）
     * @param d    日（1-31）
     * @return Clock::time_point  生成的时间点
     */
    static Clock::time_point makeEpoch(int y, int m, int d) {
        std::tm tm{};
        tm.tm_year = y - 1900;
        tm.tm_mon  = m - 1;
        tm.tm_mday = d;
        tm.tm_hour = 0;
        tm.tm_min  = 0;
        tm.tm_sec  = 0;
        tm.tm_isdst = -1;
        auto tt = std::mktime(&tm);
        return Clock::from_time_t(tt);
    }

    static std::string formatDate(Clock::time_point tp) {
        std::time_t t = Clock::to_time_t(tp);
        std::tm tm{};
        localtime_r(&t, &tm);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d%02d%02d",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        return std::string(buf);
    }
};

} // namespace infra::id
