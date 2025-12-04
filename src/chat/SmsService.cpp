#include "chat/SmsService.h"
#include "db/RedisPool.h"
#include "core/Logger.h"
#include "utils/Random.h"
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace {
    const int SMS_CODE_LEN        = 6;
    const int SMS_EXPIRE_SECONDS  = 60;   // 验证码有效期
    const int SMS_RESEND_COOLDOWN = 30;   // 同一手机号最小重发间隔
    std::once_flag g_randInitFlag;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_lastSend;
    std::mutex g_sendMtx;
}

bool SmsService::isPhoneValid(const std::string& phone)
{
    if (phone.size() != 11) return false;
    if (phone[0] != '1')    return false;
    for (char c : phone) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string SmsService::genCode()
{
    std::call_once(g_randInitFlag, []() {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
    });

    int code = utils::RandInt(100000, 999999);
    return std::to_string(code);
}

SmsResult SmsService::sendCode(const std::string& phone)
{
    SmsResult r;

    if (!isPhoneValid(phone)) {
        r.ok  = false;
        r.msg = "invalid phone number";
        return r;
    }

    {
        std::lock_guard<std::mutex> lk(g_sendMtx);
        auto now = std::chrono::steady_clock::now();
        auto it  = g_lastSend.find(phone);
        if (it != g_lastSend.end()) {
            auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
            if (diff < SMS_RESEND_COOLDOWN) {
                r.ok  = false;
                r.msg = "request too frequent, wait a bit";
                return r;
            }
        }
        g_lastSend[phone] = now;
    }

    auto conn = RedisPool::Instance().getConnection();
    if (!conn) {
        r.ok  = false;
        r.msg = "redis not available";
        return r;
    }

    std::string code = genCode();
    std::string key  = "sms:" + phone;

    if (!conn->setEX(key, code, SMS_EXPIRE_SECONDS)) {
        r.ok  = false;
        r.msg = "redis setEX failed";
        return r;
    }

    // 这里只在服务端日志里记录验证码，客户端不回显
    LOG_INFO("[SmsService::sendCode] send sms code phone=" << phone
             << " code=" << code);

    r.ok  = true;
    r.msg = "code sent";
    return r;
}

SmsResult SmsService::verifyCode(const std::string& phone,
                                 const std::string& code)
{
    SmsResult r;

    if (!isPhoneValid(phone)) {
        r.ok  = false;
        r.msg = "invalid phone number";
        return r;
    }

    auto conn = RedisPool::Instance().getConnection();
    if (!conn) {
        r.ok  = false;
        r.msg = "redis not available";
        return r;
    }

    std::string key = "sms:" + phone;
    std::string stored;
    if (!conn->get(key, stored)) {
        r.ok  = false;
        r.msg = "code not found or expired";
        return r;
    }

    if (stored != code) {
        r.ok  = false;
        r.msg = "code mismatch";
        return r;
    }

    // 验证成功后删除验证码
    conn->del(key);

    r.ok  = true;
    r.msg = "verify ok";
    return r;
}
