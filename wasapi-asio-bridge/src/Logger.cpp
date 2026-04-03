#include "Logger.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace Bridge {

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

void Logger::setFileOutput(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_file.open(path, std::ios::app);
}

void Logger::setCallback(std::function<void(LogLevel, const std::string&)> cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = std::move(cb);
}

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < m_level) return;

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    const char* lvlStr = "INFO";
    switch (level) {
        case LogLevel::Debug: lvlStr = "DEBUG"; break;
        case LogLevel::Info:  lvlStr = "INFO "; break;
        case LogLevel::Warn:  lvlStr = "WARN "; break;
        case LogLevel::Error: lvlStr = "ERROR"; break;
    }

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count()
        << " [" << lvlStr << "] " << msg;
    std::string line = oss.str();

    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << line << '\n';
    if (m_file.is_open()) m_file << line << '\n';
    if (m_callback) m_callback(level, msg);
}

} // namespace Bridge
