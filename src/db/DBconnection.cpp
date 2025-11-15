#include "db/DBconnection.h"
#include <iostream>

/*mysql_init(NULL) 会自动创建一个新的 MYSQL 连接句柄并返回。
如果传入非 NULL，则初始化你提供的结构体。*/
DBconnection::DBconnection() : SqlConn_(mysql_init(nullptr)) {
    if (!SqlConn_) {
        std::cerr << "[DBconnection::DBconnection] mysql_init failed\n";
    } else {
        std::cout << "[DBconnection::DBconnection] mysql_init OK, handle=" 
                  << SqlConn_ << std::endl;
    }
}

DBconnection::~DBconnection(){
    if(SqlConn_){
        std::cout << "[DBconnection::~DBconnection] closing MySQL connection, handle="
                  << SqlConn_ << std::endl;
        mysql_close(SqlConn_);
        SqlConn_ = nullptr;
    }
}

bool DBconnection::connect(const std::string& host,
                           unsigned int       port,
                           const std::string& user,
                           const std::string& password,
                           const std::string& dbname,
                           const std::string& charset){
    if(!SqlConn_) {
        std::cerr << "[DBconnection::connect] SqlConn_ is null, cannot connect\n";
        return false;
    }

    std::cout << "[DBconnection::connect] try connect: host=" << host
              << " port=" << port
              << " user=" << user
              << " dbname=" << dbname
              << " charset=" << charset << std::endl;
    
    // 设置字符集
    if (mysql_options(SqlConn_, MYSQL_SET_CHARSET_NAME, charset.c_str()) != 0) {
        std::cerr << "[DBconnection::connect] mysql_options(MYSQL_SET_CHARSET_NAME) failed: "
                  << mysql_error(SqlConn_) << std::endl;
        // 这里先不中断，让后续连接试试
    }

    if(!mysql_real_connect(SqlConn_, 
                           host.c_str(),
                           user.c_str(),
                           password.c_str(),
                           dbname.c_str(),
                           port,
                           nullptr,
                           0 )){
        std::cerr << "[DBconnection::connect] MySQL connect failed: "
                  << mysql_error(SqlConn_) << std::endl;
        return false;
    }           
    
    std::cout << "[DBconnection::connect] MySQL connect OK, handle="
              << SqlConn_ << std::endl;
    return true;
}

MYSQL_RES* DBconnection::query(const std::string& sql){
    std::cout << "[DBconnection::query] SQL: " << sql << std::endl;

    if (mysql_query(SqlConn_, sql.c_str()) != 0){
        std::cerr << "[DBconnection::query] MySQL query failed: "
                  << mysql_error(SqlConn_) << std::endl;
        return nullptr;
    }

    MYSQL_RES* res = mysql_store_result(SqlConn_);
    if (!res) {
        // 对于 SELECT，如果没有结果集，mysql_store_result 也可能是 nullptr
        // 这里打印一下，方便区分是“没结果”还是“错误”
        if (mysql_field_count(SqlConn_) != 0) {
            std::cerr << "[DBconnection::query] mysql_store_result returned NULL, but field_count != 0\n";
        } else {
            std::cout << "[DBconnection::query] query OK, no result set (e.g. UPDATE/INSERT)\n";
        }
    } else {
        std::cout << "[DBconnection::query] query OK, has result set\n";
    }

    return res;
}

bool DBconnection::update(const std::string& sql){
    std::cout << "[DBconnection::update] SQL: " << sql << std::endl;

    if(mysql_query(SqlConn_, sql.c_str()) != 0) {
        std::cerr << "[DBconnection::update] MySQL update failed: "
                  << mysql_error(SqlConn_) << std::endl;
        return false;
    }

    // 影响的行数（对 INSERT/UPDATE/DELETE 有用）
    my_ulonglong affected = mysql_affected_rows(SqlConn_);
    std::cout << "[DBconnection::update] update OK, affected_rows="
              << affected << std::endl;

    return true;
}
