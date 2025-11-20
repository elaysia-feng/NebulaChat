#include "chat/MessageHandler.h"
#include "core/Server.h"
#include <iostream>

using json = nlohmann::json;

std::string MessageHandler::handleMessage(Connection& c, const std::string& line) {
    json resp;
    try {
        json rep = json::parse(line);
        std::string cmd = rep.value("cmd", "");

        // ========= 鉴权 =========
        // 未登录只能执行：login / register / reset_pass（找回密码）
        if (!c.authed &&
            cmd != "login" &&
            cmd != "register" &&
            cmd != "reset_pass") {
            resp["ok"]  = false;
            resp["err"] = "please login first";
            return resp.dump() + "\n";
        }

        // ========= login =========
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

                // step = 1 : 发送验证码
                if (step == 1) {
                    SmsResult r = sms_.sendCode(phone);
                    resp["ok"]  = r.ok;
                    resp["msg"] = r.msg;
                    return resp.dump() + "\n";
                }

                // step = 2 : 验证码 + 手机号登录
                if (step == 2) {
                    std::string code = rep.value("code", "");

                    // A. 校验验证码（Redis）
                    SmsResult r = sms_.verifyCode(phone, code);
                    if (!r.ok) {
                        resp["ok"]  = false;
                        resp["msg"] = r.msg;
                        return resp.dump() + "\n";
                    }

                    // B.通过 AuthService 按 phone 找用户（内部会走本地缓存 + Redis + MySQL）
                    int         uid = 0;
                    std::string username;
                    if (!auth_.loginByPhone(phone, uid, username)) {
                        resp["ok"]  = false;
                        resp["msg"] = "phone not registered";
                        return resp.dump() + "\n";
                    }

                    // C. 登录成功，更新会话
                    c.authed = true;
                    c.userId = uid;
                    c.name   = username;

                    resp["ok"]  = true;
                    resp["msg"] = "login success (sms)";
                    return resp.dump() + "\n";
                }

                resp["ok"]  = false;
                resp["msg"] = "invalid step for sms login";
            }
        }

        // ========= register（带短信验证码） =========
        else if (cmd == "register") {
            int         step  = rep.value("step", 1);
            std::string phone = rep.value("phone", "");

            // step = 1 : 发送验证码
            if (step == 1) {
                SmsResult r = sms_.sendCode(phone);
                resp["ok"]  = r.ok;
                resp["msg"] = r.msg;
                return resp.dump() + "\n";
            }

            // step = 2 : 校验验证码 + 用户名 + 两次密码
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

        // ========= update_name（改昵称，需要已登录） =========
        else if (cmd == "update_name") {
            // 必须已经登录，这里多判一次更安全
            if (!c.authed || c.userId <= 0) {
                resp["ok"]  = false;
                resp["msg"] = "not authed";
                return resp.dump() + "\n";
            }

            std::string newName = rep.value("newName", "");
            if (newName.empty()) {
                resp["ok"]  = false;
                resp["msg"] = "newName cannot be empty";
                return resp.dump() + "\n";
            }

            std::string oldName;
            std::string phone;
            // 这里的 userId 从 Connection 里拿，用户自己是不需要知道 id 的
            if (auth_.updateUsername(c.userId, newName, oldName, phone)) {
                c.name = newName;  // 更新会话中的名字

                resp["ok"]       = true;
                resp["msg"]      = "update username success";
                resp["oldName"]  = oldName;
                resp["newName"]  = newName;
                resp["phone"]    = phone;
            } else {
                resp["ok"]  = false;
                resp["msg"] = "update username failed";
            }
        }

        // ========= reset_pass（忘记密码：手机号 + 短信验证码） =========
        else if (cmd == "reset_pass") {
            int         step  = rep.value("step", 1);
            std::string phone = rep.value("phone", "");

            // step = 1 : 发送验证码
            if (step == 1) {
                SmsResult r = sms_.sendCode(phone);
                resp["ok"]  = r.ok;
                resp["msg"] = r.msg;
                return resp.dump() + "\n";
            }

            // step = 2 : 校验验证码 + 设置新密码
            if (step == 2) {
                std::string code    = rep.value("code", "");
                std::string newPass = rep.value("newPass", "");

                if (newPass.empty()) {
                    resp["ok"]  = false;
                    resp["msg"] = "newPass cannot be empty";
                    return resp.dump() + "\n";
                }

                // 1. 校验验证码
                SmsResult r = sms_.verifyCode(phone, code);
                if (!r.ok) {
                    resp["ok"]  = false;
                    resp["msg"] = r.msg;
                    return resp.dump() + "\n";
                }

                // 2. 更新数据库密码（并清理对应缓存）
                if (auth_.resetPasswordByPhone(phone, newPass)) {
                    resp["ok"]  = true;
                    resp["msg"] = "reset password success";
                } else {
                    resp["ok"]  = false;
                    resp["msg"] = "reset password failed";
                }
                return resp.dump() + "\n";
            }

            resp["ok"]  = false;
            resp["msg"] = "invalid step for reset_pass";
        }

        // ========= echo =========
        else if (cmd == "echo") {
            std::string msg = rep.value("msg", "");
            resp["ok"]   = true;
            resp["data"] = msg;
        }

        // ========= upper =========
        else if (cmd == "upper") {
            std::string msg = rep.value("msg", "");
            for (auto& ch : msg) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            resp["ok"]   = true;
            resp["data"] = msg;
        }

        // ========= quit =========
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
