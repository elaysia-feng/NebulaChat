#include "chat/MessageHandler.h"
#include "chat/RoomManager.h"
#include "chat/ChatHistory.h"
#include <iostream>
#include <chrono>

constexpr int MAX_ROOM_SIZE = 100;

using json = nlohmann::json;

std::string MessageHandler::handleMessage(Connection& c, const std::string& line) {
    json resp;
    try {
        json        rep = json::parse(line);
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

                    // 尝试进入 1 号房间
                    auto& roomMgr = RoomManager::Instance();
                    if (roomMgr.tryEnterRoom(1, MAX_ROOM_SIZE)) {
                        c.roomId      = 1;
                        resp["roomId"] = 1;
                        resp["msg"]    = "login success";
                    } else {
                        c.roomId      = 0;  // 没有房间
                        resp["roomId"] = 0;
                        resp["msg"]    = "login success, but room 1 is full";
                    }

                    resp["ok"]  = true;
                } else {
                    resp["ok"]  = false;
                    resp["msg"] = "wrong username or password";
                }

                return resp.dump() + "\n";
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
                else if (step == 2) {
                    std::string code = rep.value("code", "");

                    // A. 校验验证码（Redis）
                    SmsResult r = sms_.verifyCode(phone, code);
                    if (!r.ok) {
                        resp["ok"]  = false;
                        resp["msg"] = r.msg;
                        return resp.dump() + "\n";
                    }

                    // B.通过 AuthService 按 phone 找用户（内部会走本地缓存 + Redis + MySQL）
                    int         uid      = 0;
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

                    // 尝试进入 1 号房间
                    auto& roomMgr = RoomManager::Instance();
                    if (roomMgr.tryEnterRoom(1, MAX_ROOM_SIZE)) {
                        c.roomId      = 1;
                        resp["roomId"] = 1;
                        resp["msg"]    = "login success";
                    } else {
                        c.roomId      = 0;  // 没有房间
                        resp["roomId"] = 0;
                        resp["msg"]    = "login success, but room 1 is full";
                    }

                    resp["ok"]  = true;
                    return resp.dump() + "\n";
                }

                // 其它 step 值非法
                resp["ok"]  = false;
                resp["msg"] = "invalid step for sms login";
                return resp.dump() + "\n";
            }

            // 其它 mode 非法
            resp["ok"]  = false;
            resp["msg"] = "invalid login mode";
            return resp.dump() + "\n";
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
            return resp.dump() + "\n";
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
            return resp.dump() + "\n";
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
            return resp.dump() + "\n";
        }

        // 加入/切换房间（切换聊天室，带容量限制）
        else if (cmd == "join_room") {
            if (!c.authed || c.userId <= 0) {
                resp["ok"]  = false;
                resp["msg"] = "not authed";
                return resp.dump() + "\n";
            }

            int newRoomId = rep.value("roomId", 1);
            if (newRoomId <= 0) newRoomId = 1;

            int oldRoomId = c.roomId;
            if (newRoomId == oldRoomId) {
                // 已经在这个房间了
                resp["ok"]     = true;
                resp["roomId"] = newRoomId;
                resp["msg"]    = "already in this room";
                return resp.dump() + "\n";
            }

            auto& roomMgr = RoomManager::Instance();
            if (!roomMgr.tryEnterRoom(newRoomId, MAX_ROOM_SIZE)) {
                resp["ok"]     = false;
                resp["msg"]    = "room is full";
                resp["roomId"] = oldRoomId; // 保持原房间不变
                return resp.dump() + "\n";
            }

            // 成功进入新房间后，再从旧房间退出（如果旧房间有效）
            if (oldRoomId > 0) {
                roomMgr.leaveRoom(oldRoomId);
            }

            c.roomId       = newRoomId;
            resp["ok"]     = true;
            resp["roomId"] = newRoomId;
            resp["msg"]    = "join room success";
            return resp.dump() + "\n";
        }

        // 发送消息
        else if (cmd == "send_msg") {
            if (!c.authed || c.userId <= 0) {
                resp["ok"]  = false;
                resp["msg"] = "not authed";
                return resp.dump() + "\n";
            }

            std::string text = rep.value("text", "");
            if (text.empty()) {
                resp["ok"]  = false;
                resp["msg"] = "text cannot be empty";
                return resp.dump() + "\n";
            }

            int roomId = c.roomId;
            if (roomId <= 0) roomId = 1;

            // 这里先只做内存广播 + 回包，不做 DB 持久化，后面加历史消息 + 缓存
            resp["ok"]        = true;
            resp["broadcast"] = true;      // 关键！告诉 Server：这是广播消息
            resp["roomId"]    = roomId;
            resp["fromId"]    = c.userId;
            resp["fromName"]  = c.name;
            resp["text"]      = text;

            // 简单时间戳（秒），客户端要精确再说
            resp["ts"] = static_cast<long long>(
                std::chrono::system_clock::to_time_t(
                    std::chrono::system_clock::now()
                )
            );

            // 持久化并清理缓存（若失败仅记录日志，不影响即时广播）
            try {
                chat::SaveMessage(roomId, c.userId, c.name, text);
                chat::InvalidateHistoryCache(roomId);
            } catch (const std::exception& e) {
                std::cerr << "[send_msg] persist failed: " << e.what() << std::endl;
            }

            return resp.dump() + "\n";
        }

        // 拉取历史消息（带 Redis 缓存 + 防缓存击穿）
        else if (cmd == "get_history") {
            if (!c.authed || c.userId <= 0) {
                resp["ok"]  = false;
                resp["msg"] = "not authed";
                return resp.dump() + "\n";
            }

            int roomId = c.roomId;
            if (roomId <= 0) {
                resp["ok"]  = false;
                resp["msg"] = "not in any room";
                return resp.dump() + "\n";
            }

            int limit = rep.value("limit", 10);
            if (limit <= 0) {
                resp["ok"]  = false;
                resp["msg"] = "invalid limit";
                return resp.dump() + "\n";
            }

            json history;
            if (!chat::GetHistoryWithCache(roomId, limit, history)) {
                resp["ok"]  = false;
                resp["msg"] = "get history failed";
                return resp.dump() + "\n";
            }

            resp["ok"]      = true;
            resp["roomId"]  = roomId;
            resp["history"] = history;
            return resp.dump() + "\n";
        }

        // 房间列表（实时人数）
        else if (cmd == "list_rooms") {
            auto rooms = RoomManager::Instance().snapshot();
            resp["ok"]    = true;
            resp["rooms"] = json::array();
            for (auto& kv : rooms) {
                json item;
                item["roomId"] = kv.first;
                item["size"]   = kv.second;
                resp["rooms"].push_back(std::move(item));
            }
            return resp.dump() + "\n";
        }

        // 主动离开房间
        else if (cmd == "leave_room") {
            if (!c.authed || c.userId <= 0) {
                resp["ok"]  = false;
                resp["msg"] = "not authed";
                return resp.dump() + "\n";
            }
            if (c.roomId > 0) {
                RoomManager::Instance().leaveRoom(c.roomId);
                c.roomId = 0;
            }
            resp["ok"]  = true;
            resp["msg"] = "leave room success";
            return resp.dump() + "\n";
        }


        // ========= echo =========
        else if (cmd == "echo") {
            std::string msg = rep.value("msg", "");
            resp["ok"]   = true;
            resp["data"] = msg;
            return resp.dump() + "\n";
        }

        // ========= upper =========
        else if (cmd == "upper") {
            std::string msg = rep.value("msg", "");
            for (auto& ch : msg) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            resp["ok"]   = true;
            resp["data"] = msg;
            return resp.dump() + "\n";
        }

        // ========= quit =========
        else if (cmd == "quit") {
            resp["ok"]    = true;
            resp["data"]  = "bye";
            resp["close"] = true;
            return resp.dump() + "\n";
        }

        else {
            resp["ok"]  = false;
            resp["err"] = "unknown cmd";
            return resp.dump() + "\n";
        }

    } catch (const std::exception& e) {
        resp["ok"]  = false;
        resp["err"] = e.what();
        return resp.dump() + "\n";
    }
}
