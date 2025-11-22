#include "chat/ChatHistory.h"
#include "db/DBpool.h"
#include "db/RedisPool.h"
#include "core/Logger.h"
#include "utils/Random.h"

#include <mysql/mysql.h>
#include <mutex>

namespace chat {

using json = nlohmann::json;

namespace {

// 默认一次拉多少条历史
constexpr int DEFAULT_HISTORY_LIMIT      = 50;
// 单次最多允许客户端要多少条
constexpr int MAX_HISTORY_LIMIT          = 200;
// 缓存基础 TTL（秒）
constexpr int HISTORY_CACHE_BASE_TTL     = 60;
// 在基础 TTL 上增加一个 0~30 秒的随机值，防止雪崩
constexpr int HISTORY_CACHE_JITTER       = 30;

// 简单互斥锁：防止缓存未命中时被大量并发打爆 DB
std::mutex g_historyMutex;

// 降级状态：true 表示 Redis 现在认为是“坏掉的”
std::atomic<bool> g_redisBroken{false};

// 当 Redis 坏掉时，用这个锁串行访问 DB，避免 DB 被大量并发压死
std::mutex g_fallbackDbMutex;

// 简单的“DB 回退 QPS”计数器
std::atomic<int> g_fallbackQps{0};
constexpr int MAX_FALLBACK_QPS = 50; // 每秒最多允许 50 次回退 DB

// 用 mysql_real_escape_string 做一下转义，防止聊天内容里的单引号把 SQL 搞坏,这个就是预防sql注入的
std::string escapeForSql(MYSQL* conn, const std::string& s) {
    if (!conn || s.empty()) return s;

    // mysql_real_escape_string 最多把长度扩大一倍 + 1
    std::string out;
    out.resize(s.size() * 2 + 1);

    unsigned long len = mysql_real_escape_string(
        conn,
        &out[0],
        s.c_str(),
        static_cast<unsigned long>(s.size())
    );
    out.resize(len);
    return out;
}

// 从 DB 里拉最近 limit 条历史消息
json loadHistoryFromDB(int roomId, int limit) {
    json history = json::array();

    auto dbConn = DBPool::Instance().getConnection();
    if (!dbConn) {
        LOG_ERROR("[ChatHistory::loadHistoryFromDB] no db connection");
        return history;
    }

    if (limit <= 0) limit = DEFAULT_HISTORY_LIMIT;
    if (limit > MAX_HISTORY_LIMIT) limit = MAX_HISTORY_LIMIT;

    std::string sql =
        "SELECT id, user_id, username, content, UNIX_TIMESTAMP(created_at) "
        "FROM messages "
        "WHERE room_id = " + std::to_string(roomId) + " "
        "ORDER BY id DESC "
        "LIMIT " + std::to_string(limit);

    MYSQL_RES* res = dbConn->query(sql);
    if (!res) {
        LOG_ERROR("[ChatHistory::loadHistoryFromDB] query failed, sql=" << sql);
        return history;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        json item;
        // id
        item["id"]       = row[0] ? std::atoi(row[0]) : 0;
        // user_id
        item["fromId"]   = row[1] ? std::atoi(row[1]) : 0;
        // username
        item["fromName"] = row[2] ? row[2] : "";
        // content
        item["text"]     = row[3] ? row[3] : "";
        // created_at 时间戳（秒）
        long long ts     = row[4] ? std::atoll(row[4]) : 0;
        item["ts"]       = ts;
        // 房间号
        item["roomId"]   = roomId;

        history.push_back(std::move(item));
    }

    mysql_free_result(res);

    // 现在 history 是“最新在前”
    // std::reverse(history.begin(), history.end());

    return history;
}
} // anonymous namespace
// 把一条聊天消息写入 messages 表
void SaveMessage(int roomId,
                 int userId,
                 const std::string& username,
                 const std::string& text)
{
    auto dbConn = DBPool::Instance().getConnection();
    if (!dbConn) {
        LOG_ERROR("[ChatHistory::SaveMessage] no db connection");
        return;
    }

    MYSQL* raw = dbConn->raw();
    std::string escName = escapeForSql(raw, username);
    std::string escText = escapeForSql(raw, text);

    std::string sql =
        "INSERT INTO messages(room_id, user_id, username, content) VALUES(" +
        std::to_string(roomId) + ", " +
        std::to_string(userId) + ", '" +
        escName + "', '" + escText + "')";

    if (!dbConn->update(sql)) {
        LOG_ERROR("[ChatHistory::SaveMessage] update failed, sql=" << sql);
    }
}

// 带缓存 + 互斥锁防缓存击穿的历史消息获取
bool GetHistoryWithCache(int roomId,
                         int limit,
                         json& historyOut)
{
    if (roomId <= 0) roomId = 1;
    if (limit <= 0)  limit  = DEFAULT_HISTORY_LIMIT;
    if (limit > MAX_HISTORY_LIMIT) limit = MAX_HISTORY_LIMIT;

    auto redisConn = RedisPool::Instance().getConnection();
    std::string cacheKey =
        "room:history:" + std::to_string(roomId) + ":" + std::to_string(limit);

    // 1) Redis 可用，先尝试直接读缓存
    if (redisConn) {
        std::string cached;
        if (redisConn->get(cacheKey, cached)) {
            try {
                historyOut = json::parse(cached);
                return true;
            } catch (const std::exception& e) {
                LOG_ERROR("[ChatHistory::GetHistoryWithCache] parse redis json fail: "
                          << e.what());
            }
        }

        // 2) 缓存 MISS → 加锁防缓存击穿
        {
            std::unique_lock<std::mutex> lk(g_historyMutex);

            // 2.1 double-check：锁内再查一次缓存，防止别的线程刚刚填好了
            std::string cached2;
            if (redisConn->get(cacheKey, cached2)) {
                try {
                    historyOut = json::parse(cached2);
                    return true;
                } catch (const std::exception& e) {
                    LOG_ERROR("[ChatHistory::GetHistoryWithCache] parse redis json (2) fail: "
                              << e.what());
                }
            }

            // 2.2 仍然没有 → 打 DB
            historyOut = loadHistoryFromDB(roomId, limit);

            // 2.3 写回 Redis，加随机 TTL 防雪崩
            try {
                int ttl = HISTORY_CACHE_BASE_TTL
                          + utils::RandInt(0, HISTORY_CACHE_JITTER);
                redisConn->setEX(cacheKey, historyOut.dump(), ttl);
            } catch (const std::exception& e) {
                LOG_ERROR("[ChatHistory::GetHistoryWithCache] set redis cache fail: "
                          << e.what());
            }

            return true;
        }
    }

    //3) Redis 不可用：进入“降级模式”
    g_redisBroken.store(true);
    LOG_WARN("[ChatHistory::GetHistoryWithCache] redis not available, fallback DB only");

    // 3.1 简单限流：如果当前回退请求数太多，直接拒绝，避免 DB 被打爆
    int cur = g_fallbackQps.fetch_add(1);
    if (cur > MAX_FALLBACK_QPS) {
        g_fallbackQps.fetch_sub(1);
        LOG_WARN("[ChatHistory::GetHistoryWithCache] fallback DB qps too high, reject");
        return false;  // 上层会返回 get history failed
    }

    // 3.2 串行访问 DB：所有回退请求都排队访问 DB
    {
        std::lock_guard<std::mutex> lock(g_fallbackDbMutex);
        historyOut = loadHistoryFromDB(roomId, limit);
    }

    g_fallbackQps.fetch_sub(1);
    return true;    
}

} // namespace chat
