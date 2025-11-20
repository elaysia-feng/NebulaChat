#pragma once

#include <unordered_map>
#include <list>
#include <mutex>
#include <chrono>
#include <string>
#include <cstddef>

namespace utils {

// 本地手机号 -> 用户信息 的 LRU + TTL 缓存
class LocalUserCacheByPhone {
public:
    // capacity = 最多缓存多少个手机号
    // ttlSeconds = 每个缓存存活时间（秒）
    LocalUserCacheByPhone(std::size_t capacity, int ttlSeconds)
        : capacity_(capacity), ttlSeconds_(ttlSeconds) {}

    // 读缓存：命中返回 true，并填充 userId / usernameOut
    bool get(const std::string& phone, int& userId, std::string& usernameOut) {
        std::lock_guard<std::mutex> lk(mu_);

        auto it = index_.find(phone);
        if (it == index_.end()) return false;

        auto now = std::chrono::steady_clock::now();
        Node& node = *(it->second);

        // TTL 过期了：顺便删掉
        if (now >= node.expire) {
            cache_.erase(it->second);
            index_.erase(it);
            return false;
        }

        // LRU：访问过的移动到链表尾部（表示“最近使用”）
        cache_.splice(cache_.end(), cache_, it->second);

        userId      = node.id;
        usernameOut = node.username;
        return true;
    }

    // 写缓存：插入 / 覆盖 phone -> (id, username)
    void put(const std::string& phone, int userId, const std::string& username) {
        std::lock_guard<std::mutex> lk(mu_);

        auto now = std::chrono::steady_clock::now();

        // 如果已经存在，更新并挪到尾部
        auto it = index_.find(phone);
        if (it != index_.end()) {
            Node& node = *(it->second);
            node.id       = userId;
            node.username = username;
            node.expire   = now + std::chrono::seconds(ttlSeconds_);
            cache_.splice(cache_.end(), cache_, it->second);
            return;
        }

        // 容量满了：淘汰最久未使用的（链表头）
        if (index_.size() >= capacity_) {
            const Node& oldest = cache_.front();
            index_.erase(oldest.phone);
            cache_.pop_front();
        }

        // 插入新节点到链表尾
        Node node;
        node.phone    = phone;
        node.id       = userId;
        node.username = username;
        node.expire   = now + std::chrono::seconds(ttlSeconds_);

        cache_.push_back(std::move(node));
        auto itNode = std::prev(cache_.end());
        index_[phone] = itNode;
    }

    // 删除某个 phone 对应的缓存（比如改名 / 改密码时）
    void erase(const std::string& phone) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = index_.find(phone);
        if (it == index_.end()) return;
        cache_.erase(it->second);
        index_.erase(it);
    }

private:
    struct Node {
        std::string phone;  // 为了在淘汰时能从 index_ 里删掉
        int         id{0};
        std::string username;
        std::chrono::steady_clock::time_point expire;  // 过期时间
    };

    std::size_t capacity_;
    int ttlSeconds_;

    // 链表：从前到后 = 最久未用 → 最近使用
    std::list<Node> cache_;

    // 索引：phone -> 链表节点
    std::unordered_map<std::string, std::list<Node>::iterator> index_;

    std::mutex mu_;
};



// 简单 QPS 限流器：每秒最多 limit 次 allow() 返回 true
class SimpleQpsLimiter {
public:
    explicit SimpleQpsLimiter(int limitPerSec)
        : limit_(limitPerSec), lastSec_(0), count_(0) {}

    bool allow() {
        using namespace std::chrono;
        auto now = steady_clock::now();
        auto sec = duration_cast<seconds>(now.time_since_epoch()).count();

        std::lock_guard<std::mutex> lk(mu_);
        if (sec != lastSec_) {
            lastSec_ = sec;
            count_   = 0;
        }

        if (count_ < limit_) {
            ++count_;
            return true;
        }
        return false;
    }

private:
    int        limit_;
    long long  lastSec_;
    int        count_;
    std::mutex mu_;
};

// inline 全局对象（C++17 起可以这样写，避免多重定义）
inline LocalUserCacheByPhone g_localUserCacheByPhone(1024, 30);
inline SimpleQpsLimiter      g_loginByPhoneLimiter(1000);

} // namespace utils
