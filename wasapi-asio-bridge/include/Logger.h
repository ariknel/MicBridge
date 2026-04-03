#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <functional>
#include <atomic>

namespace Bridge {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static Logger& get();

    void setLevel(LogLevel level) { m_level = level; }
    void setFileOutput(const std::string& path);
    void setCallback(std::function<void(LogLevel, const std::string&)> cb);

    void log(LogLevel level, const std::string& msg);

    void debug(const std::string& msg) { log(LogLevel::Debug, msg); }
    void info (const std::string& msg) { log(LogLevel::Info,  msg); }
    void warn (const std::string& msg) { log(LogLevel::Warn,  msg); }
    void error(const std::string& msg) { log(LogLevel::Error, msg); }

private:
    Logger() = default;
    LogLevel m_level { LogLevel::Info };
    std::ofstream m_file;
    std::mutex m_mutex;
    std::function<void(LogLevel, const std::string&)> m_callback;
};

#define LOG_DEBUG(msg) Bridge::Logger::get().debug(msg)
#define LOG_INFO(msg)  Bridge::Logger::get().info(msg)
#define LOG_WARN(msg)  Bridge::Logger::get().warn(msg)
#define LOG_ERROR(msg) Bridge::Logger::get().error(msg)

} // namespace Bridge
