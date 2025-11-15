#pragma once
#include <string>
class Connection;

class AuthService
{
public:
    // 检查用户名/密码是否合法，合法返回 true，并输出 userId
    bool login(const std::string& user,
               const std::string& pass,
               int&               userId);
    //注册
    bool Register(const std::string& user,
               const std::string& pass,
               int&               userId);
};