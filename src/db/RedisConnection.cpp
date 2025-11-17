#include "db/RedisConnection.h"
#include "core/Logger.h"

RedisConnection::RedisConnection()
    : ctx_(nullptr)
{
}

RedisConnection::~RedisConnection()
{
    if (ctx_) {
        LOG_INFO("[RedisConnection::~RedisConnection] closing redis connection, ctx=" << ctx_);
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

bool RedisConnection::connect(const std::string& host, int port)
{
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }

    ctx_ = redisConnect(host.c_str(), port);
    if (!ctx_) {
        LOG_ERROR("[RedisConnection::connect] redisConnect returned nullptr");
        return false;
    }
    if (ctx_->err) {
        LOG_ERROR("[RedisConnection::connect] redisConnect error: "
                  << ctx_->errstr);
        redisFree(ctx_);
        ctx_ = nullptr;
        return false;
    }

    LOG_INFO("[RedisConnection::connect] connect OK, host=" << host
             << " port=" << port << " ctx=" << ctx_);
    return true;
}

bool RedisConnection::set(const std::string& key, const std::string& value)
{
    if (!ctx_) {
        LOG_ERROR("[RedisConnection::set] ctx_ is null");
        return false;
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SET %s %s", key.c_str(), value.c_str()));
    if (!reply) {
        LOG_ERROR("[RedisConnection::set] redisCommand returned nullptr");
        return false;
    }

    bool ok = false;
    if (reply->type == REDIS_REPLY_STATUS && reply->str &&
        std::string(reply->str) == "OK") {
        ok = true;
    } else {
        LOG_ERROR("[RedisConnection::set] unexpected reply type=" << reply->type);
    }

    freeReplyObject(reply);
    return ok;
}

bool RedisConnection::setEX(const std::string& key,
                            const std::string& value,
                            int               seconds)
{
    if (!ctx_) {
        LOG_ERROR("[RedisConnection::setEX] ctx_ is null");
        return false;
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SET %s %s EX %d",
                     key.c_str(), value.c_str(), seconds));
    if (!reply) {
        LOG_ERROR("[RedisConnection::setEX] redisCommand returned nullptr");
        return false;
    }

    bool ok = false;
    if (reply->type == REDIS_REPLY_STATUS && reply->str &&
        std::string(reply->str) == "OK") {
        ok = true;
    } else {
        LOG_ERROR("[RedisConnection::setEX] unexpected reply type=" << reply->type);
    }

    freeReplyObject(reply);
    return ok;
}

bool RedisConnection::get(const std::string& key, std::string& out)
{
    if (!ctx_) {
        LOG_ERROR("[RedisConnection::get] ctx_ is null");
        return false;
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "GET %s", key.c_str()));
    if (!reply) {
        LOG_ERROR("[RedisConnection::get] redisCommand returned nullptr");
        return false;
    }

    bool ok = false;
    if (reply->type == REDIS_REPLY_NIL) {
        // key 不存在
        LOG_INFO("[RedisConnection::get] key not exist: " << key);
        ok = false;
    } else if (reply->type == REDIS_REPLY_STRING) {
        out.assign(reply->str, reply->len);
        ok = true;
    } else {
        LOG_ERROR("[RedisConnection::get] unexpected reply type=" << reply->type);
    }

    freeReplyObject(reply);
    return ok;
}

bool RedisConnection::del(const std::string& key)
{
    if (!ctx_) {
        LOG_ERROR("[RedisConnection::del] ctx_ is null");
        return false;
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "DEL %s", key.c_str()));
    if (!reply) {
        LOG_ERROR("[RedisConnection::del] redisCommand returned nullptr");
        return false;
    }

    bool ok = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        // 影响行数 >=1 认为删除成功
        ok = (reply->integer >= 1);
    } else {
        LOG_ERROR("[RedisConnection::del] unexpected reply type=" << reply->type);
    }

    freeReplyObject(reply);
    return ok;
}
