#include "telemetry/tracer.hpp"

#include <iomanip>
#include <random>
#include <sstream>
#include <spdlog/spdlog.h>

namespace prodxcloud::telemetry {

Span::Span(std::string name, std::string trace_id, std::string span_id,
           std::optional<std::string> parent_span_id)
    : name_(std::move(name)),
      trace_id_(std::move(trace_id)),
      span_id_(std::move(span_id)),
      parent_span_id_(std::move(parent_span_id)),
      start_time_(std::chrono::steady_clock::now()) {}

Span::~Span() {
    if (!ended_) end();
}

Span::Span(Span&& o) noexcept
    : name_(std::move(o.name_)), trace_id_(std::move(o.trace_id_)),
      span_id_(std::move(o.span_id_)), parent_span_id_(std::move(o.parent_span_id_)),
      start_time_(o.start_time_), end_time_(o.end_time_), ended_(o.ended_),
      error_(o.error_), error_message_(std::move(o.error_message_)),
      attributes_(std::move(o.attributes_)), events_(std::move(o.events_)) {
    o.ended_ = true;
}

Span& Span::operator=(Span&& o) noexcept {
    if (this != &o) {
        if (!ended_) end();
        name_ = std::move(o.name_); trace_id_ = std::move(o.trace_id_);
        span_id_ = std::move(o.span_id_); parent_span_id_ = std::move(o.parent_span_id_);
        start_time_ = o.start_time_; end_time_ = o.end_time_; ended_ = o.ended_;
        error_ = o.error_; error_message_ = std::move(o.error_message_);
        attributes_ = std::move(o.attributes_); events_ = std::move(o.events_);
        o.ended_ = true;
    }
    return *this;
}

void Span::set_attribute(const std::string& key, const std::string& value) {
    attributes_[key] = value;
}

void Span::add_event(const std::string& name,
                      const std::unordered_map<std::string, std::string>& attrs) {
    events_.push_back({name, std::chrono::steady_clock::now(), attrs});
}

void Span::set_error(const std::string& message) {
    error_ = true;
    error_message_ = message;
    set_attribute("error", "true");
    set_attribute("error.message", message);
}

void Span::end() {
    if (ended_) return;
    ended_    = true;
    end_time_ = std::chrono::steady_clock::now();
    double dur = duration_ms();
    if (error_)
        spdlog::warn("Span error: name='{}', trace={}, dur={:.2f}ms, err={}", name_, trace_id_, dur, error_message_);
    else
        spdlog::debug("Span end: name='{}', trace={}, dur={:.2f}ms", name_, trace_id_, dur);
}

double Span::duration_ms() const {
    auto e = ended_ ? end_time_ : std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(e - start_time_).count();
}

Tracer::Tracer(const std::string& service_name, const std::string& otlp_endpoint)
    : service_name_(service_name), otlp_endpoint_(otlp_endpoint) {
    spdlog::info("Tracer initialized: service='{}', otlp='{}'",
                 service_name, otlp_endpoint.empty() ? "(none)" : otlp_endpoint);
}

Span Tracer::start_span(const std::string& name) {
    return Span(name, generate_trace_id(), generate_span_id());
}

Span Tracer::start_span(const std::string& name, const Span& parent) {
    return Span(name, parent.trace_id(), generate_span_id(), parent.span_id());
}

std::string Tracer::generate_trace_id() const {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(16) << dist(rng) << std::setw(16) << dist(rng);
    return os.str();
}

std::string Tracer::generate_span_id() const {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(16) << std::uniform_int_distribution<uint64_t>{}(rng);
    return os.str();
}

}  // namespace prodxcloud::telemetry
