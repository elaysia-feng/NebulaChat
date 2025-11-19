#include "chat/AuthService.h"
#include "db/DBpool.h"
#include "db/RedisPool.h"
#include "core/Logger.h"
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;


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
            redisConn->setEX(key, "null", 120);
            LOG_INFO("[AuthService::login] set null cache for user=" << user);
        }
        return false;
    }
    int         dbId   = std::stoi(row[0]);
    std::string dbPass = row[2] ? row[2] : "";
    
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
        
        redisConn->setEX(key, j.dump(), 3600); // 缓存 1 小时
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
    if(RedisConn) {
        try{
            json j;
            j["id"]       = userId;
            j["username"] = user;
            j["phone"]    = phone;
            j["password"] = pass;

            std::string v = j.dump();

            RedisConn->setEX("user:name:" + user, v, 3600);
            RedisConn->setEX("user:pass:" + pass, v, 3600);

            LOG_INFO("[AuthService::Register] warm cache for user=" << user
                     << " phone=" << phone);
        } catch (const std::exception& e) {
            LOG_ERROR("[AuthService::Register] build cache json fail, user=" << user
                      << " err=" << e.what());
        }
    }


    return true;
}

// ===================== 手机号登录（带缓存） =====================
bool AuthService::loginByPhone(const std::string& phone,
                               int&               userId,
                               std::string&       usernameOut)
{
    std::string key = "user:phone" + phone;

    // 1) 先走 Redis 缓存
    auto redisConn = RedisPool::Instance().getConnection();
    if (redisConn) {
        std::string cached;
        if(redisConn->get(key, cached)) {
            if (cached == "null") {
                LOG_INFO("[AuthService::loginByPhone] cache hit null, phone=" << phone);
                return false;
            }

            try{
                json j = json::parse(cached);
                int cachedId = j.value("id", 0);
                std::string cachedName = j.value("username", "");

                if (cachedId > 0) {
                    userId      = cachedId;
                    usernameOut = cachedName;
                    LOG_INFO("[AuthService::loginByPhone] cache hit, phone=" << phone
                             << " id=" << userId
                             << " username=" << usernameOut);
                    return true;
                }
            }catch (const std::exception& e) {
                LOG_ERROR("[AuthService::loginByPhone] parse cache fail, phone=" << phone
                          << " err=" << e.what());
            }
        }
    } else {
        LOG_WARN("[AuthService::loginByPhone] redis not available, fallback to DB only");
    }

    // 2) 缓存未命中 → 查 MySQL
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

        // 写一个短期空缓存，防止穿透
        if (redisConn) {
            redisConn->setEX(key, "null", 60);
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

    // 3) 写回 Redis 缓存
    if (redisConn) {
        try {
            json j;
            j["id"]       = userId;
            j["username"] = usernameOut;
            j["phone"]    = phone;

            redisConn->setEX(key, j.dump(), 3600);
            LOG_INFO("[AuthService::loginByPhone] set cache for phone=" << phone);
        } catch (const std::exception& e) {
            LOG_ERROR("[AuthService::loginByPhone] build cache json fail, phone=" << phone
                      << " err=" << e.what());
        }
    }

    return true;
}