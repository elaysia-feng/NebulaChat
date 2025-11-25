#pragma once

#include <unordered_map>
#include <list>
#include <mutex>
#include <chrono>
#include <string>
#include <cstddef>

namespace utils {

// ======================
// 1. 用户名 -> 用户信息 LRU + TTL + 空值缓存
// ======================
class LocalUserByName {
private:
    struct Node {
        std::string username;
        int         id{0};
        std::string password;   // 建议未来换成 passwordHash
        std::chrono::steady_clock::time_point expire;
        bool        isNull{false};
    };

    std::size_t capacity_;
    int         ttlSeconds_;
    std::list<Node> cache_;   // LRU 链表：front 最久未用，back 最近使用
    std::unordered_map<std::string, std::list<Node>::iterator> index_;
    std::mutex mu_;

public:
    LocalUserByName(std::size_t capacity, int ttlSeconds)
        : capacity_(capacity), ttlSeconds_(ttlSeconds) {}

    // 命中返回 true，并填充 idOut / passwordOut / isNullOut
    bool get(const std::string& username,
             int& idOut,
             std::string& passwordOut,
             bool& isNullOut)
    {
        std::lock_guard<std::mutex> lk(mu_);

        auto it = index_.find(username);
        if (it == index_.end()) {
            return false;
        }

        auto now  = std::chrono::steady_clock::now();
        Node& node = *(it->second);

        // TTL 过期：删掉
        if (now >= node.expire) {
            cache_.erase(it->second);
            index_.erase(it);
            return false;
        }

        // LRU：命中，挪到链表尾部
        cache_.splice(cache_.end(), cache_, it->second);

        idOut       = node.id;
        passwordOut = node.password;
        isNullOut   = node.isNull;
        return true;
    }

    // 写缓存：username -> (id, password)，isNull=false
    void put(const std::string& username, int userId, const std::string& password) {
        std::lock_guard<std::mutex> lk(mu_);
        auto now = std::chrono::steady_clock::now();

        auto it = index_.find(username);
        if (it != index_.end()) {
            Node& node = *(it->second);
            node.id       = userId;
            node.password = password;
            node.isNull   = false;
            node.expire   = now + std::chrono::seconds(ttlSeconds_);
            cache_.splice(cache_.end(), cache_, it->second);
            return;
        }

        // 容量满：淘汰最久未用
        if (index_.size() >= capacity_) {
            const Node& oldest = cache_.front();
            index_.erase(oldest.username);
            cache_.pop_front();
        }

        Node node;
        node.username = username;
        node.id       = userId;
        node.password = password;
        node.isNull   = false;
        node.expire   = now + std::chrono::seconds(ttlSeconds_);

        cache_.push_back(std::move(node));
        auto itNode = std::prev(cache_.end());
        index_[username] = itNode;
    }

    // 写一个“空值缓存”
    void putNull(const std::string& username) {
        std::lock_guard<std::mutex> lk(mu_);
        auto now = std::chrono::steady_clock::now();

        auto it = index_.find(username);
        if (it != index_.end()) {
            Node& node = *(it->second);
            node.isNull = true;
            node.expire = now + std::chrono::seconds(ttlSeconds_);
            cache_.splice(cache_.end(), cache_, it->second);
            return;
        }

        if (index_.size() >= capacity_) {
            const Node& oldest = cache_.front();
            index_.erase(oldest.username);
            cache_.pop_front();
        }

        Node node;
        node.username = username;
        node.id       = 0;
        node.password.clear();
        node.isNull   = true;
        node.expire   = now + std::chrono::seconds(ttlSeconds_);

        cache_.push_back(std::move(node));
        auto itNode = std::prev(cache_.end());
        index_[username] = itNode;
    }

    void erase(const std::string& username) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = index_.find(username);
        if (it == index_.end()) return;
        cache_.erase(it->second);
        index_.erase(it);
    }

    // 简单版 isNull：会顺手做过期检查
    bool isNull(const std::string& username) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = index_.find(username);
        if (it == index_.end()) return false;

        auto now = std::chrono::steady_clock::now();
        Node& node = *(it->second);
        if (now >= node.expire) {
            cache_.erase(it->second);
            index_.erase(it);
            return false;
        }
        return node.isNull;
    }
};


