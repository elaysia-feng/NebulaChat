#include "chat/AuthService.h"
#include "db/DBpool.h"
#include "db/RedisPool.h"
#include "core/Logger.h"
#include "utils/Random.h"
#include "utils/UserCacheVal.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace utils;

// ===================== 用户名 + 密码登录 =====================
//
// login 只做“校验密码 + 返回结果”
// 真正的缓存 + DB 逻辑由 loadUserByName 负责
//
bool AuthService::login(const std::string& user,
                        const std::string& pass,
                        int&               userId)
{
    std::string cachedPass;
    if (!loadUserByName(user, userId, cachedPass)) {
        // 用户不存在（本地 / Redis / DB 都确认没有）
        return false;
    }

    if (cachedPass != pass) {
        LOG_WARN("[AuthService::login] wrong password for user=" << user);
        return false;
    }

    LOG_INFO("[AuthService::login] user=" << user << " login success, id=" << userId);
    return true;
}


// ===================== 手机号登录 =====================
//
// loginByPhone 只做“按手机号查用户 + 返回 id/username”
// 真正多级缓存逻辑由 loadUserByPhone 负责
//
bool AuthService::loginByPhone(const std::string& phone,
                               int&               userId,
                               std::string&       usernameOut)
{
    // 以后可以在这里调用 sms_.verifyCode(phone, code) 做验证码校验
    // 当前版本只做“按手机号查用户信息”
    return loadUserByPhone(phone, userId, usernameOut);
}


// ===================== 注册 =====================
//
// 1）检查手机号是否已存在
// 2）检查用户名是否已存在
// 3）插入 DB
// 4）查回 id
// 5）写 Redis 缓存（user:name / user:pass / user:phone）
// 6）预热本地 L1 缓存（phone -> {id, username}）
//
bool AuthService::Register(const std::string& phone,
                           const std::string& user,
                           const std::string& pass,
                           int&               userId)
{
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::Register] no db connection");
        return false;
    }

    auto redisConn = RedisPool::Instance().getConnection();

    // 1. 检查手机号是否已存在
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

    // 3. 插入新用户
    std::string insert_sql =
        "INSERT INTO users(phone, username, password) "
        "VALUES('" + phone + "', '" + user + "', '" + pass + "')";

    LOG_DEBUG("[AuthService::Register] insert SQL = " << insert_sql);

    if (!conn->update(insert_sql)) {
        LOG_ERROR("[AuthService::Register] insert error");
        return false;
    }

    // 4. 再查一次 id
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
    mysql_free_result(id_res);

    LOG_INFO("[AuthService::Register] register success, user=" << user
             << ", phone=" << phone << ", id=" << userId);

    // 5. 预热 Redis 缓存
    if (redisConn) {
        try {
            json j;
            j["id"]       = userId;
            j["username"] = user;
            j["phone"]    = phone;
            j["password"] = pass;   // 示例；生产环境建议改为 hash

            std::string v   = j.dump();
            int ttl         = utils::MakeTtlWithJitter(3600, 600);

            redisConn->setEX("user:name:"  + user,  v, ttl);
            redisConn->setEX("user:pass:"  + pass,  v, ttl);   // 暂时未用，可预留
            redisConn->setEX("user:phone:" + phone, v, ttl);

            LOG_INFO("[AuthService::Register] warm cache for user=" << user
                     << " phone=" << phone);
        } catch (const std::exception& e) {
            LOG_ERROR("[AuthService::Register] build cache json fail, user=" << user
                      << " err=" << e.what());
        }
    }

    // 6. 本地 L1 缓存预热（方便 loginByPhone）
    warmUserCacheByPhone(userId, phone, user);

    return true;
}


// ===================== 修改用户名 =====================
bool AuthService::updateUsername(int                userId,
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

    // 1. 查当前 username + phone
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

    oldNameOut = row[0] ? row[0] : "";
    phoneOut   = row[1] ? row[1] : "";
    mysql_free_result(res);

    // 2. 检查新用户名是否被其他用户占用
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

    // 3. 更新数据库
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

    // 4. 失效缓存
    invalidateUserCacheByName(oldNameOut);
    invalidateUserCacheByPhone(phoneOut);

    // 同时可重建基于手机号的缓存（username 已经变更）
    warmUserCacheByPhone(userId, phoneOut, newName);

    // 可以顺便重建 user:name:<newName> 缓存（可选）
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        try {
            json j;
            j["id"]       = userId;
            j["username"] = newName;
            j["phone"]    = phoneOut;

            int ttl = utils::MakeTtlWithJitter(3600, 600);
            redisConn->setEX("user:name:" + newName, j.dump(), ttl);
        } catch (const std::exception& e) {
            LOG_ERROR("[AuthService::updateUsername] rebuild user:name cache fail, err="
                      << e.what());
        }
    }

    return true;
}


