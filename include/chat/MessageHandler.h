#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "chat/AuthService.h"

struct Connection;  // 只需要前向声明

class MessageHandler
{
public:
    std::string handleMessage(Connection& c, const std::string& line);

private:
    AuthService auth_;
};
