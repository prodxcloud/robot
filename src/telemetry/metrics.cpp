#include "telemetry/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace prodxcloud::telemetry {

void HistogramValue::observe(double value) {
    std::lock_guard lock(mutex);
    values.push_back(value);
    sum += value;
    count++;
}

double HistogramValue::mean() const { return count > 0 ? sum / count : 0.0; }

double HistogramValue::percentile(double p) const {
    if (values.empty()) return 0.0;
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    double idx   = p * (sorted.size() - 1);
    size_t lower = static_cast<size_t>(idx);
    size_t upper = std::min(lower + 1, sorted.size() - 1);
    double frac  = idx - lower;
    return sorted[lower] * (1.0 - frac) + sorted[upper] * frac;
}

std::string HistogramValue::serialize(const std::string& name, const std::string& labels) const {
    std::ostringstream os;
    double buckets[] = {1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000};
    for (double bucket : buckets) {
        int64_t le_count = 0;
        for (double v : values)
            if (v <= bucket) le_count++;
        os << name << "_bucket{" << labels << ",le=\"" << bucket << "\"} " << le_count << "\n";
    }
    os << name << "_bucket{" << labels << ",le=\"+Inf\"} " << count << "\n";
    os << name << "_sum{" << labels << "} " << sum << "\n";
    os << name << "_count{" << labels << "} " << count << "\n";
    return os.str();
}

PrometheusMetrics& PrometheusMetrics::instance() {
    static PrometheusMetrics inst;
    return inst;
}

void PrometheusMetrics::record_inference(const std::string& tenant_id,
                                           const std::string& model_id,
                                           const std::string& status) {
    get_counter(tenant_id + ":" + model_id + ":" + status).increment();
}

int64_t PrometheusMetrics::inference_count(const std::string& tenant_id,
                                             const std::string& model_id,
                                             const std::string& status) const {
    std::string key = tenant_id + ":" + model_id + ":" + status;
    std::lock_guard lock(mutex_);
    auto it = inference_counters_.find(key);
    return it != inference_counters_.end() ? it->second->get() : 0;
}

void PrometheusMetrics::record_latency(const std::string& model_id, double latency_ms) {
    get_histogram(model_id).observe(latency_ms);
}

void PrometheusMetrics::set_active_agents(const std::string& tenant_id, int64_t count) {
    get_gauge(tenant_id).set(count);
}
void PrometheusMetrics::agent_started(const std::string& tenant_id) {
    get_gauge(tenant_id).increment();
}
void PrometheusMetrics::agent_stopped(const std::string& tenant_id) {
    get_gauge(tenant_id).decrement();
}

std::string PrometheusMetrics::serialize() const {
    std::lock_guard lock(mutex_);
    std::ostringstream os;

    os << "# HELP inference_requests_total Total inference requests\n"
       << "# TYPE inference_requests_total counter\n";
    for (const auto& [key, counter] : inference_counters_) {
        auto p1 = key.find(':');
        auto p2 = key.find(':', p1 + 1);
        os << "inference_requests_total{tenant_id=\"" << key.substr(0, p1)
           << "\",model_id=\"" << key.substr(p1 + 1, p2 - p1 - 1)
           << "\",status=\"" << key.substr(p2 + 1) << "\"} " << counter->get() << "\n";
    }

    os << "\n# HELP inference_latency_ms Inference latency in milliseconds\n"
       << "# TYPE inference_latency_ms histogram\n";
    for (const auto& [model_id, hist] : latency_histograms_) {
        os << hist->serialize("inference_latency_ms", "model_id=\"" + model_id + "\"");
    }

    os << "\n# HELP active_agents Number of active agents per tenant\n"
       << "# TYPE active_agents gauge\n";
    for (const auto& [tid, gauge] : active_agents_) {
        os << "active_agents{tenant_id=\"" << tid << "\"} " << gauge->get() << "\n";
    }
    return os.str();
}

CounterValue& PrometheusMetrics::get_counter(const std::string& key) {
    std::lock_guard lock(mutex_);
    auto [it, ins] = inference_counters_.try_emplace(key);
    if (ins) it->second = std::make_unique<CounterValue>();
    return *it->second;
}
HistogramValue& PrometheusMetrics::get_histogram(const std::string& key) {
    std::lock_guard lock(mutex_);
    auto [it, ins] = latency_histograms_.try_emplace(key);
    if (ins) it->second = std::make_unique<HistogramValue>();
    return *it->second;
}
GaugeValue& PrometheusMetrics::get_gauge(const std::string& key) {
    std::lock_guard lock(mutex_);
    auto [it, ins] = active_agents_.try_emplace(key);
    if (ins) it->second = std::make_unique<GaugeValue>();
    return *it->second;
}

}  // namespace prodxcloud::telemetry
