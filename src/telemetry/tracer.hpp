#pragma once

/// @file tracer.hpp
/// @brief OpenTelemetry-compatible span tracer with RAII scope management.

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace prodxcloud::telemetry {

class Span {
public:
    Span(std::string name, std::string trace_id, std::string span_id,
         std::optional<std::string> parent_span_id = std::nullopt);
    ~Span();

    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;
    Span(Span&& other) noexcept;
    Span& operator=(Span&& other) noexcept;

    void set_attribute(const std::string& key, const std::string& value);
    void add_event(const std::string& name,
                   const std::unordered_map<std::string, std::string>& attrs = {});
    void set_error(const std::string& message);
    void end();

    [[nodiscard]] const std::string& trace_id() const { return trace_id_; }
    [[nodiscard]] const std::string& span_id() const { return span_id_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] double duration_ms() const;

private:
    std::string name_;
    std::string trace_id_;
    std::string span_id_;
    std::optional<std::string> parent_span_id_;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point end_time_;
    bool ended_ = false;
    bool error_ = false;
    std::string error_message_;
    std::unordered_map<std::string, std::string> attributes_;

    struct Event {
        std::string name;
        std::chrono::steady_clock::time_point timestamp;
        std::unordered_map<std::string, std::string> attributes;
    };
    std::vector<Event> events_;
};

class Tracer {
public:
    explicit Tracer(const std::string& service_name = "prodxcloud-ai-engine",
                     const std::string& otlp_endpoint = "");
    ~Tracer() = default;

    Span start_span(const std::string& name);
    Span start_span(const std::string& name, const Span& parent);

    [[nodiscard]] const std::string& service_name() const { return service_name_; }

private:
    std::string service_name_;
    std::string otlp_endpoint_;
    std::string generate_trace_id() const;
    std::string generate_span_id() const;
};

}  // namespace prodxcloud::telemetry
