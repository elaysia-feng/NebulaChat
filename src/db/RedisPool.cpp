#include "db/RedisPool.h"
#include "core/Logger.h"

RedisPool& RedisPool::Instance()
{
    static RedisPool instance;
    return instance;
}

bool RedisPool::init(const std::string& host, int port, int poolSize)
{
    bool ok = false;

    std::call_once(initFlag_, [&]() {
        LOG_INFO("[RedisPool::init] start init: host=" << host
                 << " port=" << port
                 << " poolSize=" << poolSize);

        host_     = host;
        port_     = port;
        poolSize_ = poolSize;

        int success = 0;
        for (int i = 0; i < poolSize_; ++i) {
            auto conn = std::make_shared<RedisConnection>();
            if (!conn->connect(host_, port_)) {
                LOG_ERROR("[RedisPool::init] connect failed, index=" << i);
                continue;
            }
            pool_.Safepush(conn);
            ++success;
            LOG_DEBUG("[RedisPool::init] created redis connection index=" << i
                      << " raw=" << conn.get());
        }

        if (success == 0) {
            LOG_ERROR("[RedisPool::init] no connection created, init FAILED");
            inited_ = false;
            ok      = false;
        } else {
            LOG_INFO("[RedisPool::init] init OK, success=" << success
                     << " / poolSize=" << poolSize_);
            inited_ = true;
            ok      = true;
        }
    });

    if (!inited_) {
        LOG_ERROR("[RedisPool::init] initFlag already set, but inited_=false");
    }

    return inited_ && ok;
}

RedisConnPtr RedisPool::getConnection()
{
    if (!inited_) {
        LOG_ERROR("[RedisPool::getConnection] RedisPool not inited");
        return nullptr;
    }

    RedisConnPtr conn;
    if (!pool_.Safepop(conn)) {
        LOG_WARN("[RedisPool::getConnection] Safepop failed (pool empty or stopped)");
        return nullptr;
    }

    LOG_DEBUG("[RedisPool::getConnection] got redis connection from pool, raw="
              << conn.get());
              
    /*这里仅仅是为了好看，
    因为是单例，所以不会存在指针悬空，
    如果不是单例的话（instance），
    就要考虑用share_ptr_this了*/
    auto self = this;

    // 和 DBPool 一样，自定义 deleter：连接用完自动归还到队列
    return RedisConnPtr(conn.get(), [self, conn](RedisConnection* p) {
        (void)p;
        LOG_DEBUG("[RedisPool::getConnection] return redis connection to pool, raw="
                  << conn.get());
        self->pool_.Safepush(conn);
    });
}
