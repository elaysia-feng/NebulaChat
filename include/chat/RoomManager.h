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
    static RoomManager& Instance(){
        static RoomManager inst;
        return inst;
    }

    // 尝试进入房间：如果人数 < maxSize 则 +1 并返回 true，否则返回 false
    bool tryEnterRoom(int roomId, int maxSize){
         std::lock_guard<std::mutex> lock(mtx_);
        int& cnt = roomCounts_[roomId];
        if(cnt >= maxSize) return false;
        ++cnt;
        return true;
    }

    // 离开房间：人数 -1（不要减到负数）
    void leaveRoom(int roomId){
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = roomCounts_.find(roomId);
        if(it == roomCounts_.end()) return;
        if (it->second > 0) {
            --it->second;
        }
        return;
    }

    // 获取房间人数
    int getRoomSize(int roomId){
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = roomCounts_.find(roomId);
        if(it == roomCounts_.end()) return 0;
        return it->second;
    }

    std::unordered_map<int, int> snapshot(){
        std::lock_guard<std::mutex> lock(mtx_);
        return roomCounts_;
    }
};
