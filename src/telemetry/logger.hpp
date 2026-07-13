#pragma once

/// @file logger.hpp
/// @brief Structured JSON logger wrapping spdlog.

#include <memory>
#include <string>
#include <unordered_map>

#include <spdlog/spdlog.h>

namespace prodxcloud::telemetry {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class StructuredLogger {
public:
    static void init(LogLevel level = LogLevel::INFO, bool json_output = true);
    static std::shared_ptr<spdlog::logger> get();
    static void log(LogLevel level, const std::string& message,
                    const std::unordered_map<std::string, std::string>& metadata = {});
    static void info(const std::string& msg, const std::string& tenant = "",
                     const std::string& trace = "");
    static void warn(const std::string& msg, const std::string& tenant = "",
                     const std::string& trace = "");
    static void error(const std::string& msg, const std::string& tenant = "",
                      const std::string& trace = "");
    static void debug(const std::string& msg, const std::string& tenant = "",
                      const std::string& trace = "");
    static void set_default_field(const std::string& key, const std::string& value);

private:
    static std::shared_ptr<spdlog::logger> logger_;
    static std::unordered_map<std::string, std::string> default_fields_;
    static bool json_output_;
};

}  // namespace prodxcloud::telemetry
