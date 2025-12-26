#pragma once

#include <string>
#include "SmsService.h"

class AuthService
{
private:
    
    SmsService sms_;

    // 按用户名加载用户认证信息（只负责取数据，不校验密码）
    // 返回：true = 找到了这个用户；false = 用户不存在
    bool loadUserByName(const std::string& username,
                        int&               idOut,
                        std::string&       passHashOut);

    // 按手机号加载用户基础信息（走本地 L1 + Redis + DB 的多级缓存）
    // 返回：true = 查到这个手机号对应的用户；false = 用户不存在
    bool loadUserByPhone(const std::string& phone,
                         int&               idOut,
                         std::string&       usernameOut);

    // 失效 / 预热用户缓存（给注册 / 改名 / 改密码用）
    void invalidateUserCacheByName(const std::string& username);
    void invalidateUserCacheByPhone(const std::string& phone);
    void warmUserCacheByPhone(int                userId,
                              const std::string& phone,
                              const std::string& username);

public:
    // 用户名 + 密码登录
    bool login(const std::string& user,
               const std::string& pass,
               int&               userId);

    // 手机号登录（验证码逻辑你可以以后接 SmsService 进去）
    // 外部根本不需要关心“是不是空值缓存命中”，只要知道登上 / 没登上就行
    bool loginByPhone(const std::string& phone,
                      int&               userId,
                      std::string&       usernameOut);

    // 注册
    bool Register(const std::string& phone,
                  const std::string& user,
                  const std::string& pass,
                  int&               userId);

    // 更新用户名（根据 userId 改名）
    bool updateUsername(int                userId,
                        const std::string& newName,
                        std::string&       oldNameOut,
                        std::string&       phoneOut);

    // 通过手机号重置密码
    bool resetPasswordByPhone(const std::string& phone,
                              const std::string& newPass);
};
