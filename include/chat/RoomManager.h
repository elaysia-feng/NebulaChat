#pragma once 

#include <mutex>
#include <unordered_map>

class RoomManager
{
private:
    RoomManager() = default;

    std::mutex  mtx_;
    // roomId -> 人数
    std::unordered_map<int, int> roomCounts_;
public:
    static RoomManager& Instance();

    // 尝试进入房间：如果人数 < maxSize 则 +1 并返回 true，否则返回 false
    bool tryEnterRoom(int roomId, int maxSize);

    // 离开房间：人数 -1（不要减到负数）
    void leaveRoom(int roomId);

    // 获取房间人数
    int getRoomSize(int roomId);
};

RoomManager& RoomManager::Instance(){
    static RoomManager inst;
    return inst;
}

bool RoomManager::tryEnterRoom(int roomId, int maxSize){
    std::lock_guard<std::mutex> lock(mtx_);
    int& cnt = roomCounts_[roomId];
    if(cnt >= maxSize) return false;
    ++cnt;
    return true;
}

void RoomManager::leaveRoom(int roomId){
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = roomCounts_.find(roomId);
    if(it == roomCounts_.end()) return;
    if (it->second > 0) {
        --it->second;
    }
    return;
}

int RoomManager::getRoomSize(int roomId){
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = roomCounts_.find(roomId);
    if(it == roomCounts_.end()) return 0;
    return it->second;
}