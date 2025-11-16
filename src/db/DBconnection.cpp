#include "db/DBconnection.h"
#include "core/Logger.h"   // 新增：日志头文件

/*mysql_init(NULL) 会自动创建一个新的 MYSQL 连接句柄并返回。
如果传入非 NULL，则初始化你提供的结构体。*/
DBconnection::DBconnection() : SqlConn_(mysql_init(nullptr)) {
    if (!SqlConn_) {
        LOG_ERROR("[DBconnection::DBconnection] mysql_init failed");
    } else {
        LOG_INFO("[DBconnection::DBconnection] mysql_init OK, handle=" << SqlConn_);
    }
}

DBconnection::~DBconnection(){
    if(SqlConn_){
        LOG_INFO("[DBconnection::~DBconnection] closing MySQL connection, handle=" << SqlConn_);
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
        LOG_ERROR("[DBconnection::connect] SqlConn_ is null, cannot connect");
        return false;
    }

    LOG_INFO("[DBconnection::connect] try connect: host=" << host
             << " port=" << port
             << " user=" << user
             << " dbname=" << dbname
             << " charset=" << charset);
    
    // 设置字符集
    if (mysql_options(SqlConn_, MYSQL_SET_CHARSET_NAME, charset.c_str()) != 0) {
        LOG_WARN("[DBconnection::connect] mysql_options(MYSQL_SET_CHARSET_NAME) failed: "
                 << mysql_error(SqlConn_));
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
        LOG_ERROR("[DBconnection::connect] MySQL connect failed: "
                  << mysql_error(SqlConn_));
        return false;
    }           
    
    LOG_INFO("[DBconnection::connect] MySQL connect OK, handle=" << SqlConn_);
    return true;
}

MYSQL_RES* DBconnection::query(const std::string& sql){
    LOG_DEBUG("[DBconnection::query] SQL: " << sql);

    if (mysql_query(SqlConn_, sql.c_str()) != 0){
        LOG_ERROR("[DBconnection::query] MySQL query failed: "
                  << mysql_error(SqlConn_));
        return nullptr;
    }

    MYSQL_RES* res = mysql_store_result(SqlConn_);
    if (!res) {
        // 对于 SELECT，如果没有结果集，mysql_store_result 也可能是 nullptr
        // 这里打印一下，方便区分是“没结果”还是“错误”
        if (mysql_field_count(SqlConn_) != 0) {
            LOG_ERROR("[DBconnection::query] mysql_store_result returned NULL, but field_count != 0");
        } else {
            LOG_INFO("[DBconnection::query] query OK, no result set (e.g. UPDATE/INSERT)");
        }
    } else {
        LOG_DEBUG("[DBconnection::query] query OK, has result set");
    }

    return res;
}

bool DBconnection::update(const std::string& sql){
    LOG_DEBUG("[DBconnection::update] SQL: " << sql);

    if(mysql_query(SqlConn_, sql.c_str()) != 0) {
        LOG_ERROR("[DBconnection::update] MySQL update failed: "
                  << mysql_error(SqlConn_));
        return false;
    }

    // 影响的行数（对 INSERT/UPDATE/DELETE 有用）
    my_ulonglong affected = mysql_affected_rows(SqlConn_);
    LOG_INFO("[DBconnection::update] update OK, affected_rows=" << affected);

    return true;
}
