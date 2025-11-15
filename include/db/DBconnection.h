#pragma once
#include <mysql/mysql.h>
#include <string>
#include <memory>

class DBconnection
{
private:
    MYSQL* SqlConn_{nullptr};
public:
    DBconnection(/* args */);
    ~DBconnection();

    // 禁止拷贝，只允许移动
    DBconnection(const DBconnection& ) = delete;
    DBconnection& operator=(const DBconnection) = delete;
    
    //默认就是utf8mb4
    bool connect(const std::string& host,
                 unsigned int        port,
                 const std::string& user,
                 const std::string& password,
                 const std::string& dbname,
                 const std::string& charset = "utf8mb4");

    // 执行查询，返回 MYSQL_RES*，用完要 mysql_free_result
    MYSQL_RES* query(const std::string& sql);

    // 执行增删改
    bool update(const std::string& sql);

    MYSQL* raw() {return SqlConn_;}
};


using DBConnectionPtr = std::shared_ptr<DBconnection>;