#include "chat/AuthService.h"
#include "db/DBpool.h"
#include "core/Logger.h"
#include <string>

bool AuthService::login(const std::string& user,
                        const std::string& pass,
                        int&               userId)
{
    // 从连接池获取连接
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::login] ERROR: no db connection");
        return false;
    }

    // 简单示例：假设 user 表结构：
    // users(id INT PK, username VARCHAR, password VARCHAR, phone VARCHAR)
    std::string sql =
        "SELECT id FROM users "
        "WHERE username = '" + user + "' "
        "AND password = '" + pass + "' "
        "LIMIT 1";

    LOG_DEBUG("[AuthService::login] SQL = " << sql);

    MYSQL_RES* res = conn->query(sql);
    if (!res) {
        LOG_ERROR("[AuthService::login] query failed");
        return false;
    }

    // 一次一行
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        LOG_WARN("[AuthService::login] no such user or wrong password, user=" << user);
        mysql_free_result(res);
        return false;
    }

    // row[0] = id
    userId = std::stoi(row[0]);
    LOG_INFO("[AuthService::login] user " << user
             << " login success, id = " << userId);

    mysql_free_result(res);
    return true;
}    


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
    return true;
}

bool AuthService::loginByPhone(const std::string& phone,
                               int&               userId,
                               std::string&       usernameOut)
{
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
    return true;
}
