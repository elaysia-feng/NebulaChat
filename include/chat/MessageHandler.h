#pragma once 
#include <nlohmann/json.hpp>
#include <string>

struct Connection;

class MessageHandler
{
private:
    nlohmann::json resp_;

public:
    std::string handleMessage(Connection& conn, const std::string& line);
};

