#include "telemetry/logger.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "common/types.hpp"

namespace prodxcloud::telemetry {

std::shared_ptr<spdlog::logger> StructuredLogger::logger_;
std::unordered_map<std::string, std::string> StructuredLogger::default_fields_;
bool StructuredLogger::json_output_ = true;

void StructuredLogger::init(LogLevel level, bool json_output) {
    json_output_ = json_output;
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file    = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/prodxcloud.log", 100 * 1024 * 1024, 5);
    std::vector<spdlog::sink_ptr> sinks = {console, file};
    logger_ = std::make_shared<spdlog::logger>("prodxcloud", sinks.begin(), sinks.end());

    switch (level) {
        case LogLevel::DEBUG: logger_->set_level(spdlog::level::debug); break;
        case LogLevel::INFO:  logger_->set_level(spdlog::level::info); break;
        case LogLevel::WARN:  logger_->set_level(spdlog::level::warn); break;
        case LogLevel::ERROR: logger_->set_level(spdlog::level::err); break;
    }

    if (json_output) logger_->set_pattern("%v");
    else logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

    spdlog::set_default_logger(logger_);
}

std::shared_ptr<spdlog::logger> StructuredLogger::get() {
    if (!logger_) init();
    return logger_;
}

void StructuredLogger::log(LogLevel level, const std::string& message,
                             const std::unordered_map<std::string, std::string>& metadata) {
    if (!logger_) init();

    if (json_output_) {
        nlohmann::json j;
        j["timestamp"] = prodxcloud::now_iso8601();
        j["message"]   = message;
        switch (level) {
            case LogLevel::DEBUG: j["level"] = "DEBUG"; break;
            case LogLevel::INFO:  j["level"] = "INFO"; break;
            case LogLevel::WARN:  j["level"] = "WARN"; break;
            case LogLevel::ERROR: j["level"] = "ERROR"; break;
        }
        for (const auto& [k, v] : default_fields_) j[k] = v;
        if (!metadata.empty()) {
            nlohmann::json m;
            for (const auto& [k, v] : metadata) m[k] = v;
            j["metadata"] = m;
        }
        std::string s = j.dump();
        switch (level) {
            case LogLevel::DEBUG: logger_->debug(s); break;
            case LogLevel::INFO:  logger_->info(s); break;
            case LogLevel::WARN:  logger_->warn(s); break;
            case LogLevel::ERROR: logger_->error(s); break;
        }
    } else {
        switch (level) {
            case LogLevel::DEBUG: logger_->debug(message); break;
            case LogLevel::INFO:  logger_->info(message); break;
            case LogLevel::WARN:  logger_->warn(message); break;
            case LogLevel::ERROR: logger_->error(message); break;
        }
    }
}

void StructuredLogger::info(const std::string& msg, const std::string& t, const std::string& tr) {
    std::unordered_map<std::string, std::string> m;
    if (!t.empty()) m["tenant_id"] = t;
    if (!tr.empty()) m["trace_id"] = tr;
    log(LogLevel::INFO, msg, m);
}
void StructuredLogger::warn(const std::string& msg, const std::string& t, const std::string& tr) {
    std::unordered_map<std::string, std::string> m;
    if (!t.empty()) m["tenant_id"] = t;
    if (!tr.empty()) m["trace_id"] = tr;
    log(LogLevel::WARN, msg, m);
}
void StructuredLogger::error(const std::string& msg, const std::string& t, const std::string& tr) {
    std::unordered_map<std::string, std::string> m;
    if (!t.empty()) m["tenant_id"] = t;
    if (!tr.empty()) m["trace_id"] = tr;
    log(LogLevel::ERROR, msg, m);
}
void StructuredLogger::debug(const std::string& msg, const std::string& t, const std::string& tr) {
    std::unordered_map<std::string, std::string> m;
    if (!t.empty()) m["tenant_id"] = t;
    if (!tr.empty()) m["trace_id"] = tr;
    log(LogLevel::DEBUG, msg, m);
}
void StructuredLogger::set_default_field(const std::string& key, const std::string& value) {
    default_fields_[key] = value;
}

}  // namespace prodxcloud::telemetry
