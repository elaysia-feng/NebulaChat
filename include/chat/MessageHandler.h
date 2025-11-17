#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "chat/AuthService.h"
#include "SmsService.h"

struct Connection;  // 只需要前向声明

class MessageHandler
{
public:
    std::string handleMessage(Connection& c, const std::string& line);

private:
    AuthService auth_;
    SmsService  sms_;   // 新增：短信服务
};
