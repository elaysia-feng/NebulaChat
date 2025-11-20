#include "chat/AuthService.h"
#include "db/DBpool.h"
#include "db/RedisPool.h"
#include "core/Logger.h"
#include "utils/Random.h"
#include "utils/UserCacheVal.h"
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace utils;

// ===================== 用户名 + 密码登录（带 Redis 缓存 + 空值缓存） =====================
//
// 整体流程：
// 1）先走 Redis：
//     - 命中 "null"        → 之前确认过“用户不存在”，直接失败，防止穿透 DB。
//     - 命中正常 JSON 缓存 → 校验密码。如果密码匹配，直接返回成功；密码不匹配则继续查 DB。
// 2）Redis 没命中 / 解析失败 / 密码不匹配 → 查 MySQL。
// 3）MySQL 查不到用户       → 在 Redis 写一个短期 null 缓存（防止穿透），返回失败。
// 4）MySQL 查到但密码错误   → 返回失败，不写缓存（简单策略）。
// 5）MySQL 查到且密码正确   → 登录成功，同时写回 Redis 缓存（带随机 TTL 防止雪崩）。
//
bool AuthService::login(const std::string& user,
                        const std::string& pass,
                        int&               userId)
{
    // Redis key 规范：用用户名做 key
    std::string key = "user:name:" + user;

    // 1) 先尝试走 Redis 缓存，减轻 DB 压力
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        std::string cached;

        // 1.1 Redis 里若存在这个 key，取出缓存内容（可能是 "null" 或正常 JSON）
        if (redisConn->get(key, cached)) {
            // ======= 1.1.1 命中 "null"：表示以前查过，这个用户不存在，直接失败 =======
            if (cached == "null") {
                LOG_INFO("[AuthService::login] cache hit null for user=" << user);
                return false;   // 之前已经确认这个用户不存在
            }

            // ======= 1.1.2 命中正常 JSON 缓存 =======
            try {
                // 期望格式：{"id":123, "username":"Elias", "password":"1234"}
                json j = json::parse(cached);
                std::string cachedPass = j.value("password", "");
                int         cachedId   = j.value("id", 0);

                // 1.1.2.1 如果 id 有效且密码匹配，说明是一个完整命中，直接返回成功
                if (cachedId > 0 && cachedPass == pass) {
                    userId = cachedId;
                    LOG_INFO("[AuthService::login] cache hit, user=" << user
                             << " id=" << userId);
                    return true;    // 完全命中缓存
                }
                else {
                    // 1.1.2.2 有该用户记录，但密码不一致（可能用户改过密码或者缓存旧了）
                    //        为了严谨，继续走 DB 再查一次，以 DB 结果为准。
                    LOG_WARN("[AuthService::login] cache hit but password mismatch, user=" << user);
                }

            } catch (const std::exception& e) {
                // JSON 解析异常，可能是缓存写坏了或格式不对，当作没命中
                LOG_ERROR("[AuthService::login] parse cache fail, user=" << user
                          << " err=" << e.what());
                // 当作没命中，继续查数据库
            }
        }
        // 如果 get 返回 false → Redis 里没有这个 key，当作缓存未命中
    } else {
        // 拿不到 Redis 连接，说明 Redis 目前不可用，只能纯 DB 兜底
        LOG_WARN("[AuthService::login] redis not available, fallback to DB only");
    }

    // 2) 缓存未命中 / 解析失败 / 密码不匹配 → 查 MySQL
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::login] ERROR: no db connection");
        return false;
    }

    // 从 DB 里把 id 和 password 一起查出来
    std::string sql =
        "SELECT id, password FROM users "
        "WHERE username = '" + user + "' "
        "LIMIT 1";
    
    LOG_DEBUG("[AuthService::login] SQL = " << sql);

    MYSQL_RES* res = conn->query(sql);
    if (!res) {
        LOG_ERROR("[AuthService::login] query failed");
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        // 2.1 数据库都没这个用户 → 可以设置一个 "null" 空缓存，防止用户不存在时被疯狂打 DB
        LOG_WARN("[AuthService::login] no such user, user=" << user);
        mysql_free_result(res);

        if (redisConn) {
            int baseTTL   = 60;                          // 基础 60 秒
            int randDelta = utils::RandInt(0, 600);      // 0~600 秒随机，避免雪崩
            int ttl       = baseTTL + randDelta;
            redisConn->setEX(key, "null", ttl);
            LOG_INFO("[AuthService::login] set null cache for user=" << user);
        }
        return false;
    }

    // 从结果中拿出 id 和 password
    int         dbId   = std::stoi(row[0]);
    std::string dbPass = row[1] ? row[1] : "";
    
    mysql_free_result(res);

    // 2.2 用户存在，但密码不对 → 登录失败，不写缓存（简单版本）
    //     真正生产环境可能会考虑做错误次数限制等，这里先保持简单。
    if (dbPass != pass) {
        LOG_WARN("[AuthService::login] wrong password for user=" << user);
        return false;
    }

    // 2.3 用户存在且密码正确 → 登录成功
    userId = dbId;
    LOG_INFO("[AuthService::login] user " << user
             << " login success, id = " << userId);

    // 3) 登录成功后，把用户信息写入 Redis 缓存，方便下次登录直接命中
    if (redisConn) {
        try {
            json j;
            j["id"]       = userId;
            j["username"] = user;
            j["password"] = pass;   // ⚠ 示例，生产环境不要缓存明文密码，以后可以改成散列+盐

            int baseTTL   = 3600;                     // 基础 1 小时
            int randDelta = utils::RandInt(0, 600);   // 0~600 秒
            int ttl       = baseTTL + randDelta;

            redisConn->setEX(key, j.dump(), ttl);
            LOG_INFO("[AuthService::login] set cache for user=" << user);
        } catch (const std::exception& e) {
            LOG_ERROR("[AuthService::login] build cache json fail, user=" << user
                      << " err=" << e.what());
        }
    }
    return true;
}    


