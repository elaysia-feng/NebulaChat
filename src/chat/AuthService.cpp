#include "chat/AuthService.h"
#include "db/DBpool.h"
#include <string>
#include <iostream>

bool AuthService::login(const std::string& user,
                        const std::string& pass,
                        int&               userId)
{
    // 从连接池获取连接
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        std::cerr << "[AuthService::login] ERROR: no db connection\n";
        return false;
    }

    // 简单示例：假设 user 表结构：
    // users(id INT PK, username VARCHAR, password VARCHAR)
    std::string sql =
        "SELECT id FROM users "
        "WHERE username = '" + user + "' "
        "AND password = '" + pass + "' "
        "LIMIT 1";

    std::cout << "[AuthService::login] SQL = " << sql << std::endl;

    MYSQL_RES* res = conn->query(sql);
    if (!res) {
        std::cerr << "[AuthService::login] query failed\n";
        return false;
    }

    // 一次一行
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        std::cerr << "[AuthService::login] no such user or wrong password\n";
        mysql_free_result(res);
        return false;
    }

    // row[0] = id
    userId = std::stoi(row[0]);
    std::cout << "[AuthService::login] user " << user
              << " login success, id = " << userId << std::endl;

    mysql_free_result(res);
    return true;
}    


bool AuthService::Register(const std::string& user,
                           const std::string& pass,
                           int&               userId)
{
    auto conn = DBPool::Instance().getConnection();
    if (!conn) {
        std::cerr << "[AuthService::Register] ERROR: no db connection\n";
        return false;
    }

    // 1. 检查是否存在同名用户
    // SQL 修复：原来缺空格
    std::string check_sql =
        "SELECT id FROM users "
        "WHERE username = '" + user + "' "
        "LIMIT 1";

    std::cout << "[AuthService::Register] check SQL = " << check_sql << std::endl;

    MYSQL_RES* check_res = conn->query(check_sql);
    if (check_res) {
        MYSQL_ROW row = mysql_fetch_row(check_res);
        if (row) {
            // 用户名已存在
            std::cerr << "[AuthService::Register] username already exists: "
                      << user << std::endl;
            mysql_free_result(check_res);      // 必须释放
            return false;
        }
        mysql_free_result(check_res);
    }

    // 2. 插入新用户
    std::string insert_sql =
        "INSERT INTO users(username, password) "
        "VALUES('" + user + "', '" + pass + "')";

    std::cout << "[AuthService::Register] insert SQL = " << insert_sql << std::endl;

    if (!conn->update(insert_sql)) {
        std::cerr << "[AuthService::Register] insert error" << std::endl;
        return false;
    }

    // 3. 再查出 id
    std::string select_sql =
        "SELECT id FROM users "
        "WHERE username = '" + user + "' "
        "LIMIT 1";

    std::cout << "[AuthService::Register] select SQL = " << select_sql << std::endl;

    MYSQL_RES* id_res = conn->query(select_sql);
    if (!id_res) {
        std::cerr << "[AuthService::Register] select id failed\n";
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(id_res);

    if (!row) {
        std::cerr << "[AuthService::Register] ERROR: cannot fetch id after insert\n";
        mysql_free_result(id_res);
        return false;
    }

    userId = std::stoi(row[0]);
    std::cout << "[AuthService::Register] register success, user = " << user
              << ", id = " << userId << std::endl;

    mysql_free_result(id_res);
    return true;
}
