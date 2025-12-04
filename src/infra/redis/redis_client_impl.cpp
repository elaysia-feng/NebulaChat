#include "infra/redis/redis_client_impl.h"
#include "core/Logger.h"
#include <hiredis/hiredis.h>

namespace infra::redis {

namespace {
long long parseIntegerReply(redisReply* reply) {
    if (!reply) return 0;
    if (reply->type == REDIS_REPLY_INTEGER) {
        return reply->integer;
    }
    if (reply->type == REDIS_REPLY_STRING && reply->str) {
        try {
            return std::stoll(reply->str);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}
}

void RedisClientImpl::set(std::string_view key,
                          std::string_view value,
                          std::optional<Seconds> ttl) {
    auto conn = getConn();
    if (!conn) {
        LOG_ERROR("[RedisClientImpl::set] no redis connection");
        return;
    }

    if (ttl.has_value()) {
        conn->setEX(std::string(key), std::string(value), static_cast<int>(ttl->count()));
    } else {
        conn->set(std::string(key), std::string(value));
    }
}

bool RedisClientImpl::setNxEx(std::string_view key,
                              std::string_view value,
                              Seconds ttlSeconds) {
    auto conn = getConn();
    if (!conn) {
        LOG_ERROR("[RedisClientImpl::setNxEx] no redis connection");
        return false;
    }

    redisContext* ctx = conn->raw();
    if (!ctx) return false;

    redisReply* reply = static_cast<redisReply*>(redisCommand(
        ctx,
        "SET %b %b NX EX %d",
        key.data(), static_cast<size_t>(key.size()),
        value.data(), static_cast<size_t>(value.size()),
        static_cast<int>(ttlSeconds.count())
    ));

    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_STATUS && reply->str &&
               std::string(reply->str) == "OK");
    freeReplyObject(reply);
    return ok;
}

std::optional<std::string> RedisClientImpl::get(std::string_view key) {
    auto conn = getConn();
    if (!conn) {
        LOG_ERROR("[RedisClientImpl::get] no redis connection");
        return std::nullopt;
    }

    std::string out;
    if (conn->get(std::string(key), out)) {
        return out;
    }
    return std::nullopt;
}

long long RedisClientImpl::del(std::string_view key) {
    auto conn = getConn();
    if (!conn) {
        LOG_ERROR("[RedisClientImpl::del] no redis connection");
        return 0;
    }

    redisContext* ctx = conn->raw();
    if (!ctx) return 0;

    redisReply* reply = static_cast<redisReply*>(redisCommand(
        ctx, "DEL %b", key.data(), static_cast<size_t>(key.size())
    ));
    if (!reply) return 0;

    long long ret = parseIntegerReply(reply);
    freeReplyObject(reply);
    return ret;
}

bool RedisClientImpl::expire(std::string_view key, Seconds ttl) {
    auto conn = getConn();
    if (!conn) {
        LOG_ERROR("[RedisClientImpl::expire] no redis connection");
        return false;
    }

    redisContext* ctx = conn->raw();
    if (!ctx) return false;

    redisReply* reply = static_cast<redisReply*>(redisCommand(
        ctx, "EXPIRE %b %d",
        key.data(), static_cast<size_t>(key.size()),
        static_cast<int>(ttl.count())
    ));
    if (!reply) return false;

    bool ok = parseIntegerReply(reply) == 1;
    freeReplyObject(reply);
    return ok;
}

long long RedisClientImpl::incrBy(std::string_view key, long long delta) {
    auto conn = getConn();
    if (!conn) {
        LOG_ERROR("[RedisClientImpl::incrBy] no redis connection");
        return 0;
    }

    redisContext* ctx = conn->raw();
    if (!ctx) return 0;

    redisReply* reply = static_cast<redisReply*>(redisCommand(
        ctx, "INCRBY %b %lld",
        key.data(), static_cast<size_t>(key.size()),
        static_cast<long long>(delta)
    ));
    if (!reply) return 0;

    long long val = parseIntegerReply(reply);
    freeReplyObject(reply);
    return val;
}

long long RedisClientImpl::eval(const std::string& script,
                                const std::vector<std::string>& keys,
                                const std::vector<std::string>& args) {
    auto conn = getConn();
    if (!conn) {
        LOG_ERROR("[RedisClientImpl::eval] no redis connection");
        return 0;
    }

    redisContext* ctx = conn->raw();
    if (!ctx) return 0;

    // 构造 argv：script, numkeys, keys..., args...
    std::vector<const char*> argv;
    std::vector<size_t>      argvlen;

    argv.push_back("EVAL");
    argvlen.push_back(4);

    argv.push_back(script.data());
    argvlen.push_back(script.size());

    std::string numKeysStr = std::to_string(keys.size());
    argv.push_back(numKeysStr.c_str());
    argvlen.push_back(numKeysStr.size());

    for (auto& k : keys) {
        argv.push_back(k.data());
        argvlen.push_back(k.size());
    }
    for (auto& a : args) {
        argv.push_back(a.data());
        argvlen.push_back(a.size());
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommandArgv(ctx, static_cast<int>(argv.size()), argv.data(), argvlen.data()));
    if (!reply) return 0;

    long long ret = parseIntegerReply(reply);
    freeReplyObject(reply);
    return ret;
}

} // namespace infra::redis