// ===================== 注册（成功后预热 Redis + 本地缓存） =====================
//
// 流程：
// 1）查手机号是否已被注册。
// 2）查用户名是否已被注册。
// 3）插入新用户（phone + username + password）。
// 4）再查一次 id（拿到自增主键）。
// 5）写入 Redis 三份缓存：user:name: / user:pass: / user:phone:（方便多种查询方式）。
// 6）写入本地进程内缓存（按 phone 索引），方便手机号登录时直接命中。
//
bool AuthService::Register(const std::string& phone,
                           const std::string& user,
                           const std::string& pass,
                           int&               userId)
{
    // 先取一个 DB 连接
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::Register] ERROR: no db connection");
        return false;
    }

    // 尝试提前拿一个 Redis 连接，用于后续写缓存（可用就用，不可用就忽略）
    auto RedisConn = RedisPool::Instance().getConnection();

    // 1. 检查手机号是否已注册
    std::string check_phone_sql =
        "SELECT id FROM users "
        "WHERE phone = '" + phone + "' "
        "LIMIT 1";

    LOG_DEBUG("[AuthService::Register] check phone SQL = " << check_phone_sql);

    if (MYSQL_RES* res = conn->query(check_phone_sql)) {
        if (MYSQL_ROW row = mysql_fetch_row(res)) {
            // 能查到记录，说明手机号已经被占用
            LOG_WARN("[AuthService::Register] phone already exists: " << phone);
            mysql_free_result(res);
            return false;
        }
        mysql_free_result(res);
    }

    // 2. 检查用户名是否已存在
    std::string check_user_sql =
        "SELECT id FROM users "
        "WHERE username = '" + user + "' "
        "LIMIT 1";

    LOG_DEBUG("[AuthService::Register] check user SQL = " << check_user_sql);

    if (MYSQL_RES* res = conn->query(check_user_sql)) {
        if (MYSQL_ROW row = mysql_fetch_row(res)) {
            // 用户名已经被别人占用
            LOG_WARN("[AuthService::Register] username already exists: " << user);
            mysql_free_result(res);
            return false;
        }
        mysql_free_result(res);
    }

    // 3. 插入新用户（phone + username + password）
    std::string insert_sql =
        "INSERT INTO users(phone, username, password) "
        "VALUES('" + phone + "', '" + user + "', '" + pass + "')";

    LOG_DEBUG("[AuthService::Register] insert SQL = " << insert_sql);

    if (!conn->update(insert_sql)) {
        LOG_ERROR("[AuthService::Register] insert error");
        return false;
    }

    // 4. 再查一次：把新用户的 id 查出来
    std::string select_sql =
        "SELECT id FROM users "
        "WHERE phone = '" + phone + "' "
        "LIMIT 1";

    LOG_DEBUG("[AuthService::Register] select SQL = " << select_sql);

    MYSQL_RES* id_res = conn->query(select_sql);
    if (!id_res) {
        LOG_ERROR("[AuthService::Register] select id failed");
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(id_res);
    if (!row) {
        LOG_ERROR("[AuthService::Register] cannot fetch id after insert");
        mysql_free_result(id_res);
        return false;
    }

    userId = std::stoi(row[0]);
    LOG_INFO("[AuthService::Register] register success, user=" << user
             << ", phone=" << phone << ", id=" << userId);

    mysql_free_result(id_res);

    // 5. 预热 Redis 缓存：用户名 / 密码 / 手机号 三个方向都写一份
    if (RedisConn) {
        try {
            json j;
            j["id"]       = userId;
            j["username"] = user;
            j["phone"]    = phone;
            j["password"] = pass;   // 同样：简单示例，后续可以改为加密/哈希

            std::string v = j.dump();

            int baseTTL   = 3600;                     // 1 小时
            int randDelta = utils::RandInt(0, 600);   // 0~600 秒随机
            int ttl       = baseTTL + randDelta;

            // 根据用户名、密码、手机号分别建缓存，方便后续多种查询方式
            RedisConn->setEX("user:name:"  + user,  v, ttl);
            RedisConn->setEX("user:pass:"  + pass,  v, ttl);   // 现在暂时没用到，可留备
            RedisConn->setEX("user:phone:" + phone, v, ttl);

            LOG_INFO("[AuthService::Register] warm cache for user=" << user
                     << " phone=" << phone);
        } catch (const std::exception& e) {
            LOG_ERROR("[AuthService::Register] build cache json fail, user=" << user
                      << " err=" << e.what());
        }
    }

    // 6. 注册成功后，在本地小缓存里也写一份（按 phone 索引）
    //    方便 loginByPhone 时先命中进程内缓存。
    g_localUserCacheByPhone.put(phone, userId, user);

    return true;
}


