#include "chat/SmsService.h"
#include "db/RedisPool.h"
#include "core/Logger.h"
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <mutex>

namespace {
    const int SMS_CODE_LEN        = 6;
    const int SMS_EXPIRE_SECONDS  = 60;   // 验证码有效期
    std::once_flag g_randInitFlag;
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

    int min = 1;
    for (int i = 0; i < SMS_CODE_LEN - 1; ++i) {
        min *= 10;
    }
    int max  = min * 10 - 1;
    int code = std::rand() % (max - min + 1) + min;
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

    // 这里模拟“发送短信”：真实环境下可以换成短信网关 HTTP 调用
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
