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
    // users(id INT PK, username VARCHAR, password VARCHAR)
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


bool AuthService::Register(const std::string& user,
                           const std::string& pass,
                           int&               userId)
{
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[AuthService::Register] ERROR: no db connection");
        return false;
    }

    // 1. 检查是否存在同名用户
    // SQL 修复：原来缺空格
    std::string check_sql =
        "SELECT id FROM users "
        "WHERE username = '" + user + "' "
        "LIMIT 1";

    LOG_DEBUG("[AuthService::Register] check SQL = " << check_sql);

    MYSQL_RES* check_res = conn->query(check_sql);
    if (check_res) {
        MYSQL_ROW row = mysql_fetch_row(check_res);
        if (row) {
            // 用户名已存在
            LOG_WARN("[AuthService::Register] username already exists: "
                     << user);
            mysql_free_result(check_res);      // 必须释放
            return false;
        }
        mysql_free_result(check_res);
    }

    // 2. 插入新用户
    std::string insert_sql =
        "INSERT INTO users(username, password) "
        "VALUES('" + user + "', '" + pass + "')";

    LOG_DEBUG("[AuthService::Register] insert SQL = " << insert_sql);

    if (!conn->update(insert_sql)) {
        LOG_ERROR("[AuthService::Register] insert error");
        return false;
    }

    // 3. 再查出 id
    std::string select_sql =
        "SELECT id FROM users "
        "WHERE username = '" + user + "' "
        "LIMIT 1";

    LOG_DEBUG("[AuthService::Register] select SQL = " << select_sql);

    MYSQL_RES* id_res = conn->query(select_sql);
    if (!id_res) {
        LOG_ERROR("[AuthService::Register] select id failed");
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(id_res);

    if (!row) {
        LOG_ERROR("[AuthService::Register] ERROR: cannot fetch id after insert");
        mysql_free_result(id_res);
        return false;
    }

    userId = std::stoi(row[0]);
    LOG_INFO("[AuthService::Register] register success, user = " << user
             << ", id = " << userId);

    mysql_free_result(id_res);
    return true;
}
