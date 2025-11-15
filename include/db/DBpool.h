#pragma once
#include "DBconnection.h"
#include "core/SafeQueue.h"


class DBPool {
public:
    static DBPool& Instance();

    // 初始化连接池（程序启动时调用一次）
    bool init(const std::string& host,
              unsigned int        port,
              const std::string& user,
              const std::string& password,
              const std::string& dbname,
              int                 poolSize);

    // 从池子中取一个连接（shared_ptr，自动归还）
    DBConnectionPtr getConnection();

private:
    DBPool() = default;
    ~DBPool() = default;

    DBPool(const DBPool&)            = delete;
    DBPool& operator=(const DBPool&) = delete;

private:
    SafeQueue<DBConnectionPtr> pool_;
    std::once_flag initFlag_;
    bool inited_{false};

    std::string host_;
    unsigned int port_{0};
    std::string user_;
    std::string password_;
    std::string dbname_;
    int poolSize_{0};
};
