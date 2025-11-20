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

// ===================== 用户名 + 密码登录（带缓存） =====================


bool AuthService::login(const std::string& user,
                        const std::string& pass,
                        int&               userId)
{
    std::string key = "user:name:" + user;

    // 1) 先尝试走 Redis 缓存
    auto redisConn = RedisPool::Instance().getConnection();
    if(redisConn) {
        std::string cached;

        if(redisConn->get(key, cached)) {
            // 命中缓存
            
            if (cached == "null") {
                LOG_INFO("[AuthService::login] cache hit null for user=" << user);
                return false;   // 之前查过，这个用户不存在
            }

            try {
                json j = json::parse(cached);
                std::string cachedPass = j.value("password", "");
                int         cachedId   = j.value("id", 0);

                if (cachedId > 0 && cachedPass == pass) {
                    userId = cachedId;
                    LOG_INFO("[AuthService::login] cache hit, user=" << user
                             << " id=" << userId);
                    return true;    // 完全命中缓存
                }
                else {
                    // 缓存里有这个用户，但密码不匹配 → 继续走 DB 做一次严谨校验
                    LOG_WARN("[AuthService::login] cache hit but password mismatch, user=" << user);
                }

            } catch (const std::exception& e) {
                LOG_ERROR("[AuthService::login] parse cache fail, user=" << user
                          << " err=" << e.what());
                // 当作没命中，继续查数据库
            }
        } 

    } else {
        LOG_WARN("[AuthService::login] redis not available, fallback to DB only");
    }

    // 2) 缓存未命中 / 解析失败 / 密码不匹配 → 查 MySQL
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::login] ERROR: no db connection");
        return false;
    }

     // 这里从 DB 里把 password 也一起查出来
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
    if(!row) {
        LOG_WARN("[AuthService::login] no such user, user=" << user);
        mysql_free_result(res);

        // 3) 数据库也没有 → 写一个短期的“空缓存”防止穿透
        if(redisConn) {
            int baseTTL   = 60;
            int randDelta = utils::RandInt(0, 600); // 0~60 秒
            int ttl       = baseTTL + randDelta;
            redisConn->setEX(key, "null", ttl);
            LOG_INFO("[AuthService::login] set null cache for user=" << user);
        }
        return false;
    }
    int         dbId   = std::stoi(row[0]);
    std::string dbPass = row[1] ? row[1] : "";
    
    mysql_free_result(res);
     if (dbPass != pass) {
        LOG_WARN("[AuthService::login] wrong password for user=" << user);
        // 不写缓存，让错误密码仍然走数据库（简单版本）
        return false;
    }

    userId = dbId;
    LOG_INFO("[AuthService::login] user " << user
             << " login success, id = " << userId);

    // 4) 登录成功 → 写回 Redis 缓存，下次就直接命中了
    if(redisConn) {
        try{
        json j;
        j["id"]       = userId;
        j["username"] = user;
        j["password"] = pass;   // ⚠ 示例，生产环境不要缓存明文密码,还没学到这里，以后处理
        
        int baseTTL   = 3600;
        int randDelta = utils::RandInt(0, 600); // 0~600 秒
        int ttl       = baseTTL + randDelta;
        redisConn->setEX(key, j.dump(), ttl); // 缓存 1 小时
        LOG_INFO("[AuthService::login] set cache for user=" << user);
    } catch (const std::exception& e) {
            LOG_ERROR("[AuthService::login] build cache json fail, user=" << user
                      << " err=" << e.what());
        }
    }
    return true;
}    

