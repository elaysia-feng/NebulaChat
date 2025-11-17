#include "chat/MessageHandler.h"
#include "core/Server.h"
#include <iostream>

using json = nlohmann::json;

std::string MessageHandler::handleMessage(Connection& c, const std::string& line) {
    json resp;
    try {
        json rep = json::parse(line);
        std::string cmd = rep.value("cmd", "");

        // 未登录只能执行 login | register | send_sms | verify_sms
        if (!c.authed &&
            cmd != "login" &&
            cmd != "register" &&
            cmd != "send_sms" &&
            cmd != "verify_sms") {
            resp["ok"]  = false;
            resp["err"] = "please login first";
            return resp.dump() + "\n";
        }

        // ---------------- login ----------------
        if (cmd == "login") {
            std::string user = rep.value("user", "");
            std::string pass = rep.value("pass", "");

            int uid = 0;
            if (auth_.login(user, pass, uid)) {
                c.authed = true;
                c.userId = uid;
                c.name   = user;

                resp["ok"]  = true;
                resp["msg"] = "login success";
            } else {
                resp["ok"]  = false;
                resp["msg"] = "wrong username or password";
            }
        }

        // ---------------- register ----------------
        else if (cmd == "register") {
            
            std::string user = rep.value("user", "");
            std::string pass = rep.value("pass", "");

            int uid = 0;
            if (auth_.Register(user, pass, uid)) {
                resp["ok"]     = true;
                resp["msg"]    = "register success";
                resp["userId"] = uid;
                resp["user"]   = user;
            }
            else {
                resp["ok"]  = false;
                resp["msg"] = "register failed";
            }
        } 
             // ---------------- send_sms ----------------
        else if (cmd == "send_sms") {
            std::string phone = rep.value("phone", "");
            SmsResult r       = sms_.sendCode(phone);
            resp["ok"]        = r.ok;
            resp["msg"]       = r.msg;
        }

        // ---------------- verify_sms ----------------
        else if (cmd == "verify_sms") {
            std::string phone = rep.value("phone", "");
            std::string code  = rep.value("code", "");

            SmsResult r = sms_.verifyCode(phone, code);
            resp["ok"]  = r.ok;
            resp["msg"] = r.msg;

            // 这里以后可以选择：
            // 验证成功后自动帮用户完成登录/注册逻辑
            // 例如标记一个手机号已验证，或去调用 AuthService
        }

        // ---------------- echo ----------------
        else if (cmd == "echo") {
            std::string msg = rep.value("msg", "");
            resp["ok"]   = true;
            resp["data"] = msg;
        }

        // ---------------- upper ----------------
        else if (cmd == "upper") {
            std::string msg = rep.value("msg", "");
            for (auto& ch : msg) ch = toupper(ch);
            resp["ok"]   = true;
            resp["data"] = msg;
        }

        // ---------------- quit ----------------
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
    }

    return resp.dump() + "\n";
}
