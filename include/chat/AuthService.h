#pragma once
#include <string>
#include "SmsService.h"
class Connection;

class AuthService
{
private:
    SmsService Sms_;

public:
    // 检查用户名/密码是否合法，合法返回 true，并输出 userId
    bool login(const std::string& user,
               const std::string& pass,
               int&               userId);
    
    bool loginByPhone(const std::string& phone,
                      int&               userId,
                      std::string&       usernameOut);

    //注册
    bool Register(const std::string& phone,
               const std::string& user,
               const std::string& pass,
               int&               userId);
    //更新数据改名字
    bool updateUsername(int userId,
                    const std::string& newName,
                    std::string&       oldNameOut,
                    std::string&       phoneOut);

    //改密码
    bool resetPasswordByPhone(const std::string& phone,
                          const std::string& newPass);
};