// ===================== 注册（成功后预热缓存） =====================
bool AuthService::Register(const std::string& phone,
                           const std::string& user,
                           const std::string& pass,
                           int&               userId)
{
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::Register] ERROR: no db connection");
        return false;
    }
    // 预先拿一个 redis 连接（可用的话，后面用来更新缓存）
    auto RedisConn = RedisPool::Instance().getConnection();


    // 1. 检查手机号是否已注册
    std::string check_phone_sql =
        "SELECT id FROM users "
        "WHERE phone = '" + phone + "' "
        "LIMIT 1";

    LOG_DEBUG("[AuthService::Register] check phone SQL = " << check_phone_sql);

    if (MYSQL_RES* res = conn->query(check_phone_sql)) {
        if (MYSQL_ROW row = mysql_fetch_row(res)) {
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

    // 4. 查询新用户 id
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

    // 5. 预热缓存（用户名 + 手机号两个方向都写一下）
       // 5. 预热缓存（用户名 + 手机号两个方向都写一下）
    if(RedisConn) {
        try{
            json j;
            j["id"]       = userId;
            j["username"] = user;
            j["phone"]    = phone;
            j["password"] = pass;

            std::string v = j.dump();

            int baseTTL   = 3600;
            int randDelta = utils::RandInt(0, 600); // 0~600 秒
            int ttl       = baseTTL + randDelta;
            RedisConn->setEX("user:name:"  + user,  v, ttl);
            RedisConn->setEX("user:pass:"  + pass,  v, ttl);
            RedisConn->setEX("user:phone:" + phone, v, ttl);

            LOG_INFO("[AuthService::Register] warm cache for user=" << user
                     << " phone=" << phone);
        } catch (const std::exception& e) {
            LOG_ERROR("[AuthService::Register] build cache json fail, user=" << user
                      << " err=" << e.what());
        }
    }

    // 注册成功后也在本地小缓存写一份，方便后面手机号登录直接命中
    g_localUserCacheByPhone.put(phone, userId, user);

    return true;
}


bool AuthService::loginByPhone(const std::string& phone,
                               int&               userId,
                               std::string&       usernameOut)
{
    // ========== 0) 先查本地进程内的小缓存 ==========
    if (g_localUserCacheByPhone.get(phone, userId, usernameOut)) {
        LOG_INFO("[AuthService::loginByPhone] local cache hit, phone=" << phone
                 << " id=" << userId << " username=" << usernameOut);
        return true;
    }

    // 统一 key 规范：user:phone:<phone>
    std::string key = "user:phone:" + phone;

    // ========== 1) 再查 Redis ==========
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        std::string cached;
        if (redisConn->get(key, cached)) {
            if (cached == "null") {
                LOG_INFO("[AuthService::loginByPhone] redis cache null, phone=" << phone);
                return false;
            }

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

                    // 命中 Redis 时，顺手写一份到本地缓存
                    g_localUserCacheByPhone.put(phone, userId, usernameOut);
                    return true;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("[AuthService::loginByPhone] parse redis json fail, phone=" << phone
                          << " err=" << e.what());
            }
        }
    } else {
        LOG_WARN("[AuthService::loginByPhone] redis not available, fallback to DB");
    }

    // ========== 2) Redis 没命中 → 要打 DB（Redis 挂了时做简单限流） ==========
    if (RedisPool::IsDown()) {
        if (!g_loginByPhoneLimiter.allow()) {
            LOG_WARN("[AuthService::loginByPhone] system busy, reject by QPS limiter, phone="
                     << phone);
            return false;  // 上层可以返回“系统繁忙”
        }
    }

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
        LOG_WARN("[AuthService::loginByPhone] no such phone: " << phone);
        mysql_free_result(res);

        // 2.1 写一个短期 null 缓存（防穿透）
        if (redisConn) {
            int baseTTL   = 60;
            int randDelta = utils::RandInt(0, 60); // 0~60 秒
            int ttl       = baseTTL + randDelta;
            redisConn->setEX(key, "null", ttl);
            LOG_INFO("[AuthService::loginByPhone] set null cache for phone=" << phone);
        }
        return false;
    }

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

    // ========== 3) 回写 Redis + 本地缓存 ==========
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

    // 不管 Redis 在不在，这里一定写一份到本地缓存
    g_localUserCacheByPhone.put(phone, userId, usernameOut);

    return true;
}


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

    // 1. 先查出当前用户名 + 手机号（为了删缓存）
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

    // 2. 检查新用户名是否已被别人占用
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

    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        if (!oldNameOut.empty()){
            redisConn->del("user:name:" + oldNameOut);
        }
        if(!phoneOut.empty()) {
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
        redisConn->del("user:id:" + std::to_string(userId));
    }

    // 本地缓存同步更新：删掉旧的，再写一份新的
    if (!phoneOut.empty()) {
        g_localUserCacheByPhone.erase(phoneOut);
        g_localUserCacheByPhone.put(phoneOut, userId, newName);
    }

    return true;
}


bool AuthService::resetPasswordByPhone(const std::string& phone,
                                       const std::string& newPass)
{
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::resetPasswordByPhone] no db connection");
        return false;
    }

    // 1. 查出这个手机号对应的用户 id + username
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

    // 2. 更新密码（这里先用明文）
    std::string update_sql =
        "UPDATE users SET password = '" + newPass + "' "
        "WHERE id = " + std::to_string(userId);

    if (!conn->update(update_sql)) {
        LOG_ERROR("[AuthService::resetPasswordByPhone] update failed");
        return false;
    }

    LOG_INFO("[AuthService::resetPasswordByPhone] reset password, uid=" << userId
             << " phone=" << phone);

    // 3. 删除与这个用户相关的缓存
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        if (!username.empty()) {
            redisConn->del("user:name:" + username);
        }
        redisConn->del("user:phone:" + phone);
        redisConn->del("user:id:" + std::to_string(userId));
    }

    // 修改密码后，本地手机号缓存也失效
    g_localUserCacheByPhone.erase(phone);

    return true;
}
