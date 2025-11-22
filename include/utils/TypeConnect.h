#pragma once 
#include <string>
#include <atomic>


namespace utils {
    
/*这个Connection的成员有
fd,inbuf,outbuf;

wantWrite,shortClose;

authed,userId,name,roomId*/
struct Connection 
{
    /*为每个连接创建 Session（会话状态）*/
    int fd{-1};
    std::string inbuf;
    std::string outbuf;

    // I/O 状态
    std::atomic<bool> wantWrite{false};
    std::atomic<bool> shortClose{false};

    // Session 状态
    //标记这个连接的用户是否“已经登录成功”
    bool authed{false};     // 是否已登录
    int userId{0};          // 用户ID
    std::string name;            // 用户名
    int roomId{0};          // 所在聊天室
};
}