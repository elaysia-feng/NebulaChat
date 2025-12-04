#pragma once
#include <fstream>
#include <string>
#include <mutex>
#include <filesystem>
#include <cstring>
#include <sstream>
#include <ctime>
#include <cstdio>

enum class LogLevel{
    INFO,
    WARN,
    ERROR,
    DEBUG
};


class Logger
{
private:
    std::mutex mtx_;

    std::ofstream info_;
    std::ofstream warn_;
    std::ofstream error_;
    std::ofstream debug_;
private:
    Logger(){
    // 获取可执行文件所在绝对路径
    std::string exePath = std::filesystem::canonical("/proc/self/exe").string();
    
    // 得到可执行文件目录（/NebulaChat/build/）
    auto exeDir = std::filesystem::path(exePath).parent_path();

    // 得到项目根目录（/NebulaChat）
    auto projectRoot = exeDir.parent_path();

    auto logDir_ = projectRoot / "logs";

    std::filesystem::create_directory(logDir_);

    info_.open(logDir_ / "info.log",  std::ios::app);
    warn_.open(logDir_ / "warn.log",  std::ios::app);
    error_.open(logDir_ / "error.log", std::ios::app);
    debug_.open(logDir_ / "debug.log", std::ios::app);
    }

    ~Logger(){
        info_.close();
        warn_.close();
        error_.close();
        debug_.close();
    }

private:
    /*这个是返回对应选择的fstream*/
    std::ofstream& getFileStream(LogLevel lvl) {
        switch (lvl) {
            case LogLevel::INFO: return info_;
            case LogLevel::WARN: return warn_;
            case LogLevel::ERROR: return error_;
            case LogLevel::DEBUG: return debug_;
        }
        return info_;
    }
    /*现在的作用就是在log日志里面打印出这几个字符串*/
    std::string levelToString(LogLevel lvl) {
        switch (lvl) {
            case LogLevel::INFO:  return "INFO";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::DEBUG: return "DEBUG";
        }
        return "INFO";
    }

    std::string timestamp() {
        time_t now = time(nullptr);
        tm* lt = localtime(&now);

        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                 lt->tm_hour, lt->tm_min, lt->tm_sec);

        return buf;
    }
public:
    static Logger& Instance(){
     static Logger instance;
     return instance;
   }

   void log(LogLevel level,const std::string& msg){
    std::lock_guard<std::mutex> lock(mtx_);

    auto& file = getFileStream(level);
    file << timestamp() << " | " << levelToString(level) << " | " << msg << "\n";
        file.flush();  // 重要：防止崩溃时没写入文件
   }
};
// 简化宏
#define LOG_INFO(msg)  do{std::ostringstream oss; oss << msg; Logger::Instance().log(LogLevel::INFO, oss.str());} while (0);
#define LOG_WARN(msg)  do{std::ostringstream oss; oss << msg; Logger::Instance().log(LogLevel::WARN, oss.str());} while (0);
#define LOG_ERROR(msg)  do{std::ostringstream oss; oss << msg; Logger::Instance().log(LogLevel::ERROR, oss.str());} while (0);
#define LOG_DEBUG(msg)  do{std::ostringstream oss; oss << msg; Logger::Instance().log(LogLevel::DEBUG, oss.str());} while (0);
