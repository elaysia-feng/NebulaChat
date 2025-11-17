#pragma once
#include <string>

struct SmsResult {
    bool        ok{false};
    std::string msg;
};

class SmsService
{
public:
    // 发送验证码：校验手机 → 生成验证码 → 存 Redis → “发送”
    SmsResult sendCode(const std::string& phone);

    // 校验验证码是否正确
    SmsResult verifyCode(const std::string& phone, const std::string& code);

private:
    bool        isPhoneValid(const std::string& phone);
    std::string genCode();
};
