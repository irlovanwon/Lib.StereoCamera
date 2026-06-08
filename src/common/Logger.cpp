#include "stereo_camera/common/Logger.h"
#include <iostream>
#include <ctime>
#include <sstream>

namespace stereo_camera {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(Level level) {
    level_ = level;
}

void Logger::log(Level level, const std::string& category, const std::string& message) {
    if (level < level_) return;

    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));

    std::ostringstream oss;
    oss << "[" << time_buf << "] [" << level_str[static_cast<int>(level)]
        << "] [" << category << "] " << message;
    std::cerr << oss.str() << std::endl;
}

void Logger::debug(const std::string& category, const std::string& msg) { log(Level::Debug, category, msg); }
void Logger::info(const std::string& category, const std::string& msg)  { log(Level::Info, category, msg); }
void Logger::warn(const std::string& category, const std::string& msg)  { log(Level::Warn, category, msg); }
void Logger::error(const std::string& category, const std::string& msg) { log(Level::Error, category, msg); }

} // namespace stereo_camera