// ===================== 手机号登录（多级缓存：本地缓存 → Redis → DB） =====================
//
// 优先级：
// 0）本地小缓存（进程内 LRU 或 TTL 缓存）
// 1）Redis 缓存
// 2）Redis 标记为 down 时，打 DB 前先走 QPS 限流器（防止 Redis 挂掉时 DB 被打爆）
// 3）DB 查询成功后，再回写 Redis + 本地缓存
//
bool AuthService::loginByPhone(const std::string& phone,
                               int&               userId,
                               std::string&       usernameOut)
{
    // ========== 0) 先查本地进程内的小缓存 ==========
    // 命中本地缓存：避免每次手机号登录都去 Redis / DB。
    if (g_localUserCacheByPhone.get(phone, userId, usernameOut)) {
        LOG_INFO("[AuthService::loginByPhone] local cache hit, phone=" << phone
                 << " id=" << userId << " username=" << usernameOut);
        return true;
    }

    // Redis 的 key 统一规范：user:phone:<phone>
    std::string key = "user:phone:" + phone;

    // ========== 1) 再查 Redis ==========
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        std::string cached;
        if (redisConn->get(key, cached)) {
            // 1.1 命中空缓存 "null" → 之前已经查过 DB，确定手机号不存在
            if (cached == "null") {
                LOG_INFO("[AuthService::loginByPhone] redis cache null, phone=" << phone);
                return false;
            }

            // 1.2 命中正常缓存 JSON → 解析拿出 id + username
            try {
                json j = json::parse(cached);
                int         cachedId   = j.value("id", 0);
                std::string cachedName = j.value("username", "");

                if (cachedId > 0) {
                    userId      = cachedId;
                    usernameOut = cachedName;

                    LOG_INFO("[AuthService::loginByPhone] redis cache hit, phone=" << phone
                             << " id=" << userId
                             << " username=" << usernameOut);

                    // 命中 Redis 时，顺带写一份到本地缓存，下一次可以更快
                    g_localUserCacheByPhone.put(phone, userId, usernameOut);
                    return true;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("[AuthService::loginByPhone] parse redis json fail, phone=" << phone
                          << " err=" << e.what());
            }
        }
        // Redis 中没有这个 key，当作没命中
    } else {
        // Redis 暂不可用，此时只能走 DB，最好加 QPS 限流避免被打爆
        LOG_WARN("[AuthService::loginByPhone] redis not available, fallback to DB");
    }

    // ========== 2) Redis 没命中 / Redis 挂掉 → 打 DB 前先做限流（保护 MySQL） ==========
    if (RedisPool::IsDown()) {
        // RedisPool::IsDown() 表示系统认为 Redis 当前不可用
        // g_loginByPhoneLimiter 是一个 QPS 限流器
        if (!g_loginByPhoneLimiter.allow()) {
            LOG_WARN("[AuthService::loginByPhone] system busy, reject by QPS limiter, phone="
                     << phone);
            // 上层可以返回“系统繁忙，请稍后再试”
            return false;
        }
    }

    // 真正去 MySQL 查
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::loginByPhone] ERROR: no db connection");
        return false;
    }

    std::string sql =
        "SELECT id, username FROM users "
        "WHERE phone = '" + phone + "' "
        "LIMIT 1";

    LOG_DEBUG("[AuthService::loginByPhone] SQL = " << sql);

    MYSQL_RES* res = conn->query(sql);
    if (!res) {
        LOG_ERROR("[AuthService::loginByPhone] query failed");
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        // 2.1 数据库里也没有这个手机号 → 写一份短期 null 缓存，防穿透
        LOG_WARN("[AuthService::loginByPhone] no such phone: " << phone);
        mysql_free_result(res);

        if (redisConn) {
            int baseTTL   = 60;
            int randDelta = utils::RandInt(0, 60); // 0~60 秒
            int ttl       = baseTTL + randDelta;
            redisConn->setEX(key, "null", ttl);
            LOG_INFO("[AuthService::loginByPhone] set null cache for phone=" << phone);
        }
        return false;
    }

    // 从 DB 结果里拿出 id + username
    userId = std::stoi(row[0]);
    if (row[1]) {
        usernameOut = row[1];
    } else {
        usernameOut.clear();
    }

    LOG_INFO("[AuthService::loginByPhone] phone=" << phone
             << " login success, id=" << userId
             << ", username=" << usernameOut);

    mysql_free_result(res);

    // ========== 3) 回写 Redis + 本地缓存（加速后续请求） ==========
    if (redisConn) {
        try {
            json j;
            j["id"]       = userId;
            j["username"] = usernameOut;
            j["phone"]    = phone;

            int baseTTL   = 3600;
            int randDelta = utils::RandInt(0, 600); // 0~600 秒
            int ttl       = baseTTL + randDelta;

            redisConn->setEX(key, j.dump(), ttl);
            LOG_INFO("[AuthService::loginByPhone] set cache for phone=" << phone);
        } catch (const std::exception& e) {
            LOG_ERROR("[AuthService::loginByPhone] build redis json fail, phone=" << phone
                      << " err=" << e.what());
        }
    }

    // 不管 Redis 成不成功，本地缓存一定写一份
    g_localUserCacheByPhone.put(phone, userId, usernameOut);

    return true;
}