// ===================== 通过手机号重置密码 =====================
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

    // 2. 更新密码
    std::string update_sql =
        "UPDATE users SET password = '" + newPass + "' "
        "WHERE id = " + std::to_string(userId);

    if (!conn->update(update_sql)) {
        LOG_ERROR("[AuthService::resetPasswordByPhone] update failed");
        return false;
    }

    LOG_INFO("[AuthService::resetPasswordByPhone] reset password, uid=" << userId
             << " phone=" << phone);

    // 3. 删除用户名 & 手机号相关缓存
    invalidateUserCacheByName(username);
    invalidateUserCacheByPhone(phone);

    // 删除 user:id:<id> 缓存（如果有）
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        redisConn->del("user:id:" + std::to_string(userId));
    }

    return true;
}


// ===================== helper：按用户名加载用户认证信息 =====================
bool AuthService::loadUserByName(const std::string& username,
                                 int&               idOut,
                                 std::string&       passHashOut)
{
    // 0) 本地 L1
    bool isNull = false;
    if (g_localUserByName.get(username, idOut, passHashOut, isNull)) {
        if (isNull) {
            LOG_INFO("[loadUserByName] local null, user=" << username);
            return false;
        }
        LOG_INFO("[loadUserByName] local hit, user=" << username << " id=" << idOut);
        return true;
    }

    std::string key = "user:name:" + username;

    // 1) Redis
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        std::string cached;
        if (redisConn->get(key, cached)) {
            if (cached == "null") {
                LOG_INFO("[loadUserByName] redis null, user=" << username);
                g_localUserByName.putNull(username);
                return false;
            }

            try {
                json j         = json::parse(cached);
                int  cachedId  = j.value("id", 0);
                std::string pw = j.value("password", "");

                if (cachedId > 0) {
                    idOut       = cachedId;
                    passHashOut = pw;

                    LOG_INFO("[loadUserByName] redis hit, user=" << username
                             << " id=" << idOut);

                    g_localUserByName.put(username, idOut, passHashOut);
                    return true;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("[loadUserByName] parse redis json fail, user=" << username
                          << " err=" << e.what());
            }
        }
    } else {
        LOG_WARN("[loadUserByName] redis not available, use DB only");
    }

    if (RedisPool::IsDown()) {
        if (!g_loginLimiter.allow()) {
            LOG_WARN("[loadUserName] reject by QPS limiter, username=" << username);
            return false;
        }
    }
    // 2) DB
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[loadUserByName] no db connection");
        return false;
    }

    std::string query_sql =
        "SELECT id, password FROM users "
        "WHERE username = '" + username + "' "
        "LIMIT 1";

    MYSQL_RES* res = conn->query(query_sql);
    if (!res) {
        LOG_ERROR("[loadUserByName] query failed, user=" << username);
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        LOG_INFO("[loadUserByName] user not exist in DB, user=" << username);
        mysql_free_result(res);

        if (redisConn) {
            int ttl = utils::MakeTtlWithJitter(600, 300);
            redisConn->setEX(key, "null", ttl);
        }
        g_localUserByName.putNull(username);
        return false;
    }

    idOut       = std::stoi(row[0]);
    passHashOut = row[1] ? row[1] : "";
    mysql_free_result(res);

    LOG_INFO("[loadUserByName] DB hit, user=" << username << " id=" << idOut);

    // 回写缓存
    if (redisConn) {
        try {
            json j;
            j["id"]       = idOut;
            j["username"] = username;
            j["password"] = passHashOut;

            int ttl = utils::MakeTtlWithJitter(3600, 600);
            redisConn->setEX(key, j.dump(), ttl);
        } catch (const std::exception& e) {
            LOG_ERROR("[loadUserByName] build redis json fail, user=" << username
                      << " err=" << e.what());
        }
    }
    g_localUserByName.put(username, idOut, passHashOut);

    return true;
}