// ======================
// 2. 手机号 -> 用户信息 LRU + TTL + 空值缓存
// ======================
class LocalUserCacheByPhone {
private:
    struct Node {
        std::string phone;
        int         id{0};
        std::string username;
        bool        isNull{false};
        std::chrono::steady_clock::time_point expire;
    };

    std::size_t capacity_;
    int         ttlSeconds_;
    std::list<Node> cache_;
    std::unordered_map<std::string, std::list<Node>::iterator> index_;
    std::mutex mu_;

public:
    LocalUserCacheByPhone(std::size_t capacity, int ttlSeconds)
        : capacity_(capacity), ttlSeconds_(ttlSeconds) {}

    bool get(const std::string& phone,
             int& userId,
             std::string& usernameOut,
             bool& isNullOut)
    {
        std::lock_guard<std::mutex> lk(mu_);

        auto it = index_.find(phone);
        if (it == index_.end()) return false;

        auto now = std::chrono::steady_clock::now();
        Node& node = *(it->second);

        if (now >= node.expire) {
            cache_.erase(it->second);
            index_.erase(it);
            return false;
        }

        cache_.splice(cache_.end(), cache_, it->second);

        userId      = node.id;
        usernameOut = node.username;
        isNullOut   = node.isNull;
        return true;
    }

    void put(const std::string& phone, int userId, const std::string& username) {
        std::lock_guard<std::mutex> lk(mu_);
        auto now = std::chrono::steady_clock::now();

        auto it = index_.find(phone);
        if (it != index_.end()) {
            Node& node = *(it->second);
            node.id       = userId;
            node.username = username;
            node.isNull   = false;
            node.expire   = now + std::chrono::seconds(ttlSeconds_);
            cache_.splice(cache_.end(), cache_, it->second);
            return;
        }

        if (index_.size() >= capacity_) {
            const Node& oldest = cache_.front();
            index_.erase(oldest.phone);
            cache_.pop_front();
        }

        Node node;
        node.phone    = phone;
        node.id       = userId;
        node.username = username;
        node.isNull   = false;
        node.expire   = now + std::chrono::seconds(ttlSeconds_);

        cache_.push_back(std::move(node));
        auto itNode = std::prev(cache_.end());
        index_[phone] = itNode;
    }

    void putNull(const std::string& phone) {
        std::lock_guard<std::mutex> lk(mu_);
        auto now = std::chrono::steady_clock::now();

        auto it = index_.find(phone);
        if (it != index_.end()) {
            Node& node = *(it->second);
            node.isNull = true;
            node.expire = now + std::chrono::seconds(ttlSeconds_);
            cache_.splice(cache_.end(), cache_, it->second);
            return;
        }

        if (index_.size() >= capacity_) {
            const Node& oldest = cache_.front();
            index_.erase(oldest.phone);
            cache_.pop_front();
        }

        Node node;
        node.phone    = phone;
        node.id       = 0;
        node.username.clear();
        node.isNull   = true;
        node.expire   = now + std::chrono::seconds(ttlSeconds_);

        cache_.push_back(std::move(node));
        auto itNode = std::prev(cache_.end());
        index_[phone] = itNode;
    }

    void erase(const std::string& phone) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = index_.find(phone);
        if (it == index_.end()) return;
        cache_.erase(it->second);
        index_.erase(it);
    }
};


// ======================
// 3. QPS 限流器（你原来的没啥大问题，先保留）
// ======================
class SimpleQpsLimiter {
public:
    explicit SimpleQpsLimiter(int limitPerSec)
        : limit_(limitPerSec), lastSec_(0), count_(0) {}

    bool allow() {
        using namespace std::chrono;
        auto now = steady_clock::now();
        long long sec = duration_cast<seconds>(now.time_since_epoch()).count();

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


// 全局实例
inline LocalUserCacheByPhone g_localUserCacheByPhone(1024, 30);
inline SimpleQpsLimiter      g_loginLimiter(1000);
inline LocalUserByName    g_localUserByName(1024, 30);

} // namespace utils
