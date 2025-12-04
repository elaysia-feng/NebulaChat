#pragma once

#include <nlohmann/json.hpp>
#include <atomic>
#include <vector>

namespace chat {

using json = nlohmann::json;

// 供 send_msg 调：保存单条消息到 DB（失败不影响主流程）
void SaveMessage(int roomId,
                 int userId,
                 const std::string& username,
                 const std::string& text);

// 供 get_history 调：带 Redis 缓存 + 互斥锁防缓存击穿
// 成功返回 true，historyOut 里填好 JSON 数组
bool GetHistoryWithCache(int roomId,
                         int limit,
                         json& historyOut);

// 删除/刷新某个房间的历史缓存（通常在写入新消息后调用）
void InvalidateHistoryCache(int roomId,
                            const std::vector<int>& limitsHint = {});

}
