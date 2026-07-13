#pragma once

/// @file metrics.hpp
/// @brief Prometheus metrics singleton for inference, agent, and API metrics.

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace prodxcloud::telemetry {

struct CounterValue {
    std::atomic<int64_t> value{0};
    void increment(int64_t delta = 1) { value.fetch_add(delta, std::memory_order_relaxed); }
    [[nodiscard]] int64_t get() const { return value.load(std::memory_order_relaxed); }
};

struct HistogramValue {
    std::mutex mutex;
    std::vector<double> values;
    double sum   = 0.0;
    int64_t count = 0;

    void observe(double value);
    [[nodiscard]] double mean() const;
    [[nodiscard]] double percentile(double p) const;
    std::string serialize(const std::string& name, const std::string& labels) const;
};

struct GaugeValue {
    std::atomic<int64_t> value{0};
    void set(int64_t v) { value.store(v, std::memory_order_relaxed); }
    void increment(int64_t delta = 1) { value.fetch_add(delta, std::memory_order_relaxed); }
    void decrement(int64_t delta = 1) { value.fetch_sub(delta, std::memory_order_relaxed); }
    [[nodiscard]] int64_t get() const { return value.load(std::memory_order_relaxed); }
};

class PrometheusMetrics {
public:
    static PrometheusMetrics& instance();

    PrometheusMetrics(const PrometheusMetrics&) = delete;
    PrometheusMetrics& operator=(const PrometheusMetrics&) = delete;

    void record_inference(const std::string& tenant_id, const std::string& model_id,
                           const std::string& status);
    int64_t inference_count(const std::string& tenant_id, const std::string& model_id,
                             const std::string& status) const;
    void record_latency(const std::string& model_id, double latency_ms);
    void set_active_agents(const std::string& tenant_id, int64_t count);
    void agent_started(const std::string& tenant_id);
    void agent_stopped(const std::string& tenant_id);
    std::string serialize() const;

private:
    PrometheusMetrics() = default;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<CounterValue>> inference_counters_;
    std::unordered_map<std::string, std::unique_ptr<HistogramValue>> latency_histograms_;
    std::unordered_map<std::string, std::unique_ptr<GaugeValue>> active_agents_;

    CounterValue& get_counter(const std::string& key);
    HistogramValue& get_histogram(const std::string& key);
    GaugeValue& get_gauge(const std::string& key);
};

}  // namespace prodxcloud::telemetry