// ===================== 修改用户名（同步更新 Redis + 本地缓存） =====================
//
// 流程：
// 1）根据 uid 查出当前 username + phone（为了删旧缓存和重建新缓存）。
// 2）检查 newName 是否被其他用户占用。
// 3）更新 DB 中的 username 字段。
// 4）删除与旧用户名相关的 Redis 缓存，并重建新的 phone 缓存、删除 user:id 缓存。
// 5）更新本地手机号缓存（删除旧的 → 写入新用户名）。
//
bool AuthService::updateUsername(int userId,
                                 const std::string& newName,
                                 std::string&       oldNameOut,
                                 std::string&       phoneOut)
{
    if (newName.empty()) {
        LOG_WARN("[AuthService::updateUsername] newName empty, uid=" << userId);
        return false;
    }

    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::updateUsername] no db connection");
        return false;
    }

    // 1. 先查出当前用户名 + 手机号（为后续更新缓存做准备）
    std::string query_sql =
        "SELECT username, phone FROM users "
        "WHERE id = " + std::to_string(userId) + " LIMIT 1";
    
    MYSQL_RES* res = conn->query(query_sql);
    if (!res) {
        LOG_ERROR("[AuthService::updateUsername] query current user failed");
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        LOG_WARN("[AuthService::updateUsername] user not exist, id=" << userId);
        mysql_free_result(res);
        return false;
    }

    // SELECT username, phone → row[0] 是 username, row[1] 是 phone
    oldNameOut = row[0] ? row[0] : "";
    phoneOut   = row[1] ? row[1] : "";
    mysql_free_result(res);

    // 2. 检查新用户名是否已被其他用户占用
    std::string check_sql =
        "SELECT id FROM users "
        "WHERE username = '" + newName + "' "
        "AND id <> " + std::to_string(userId) +
        " LIMIT 1";

    if (MYSQL_RES* ck = conn->query(check_sql)) {
        if (MYSQL_ROW r = mysql_fetch_row(ck)) {
            LOG_WARN("[AuthService::updateUsername] newName already used: " << newName);
            mysql_free_result(ck);
            return false;
        }
        mysql_free_result(ck);
    }

    // 3. 更新数据库中的用户名
    std::string update_sql =
        "UPDATE users SET username = '" + newName + "' "
        "WHERE id = " + std::to_string(userId);

    if (!conn->update(update_sql)) {
        LOG_ERROR("[AuthService::updateUsername] update sql failed");
        return false;
    }

    LOG_INFO("[AuthService::updateUsername] uid=" << userId
             << " oldName=" << oldNameOut
             << " newName=" << newName);

    // 4. 尝试更新 Redis 缓存（删旧、重建新）
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        // 4.1 删掉旧的基于用户名的缓存
        if (!oldNameOut.empty()){
            redisConn->del("user:name:" + oldNameOut);
        }

        if (!phoneOut.empty()) {
            // 删除旧的 phone 缓存
            redisConn->del("user:phone:" + phoneOut);

            // 重建一份新的 phone → (id, newName) 缓存，避免手机号登录时拿到旧用户名
            try {
                json j;
                j["id"]       = userId;
                j["username"] = newName;
                j["phone"]    = phoneOut;

                int baseTTL   = 3600;
                int randDelta = utils::RandInt(0, 600);
                int ttl       = baseTTL + randDelta;
                redisConn->setEX("user:phone:" + phoneOut, j.dump(), ttl);
            } catch (const std::exception& e) {
                LOG_ERROR("[AuthService::updateUsername] rebuild phone cache fail, err="
                          << e.what());
            }
        }

        // 有些地方可能会通过 user:id:<id> 查用户，也顺路删掉这个缓存
        redisConn->del("user:id:" + std::to_string(userId));
    }

    // 5. 本地缓存同步更新：删掉旧的 phone 缓存，再写一份新的（phone → userId + newName）
    if (!phoneOut.empty()) {
        g_localUserCacheByPhone.erase(phoneOut);
        g_localUserCacheByPhone.put(phoneOut, userId, newName);
    }

    return true;
}


