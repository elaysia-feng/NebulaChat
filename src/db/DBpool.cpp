#include "db/DBpool.h"
#include "core/Logger.h"

DBPool& DBPool::Instance(){
    static DBPool instance;
    return instance;
}

bool DBPool::init(const std::string& host,
                  unsigned int       port,
                  const std::string& user,
                  const std::string& password,
                  const std::string& dbname,
                  int                poolSize)
{
    bool ok = false;

    std::call_once(initFlag_, [&](){
        LOG_INFO("[DBPool::init] start init: host=" << host
                 << " port=" << port
                 << " user=" << user
                 << " db=" << dbname
                 << " poolSize=" << poolSize);

        host_     = host;
        port_     = port;
        user_     = user;
        password_ = password;
        dbname_   = dbname;
        poolSize_ = poolSize;

        int success = 0;

        for (int i = 0; i < poolSize_; ++i) {
            auto conn = std::make_shared<DBconnection> ();
            if (!conn->connect(host_, port_, user_, password_, dbname_)) {
                LOG_ERROR("[DBPool::init] connect failed, index=" << i);
                continue;
            }
            pool_.Safepush(conn);
            ++success;
            LOG_DEBUG("[DBPool::init] created connection index=" << i
                      << " raw=" << conn.get());
        }

        if (success == 0) {
            LOG_ERROR("[DBPool::init] no connection created, init FAILED");
            inited_ = false;
            ok      = false;
        } else {
            LOG_INFO("[DBPool::init] init OK, success=" << success
                     << " / poolSize=" << poolSize_);
            inited_ = true;
            ok      = true;
        }
    });

    if (!inited_) {
        LOG_ERROR("[DBPool::init] initFlag already set, but inited_ = false");
    }

    // 用逻辑与，表达“初始化标记 + 本次 init 的结果”
    return inited_ && ok;
}


DBConnectionPtr DBPool::getConnection(){
    if (!inited_) {
        LOG_ERROR("[DBPool::getConnection] DBPool not inited");
        return nullptr;
    }

    DBConnectionPtr conn;

    if (!pool_.Safepop(conn)) {
        LOG_WARN("[DBPool::getConnection] Safepop failed (pool empty or stopped)");
        return nullptr;
    }

    LOG_DEBUG("[DBPool::getConnection] got connection from pool, raw="
              << conn.get());

    auto self = this;
    //不是马上执行 deleter，而是注册了一个“未来要执行的动作”。
    /*因为我的这个对象是一个share_ptr类型，
      这个 DBConnectionPtr(conn.get(),
      [self,conn](DBconnection* p) 相当于定义了这个share_ptr里面的deleter,
      当这个对象没用的时候才会再执行这个指令，
      也就是“把连接还回连接池”。*/
    return DBConnectionPtr(conn.get(), [self, conn](DBconnection* p) {
        (void)p; // p 实际上不用，我们只是利用 shared_ptr<DBconnection> conn 来管理生命周期

        LOG_DEBUG("[DBPool::getConnection] return connection to pool, raw="
                  << conn.get());

        // 归还到队列
        self->pool_.Safepush(conn);
    });
}