// ===================== helper：按手机号加载用户基础信息 =====================
bool AuthService::loadUserByPhone(const std::string& phone,
                                  int&               idOut,
                                  std::string&       usernameOut)
{
    // 0) 本地 L1
    bool isNull = false;
    if (g_localUserCacheByPhone.get(phone, idOut, usernameOut, isNull)) {
        if (isNull) {
            LOG_INFO("[loadUserByPhone] local null, phone=" << phone);
            return false;
        }
        LOG_INFO("[loadUserByPhone] local hit, phone=" << phone
                 << " id=" << idOut << " username=" << usernameOut);
        return true;
    }

    std::string key = "user:phone:" + phone;

    // 1) Redis
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        std::string cached;
        if (redisConn->get(key, cached)) {
            if (cached == "null") {
                LOG_INFO("[loadUserByPhone] redis null, phone=" << phone);
                g_localUserCacheByPhone.putNull(phone);
                return false;
            }

            try {
                json j          = json::parse(cached);
                int  cachedId   = j.value("id", 0);
                std::string name = j.value("username", "");
                if (cachedId > 0) {
                    idOut       = cachedId;
                    usernameOut = name;

                    LOG_INFO("[loadUserByPhone] redis hit, phone=" << phone
                             << " id=" << idOut
                             << " username=" << usernameOut);

                    g_localUserCacheByPhone.put(phone, idOut, usernameOut);
                    return true;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("[loadUserByPhone] parse redis json fail, phone=" << phone
                          << " err=" << e.what());
            }
        }
    } else {
        LOG_WARN("[loadUserByPhone] redis not available, maybe use DB+limit");
    }

    // 2) RedisDown 时：打 DB 前先限流（可选）
    if (RedisPool::IsDown()) {
        if (!g_loginLimiter.allow()) {
            LOG_WARN("[loadUserByPhone] reject by QPS limiter, phone=" << phone);
            return false;
        }
    }

    // 3) DB
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[loadUserByPhone] no db connection");
        return false;
    }

    std::string sql =
        "SELECT id, username FROM users "
        "WHERE phone = '" + phone + "' "
        "LIMIT 1";

    MYSQL_RES* res = conn->query(sql);
    if (!res) {
        LOG_ERROR("[loadUserByPhone] query failed, phone=" << phone);
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        LOG_INFO("[loadUserByPhone] phone not exist in DB, phone=" << phone);
        mysql_free_result(res);

        if (redisConn) {
            int ttl = utils::MakeTtlWithJitter(600, 300);
            redisConn->setEX(key, "null", ttl);
        }
        g_localUserCacheByPhone.putNull(phone);
        return false;
    }

    idOut = std::stoi(row[0]);
    usernameOut = row[1] ? row[1] : "";
    mysql_free_result(res);

    LOG_INFO("[loadUserByPhone] DB hit, phone=" << phone
             << " id=" << idOut
             << " username=" << usernameOut);

    // 回写缓存
    if (redisConn) {
        try {
            json j;
            j["id"]       = idOut;
            j["username"] = usernameOut;
            j["phone"]    = phone;

            int ttl = utils::MakeTtlWithJitter(3600, 600);
            redisConn->setEX(key, j.dump(), ttl);
        } catch (const std::exception& e) {
            LOG_ERROR("[loadUserByPhone] build redis json fail, phone=" << phone
                      << " err=" << e.what());
        }
    }
    g_localUserCacheByPhone.put(phone, idOut, usernameOut);

    return true;
}


// ===================== helper：缓存失效 / 预热 =====================
void AuthService::invalidateUserCacheByName(const std::string& username) {
    if (username.empty()) return;

    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        redisConn->del("user:name:" + username);
    }
    g_localUserByName.erase(username);
}

void AuthService::invalidateUserCacheByPhone(const std::string& phone) {
    if (phone.empty()) return;

    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        redisConn->del("user:phone:" + phone);
    }
    g_localUserCacheByPhone.erase(phone);
}

void AuthService::warmUserCacheByPhone(int                userId,
                                       const std::string& phone,
                                       const std::string& username)
{
    if (phone.empty() || userId <= 0) return;

    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        try {
            json j;
            j["id"]       = userId;
            j["username"] = username;
            j["phone"]    = phone;

            int ttl = utils::MakeTtlWithJitter(3600, 600);
            redisConn->setEX("user:phone:" + phone, j.dump(), ttl);
        } catch (const std::exception& e) {
            LOG_ERROR("[warmUserCacheByPhone] build redis json fail, phone=" << phone
                      << " err=" << e.what());
        }
    }

    g_localUserCacheByPhone.put(phone, userId, username);
}