// ===================== 通过手机号重置密码 =====================
//
// 流程：
// 1）根据 phone 查出 userId + username。
// 2）更新数据库中的 password（示例中用明文，后续可改为加密）。
// 3）删除与该用户相关的所有缓存（用户名 / 手机号 / user:id）。
// 4）删除本地手机号缓存（因为密码变更，相关登录信息要重新走 DB/Redis）。
//
bool AuthService::resetPasswordByPhone(const std::string& phone,
                                       const std::string& newPass)
{
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::resetPasswordByPhone] no db connection");
        return false;
    }

    // 1. 先根据手机号查出用户 id + username
    std::string query_sql =
        "SELECT id, username FROM users "
        "WHERE phone = '" + phone + "' "
        "LIMIT 1";

    MYSQL_RES* res = conn->query(query_sql);
    if (!res) {
        LOG_ERROR("[AuthService::resetPasswordByPhone] query failed");
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        LOG_WARN("[AuthService::resetPasswordByPhone] phone not found: " << phone);
        mysql_free_result(res);
        return false;
    }

    int         userId   = std::stoi(row[0]);
    std::string username = row[1] ? row[1] : "";
    mysql_free_result(res);

    // 2. 更新密码（这里暂时用明文，后续可改为 hash）
    std::string update_sql =
        "UPDATE users SET password = '" + newPass + "' "
        "WHERE id = " + std::to_string(userId);

    if (!conn->update(update_sql)) {
        LOG_ERROR("[AuthService::resetPasswordByPhone] update failed");
        return false;
    }

    LOG_INFO("[AuthService::resetPasswordByPhone] reset password, uid=" << userId
             << " phone=" << phone);

    // 3. 尝试删除 Redis 里的用户相关缓存
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        if (!username.empty()) {
            redisConn->del("user:name:" + username);
        }
        redisConn->del("user:phone:" + phone);
        redisConn->del("user:id:" + std::to_string(userId));
    }

    // 4. 修改密码后，本地手机号缓存也失效，删除之
    g_localUserCacheByPhone.erase(phone);

    return true;
}
