#include <chat/MessageHandler.h>
#include "core/Server.h"
#include <iostream>

using json = nlohmann::json;
using namespace std;

string MessageHandler::handleMessage(Connection& c, const std::string& line) {
    json resp;
    try {
        // 1. 解析客户端发来的 JSON
        json rep = json::parse(line);
        std::string cmd = rep.value("cmd", "");

        // 2. 未登录只能执行 login
        if (!c.authed && cmd != "login") {
            resp["ok"]  = false;
            resp["err"] = "please login first";
            return resp.dump() + '\n';
        }

        // 3. 各种业务分支

        if (cmd == "login") {
            // 这里要从 rep 取字段，不是 resp
            std::string user = rep.value("user", "");
            std::string pass = rep.value("pass", "");

            // 先写死，后面再接 MySQL
            if (user == "admin" && pass == "123456") {
                c.authed = true;
                c.name   = user;
                c.userId = 1;
                resp["ok"]  = true;
                resp["msg"] = "login success";
            } else {
                resp["ok"]  = false;
                resp["msg"] = "wrong user or password";
            }
        }
        else if (cmd == "echo") {
            std::string msg = rep.value("msg", "");
            resp["ok"]   = true;
            resp["data"] = msg;
        }
        else if (cmd == "upper") {
            std::string msg = rep.value("msg", "");
            for (char& ch : msg) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            resp["ok"]   = true;
            resp["data"] = msg;
        }
        else if (cmd == "quit") {
            resp["ok"]    = true;
            resp["data"]  = "bye";
            resp["close"] = true;
        }
        else {
            resp["ok"]  = false;
            resp["err"] = "unknown cmd";
        }

    } catch (const std::exception& e) {
        resp["ok"]  = false;
        resp["err"] = e.what();
        return resp.dump() + '\n';
    }

    return resp.dump() + '\n';
}