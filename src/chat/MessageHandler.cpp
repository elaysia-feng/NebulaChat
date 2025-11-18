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
    std::string mode = rep.value("mode", "password");

    // 1) 用户名 + 密码登录
    if (mode == "password") {
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

    // 2) 手机 + 短信验证码登录
    else if (mode == "sms") {
        int         step  = rep.value("step", 1);
        std::string phone = rep.value("phone", "");

        // step=1: 发送验证码
        if (step == 1) {
            SmsResult r = sms_.sendCode(phone);
            resp["ok"]  = r.ok;
            resp["msg"] = r.msg;
            return resp.dump() + "\n";
        }

        // step=2: 校验验证码并登录
        if (step == 2) {
            std::string code = rep.value("code", "");

            // A. 校验验证码（Redis）
            SmsResult r = sms_.verifyCode(phone, code);
            if (!r.ok) {
                resp["ok"]  = false;
                resp["msg"] = r.msg;
                return resp.dump() + "\n";
            }

            // B. MySQL 里按 phone 找用户
            int         uid = 0;
            std::string username;
            if (!auth_.loginByPhone(phone, uid, username)) {
                resp["ok"]  = false;
                resp["msg"] = "phone not registered";
                return resp.dump() + "\n";
            }

            // C. 登录成功
            c.authed = true;
            c.userId = uid;
            c.name   = username;

            resp["ok"]  = true;
            resp["msg"] = "login success (sms)";
            return resp.dump() + "\n";
        }

        // 其它 step 非法
        resp["ok"]  = false;
        resp["msg"] = "invalid step for sms login";
    }
}

        // ---------------- register ----------------
else if (cmd == "register") {
    int         step  = rep.value("step", 1);
    std::string phone = rep.value("phone", "");

    // step=1: 发送验证码
    if (step == 1) {
        SmsResult r = sms_.sendCode(phone);
        resp["ok"]  = r.ok;
        resp["msg"] = r.msg;
        return resp.dump() + "\n";
    }

    // step=2: 验证码 + 用户名 + 两次密码
    if (step == 2) {
        std::string code  = rep.value("code", "");
        std::string user  = rep.value("user", "");
        std::string pass  = rep.value("pass", "");
        std::string pass2 = rep.value("pass2", "");

        // 1. 两次密码必须一致
        if (pass != pass2) {
            resp["ok"]  = false;
            resp["msg"] = "two passwords not match";
            return resp.dump() + "\n";
        }

        // 2. 校验验证码
        SmsResult r = sms_.verifyCode(phone, code);
        if (!r.ok) {
            resp["ok"]  = false;
            resp["msg"] = r.msg;
            return resp.dump() + "\n";
        }

        // 3. 调 AuthService 注册
        int uid = 0;
        if (auth_.Register(phone, user, pass, uid)) {
            resp["ok"]     = true;
            resp["msg"]    = "register success";
            resp["userId"] = uid;
            resp["user"]   = user;
        } else {
            resp["ok"]  = false;
            resp["msg"] = "register failed";
        }
        return resp.dump() + "\n";
    }

    resp["ok"]  = false;
    resp["msg"] = "invalid step for register";
}

        //      // ---------------- send_sms ----------------
        // else if (cmd == "send_sms") {
        //     std::string phone = rep.value("phone", "");
        //     SmsResult r       = sms_.sendCode(phone);
        //     resp["ok"]        = r.ok;
        //     resp["msg"]       = r.msg;
        // }

        // // ---------------- verify_sms ----------------
        // else if (cmd == "verify_sms") {
        //     std::string phone = rep.value("phone", "");
        //     std::string code  = rep.value("code", "");

        //     SmsResult r = sms_.verifyCode(phone, code);
        //     resp["ok"]  = r.ok;
        //     resp["msg"] = r.msg;

        //     // 这里以后可以选择：
        //     // 验证成功后自动帮用户完成登录/注册逻辑
        //     // 例如标记一个手机号已验证，或去调用 AuthService
        // }

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
