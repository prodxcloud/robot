#pragma once

/// @file observability_engine.hpp
/// @brief Unified Observability Engine — integrated logs, metrics, traces with
///        alerting, SLO dashboards, health scoring, and anomaly detection.
///
/// Platform Pillar: Observability
/// Deep visibility with correlated logs, metrics, and traces.
/// SLO dashboards out of the box with error budget tracking.

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"

namespace prodxcloud::platform::observability {

// ─── Signal Types ───────────────────────────────────────────────────────────

enum class SignalType { LOG, METRIC, TRACE, EVENT };

// ─── Log Entry ──────────────────────────────────────────────────────────────

struct LogEntry {
    std::string id;
    std::string timestamp;
    std::string level;              // debug, info, warn, error, fatal
    std::string service;
    std::string message;
    std::string trace_id;
    std::string span_id;
    std::string tenant_id;
    std::string host;
    std::string container;
    std::string attributes_json = "{}";
};

// ─── Metric Point ───────────────────────────────────────────────────────────

struct MetricPoint {
    std::string name;
    std::string type;               // counter, gauge, histogram, summary
    double value = 0.0;
    std::string unit;
    std::string service;
    std::string tenant_id;
    std::string labels_json = "{}";
    std::string timestamp;
};

// ─── Trace Span ─────────────────────────────────────────────────────────────

struct TraceSpan {
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
    std::string operation;
    std::string service;
    std::string status;             // ok, error, timeout
    double duration_ms = 0.0;
    std::string start_time;
    std::string end_time;
    std::string attributes_json = "{}";
    std::string events_json = "[]";
};

// ─── SLO Definition ─────────────────────────────────────────────────────────

struct SLODashboard {
    std::string id;
    std::string name;
    std::string service;
    std::string sli_type;           // availability, latency_p99, error_rate, throughput
    double target = 99.9;
    std::string window = "30d";     // rolling window
    // Current state
    double current_value = 100.0;
    double error_budget_total = 0.0;
    double error_budget_remaining = 100.0;
    double burn_rate_1h = 0.0;
    double burn_rate_6h = 0.0;
    double burn_rate_24h = 0.0;
    std::string status;             // healthy, warning, critical, breached
    // Thresholds
    double warn_burn_rate = 2.0;
    double critical_burn_rate = 10.0;
    // History
    int32_t violations_30d = 0;
    double uptime_30d = 99.99;
    std::string created_at;
    std::string updated_at;
};

// ─── Alert Definition ───────────────────────────────────────────────────────

struct Alert {
    std::string id;
    std::string name;
    std::string service;
    std::string severity;           // info, warning, error, critical, page
    std::string condition;          // metric > threshold, error_rate > 1%, etc.
    std::string metric;
    double threshold = 0.0;
    std::string comparison;         // gt, lt, gte, lte, eq
    int32_t evaluation_window_sec = 300;
    int32_t pending_period_sec = 60;
    bool enabled = true;
    std::string state;              // ok, pending, firing, resolved
    std::string notification_channels; // comma-separated: slack,pagerduty,email
    std::string runbook_url;
    std::string last_fired_at;
    int32_t fire_count = 0;
    std::string created_at;
};

// ─── Health Score ───────────────────────────────────────────────────────────

struct ServiceHealth {
    std::string service;
    double health_score = 100.0;    // 0-100
    std::string status;             // healthy, degraded, unhealthy, unknown
    double availability = 100.0;
    double error_rate = 0.0;
    double latency_p50_ms = 0.0;
    double latency_p95_ms = 0.0;
    double latency_p99_ms = 0.0;
    double throughput_rps = 0.0;
    int32_t active_alerts = 0;
    int32_t active_incidents = 0;
    int32_t slo_violations = 0;
    std::string dependencies_json = "[]";
    std::string last_updated;
};

// ─── Dashboard Widget ───────────────────────────────────────────────────────

struct DashboardWidget {
    std::string id;
    std::string type;               // line_chart, bar_chart, stat, gauge, table, heatmap, logs
    std::string title;
    std::string metric;
    std::string query;              // PromQL or similar
    std::string time_range = "1h";
    int32_t refresh_interval_sec = 30;
    int32_t position_x = 0;
    int32_t position_y = 0;
    int32_t width = 6;
    int32_t height = 4;
};

struct Dashboard {
    std::string id;
    std::string name;
    std::string tenant_id;
    std::string description;
    std::vector<DashboardWidget> widgets;
    std::string tags_json = "[]";
    bool is_default = false;
    std::string created_at;
    std::string updated_at;
};

// ─── Anomaly ────────────────────────────────────────────────────────────────

struct Anomaly {
    std::string id;
    std::string service;
    std::string metric;
    std::string type;               // spike, drop, trend_change, pattern_break
    double expected_value = 0.0;
    double actual_value = 0.0;
    double deviation_percent = 0.0;
    std::string severity;           // low, medium, high
    std::string detected_at;
    bool acknowledged = false;
};

// ─── Correlation Result ─────────────────────────────────────────────────────

struct CorrelationResult {
    std::string trace_id;
    std::vector<LogEntry> related_logs;
    std::vector<MetricPoint> related_metrics;
    std::vector<TraceSpan> spans;
    std::string root_cause_service;
    double confidence = 0.0;
    std::string summary;
};

// ─── Observability Engine ───────────────────────────────────────────────────

class ObservabilityEngine {
public:
    ObservabilityEngine();
    ~ObservabilityEngine() = default;

    // Log Pipeline
    Result<void> ingest_log(LogEntry entry);
    Result<std::vector<LogEntry>> query_logs(const std::string& service,
                                              const std::string& level = "",
                                              const std::string& time_range = "1h",
                                              int32_t limit = 100);
    Result<std::vector<LogEntry>> search_logs(const std::string& query,
                                               const std::string& time_range = "1h",
                                               int32_t limit = 100);

    // Metric Pipeline
    Result<void> ingest_metric(MetricPoint point);
    Result<std::vector<MetricPoint>> query_metrics(const std::string& metric_name,
                                                    const std::string& service = "",
                                                    const std::string& time_range = "1h");
    Result<double> get_metric_value(const std::string& metric_name,
                                     const std::string& service);

    // Trace Pipeline
    Result<void> ingest_span(TraceSpan span);
    Result<std::vector<TraceSpan>> get_trace(const std::string& trace_id);
    Result<std::vector<TraceSpan>> query_traces(const std::string& service,
                                                 double min_duration_ms = 0.0,
                                                 int32_t limit = 50);

    // SLO Dashboards
    Result<SLODashboard> create_slo(const std::string& name, const std::string& service,
                                     const std::string& sli_type, double target,
                                     const std::string& window = "30d");
    Result<SLODashboard> get_slo(const std::string& slo_id) const;
    std::vector<SLODashboard> list_slos(const std::string& service = "") const;
    Result<SLODashboard> update_slo_value(const std::string& slo_id, double current_value);
    Result<void> delete_slo(const std::string& slo_id);

    // Alerting
    Result<Alert> create_alert(const std::string& name, const std::string& service,
                                const std::string& metric, const std::string& comparison,
                                double threshold, const std::string& severity = "warning");
    Result<Alert> get_alert(const std::string& alert_id) const;
    std::vector<Alert> list_alerts(const std::string& service = "",
                                    const std::string& state = "") const;
    Result<void> acknowledge_alert(const std::string& alert_id);
    Result<void> silence_alert(const std::string& alert_id, int32_t duration_min = 60);
    Result<void> evaluate_alerts();

    // Health Scoring
    Result<ServiceHealth> get_service_health(const std::string& service) const;
    std::vector<ServiceHealth> get_all_service_health() const;
    Result<ServiceHealth> compute_health_score(const std::string& service);

    // Dashboards
    Result<Dashboard> create_dashboard(const std::string& tenant_id,
                                        const std::string& name,
                                        const std::vector<DashboardWidget>& widgets);
    Result<Dashboard> get_dashboard(const std::string& dashboard_id) const;
    std::vector<Dashboard> list_dashboards(const std::string& tenant_id) const;
    Result<Dashboard> create_default_slo_dashboard(const std::string& tenant_id,
                                                    const std::string& service);

    // Correlation & Root Cause
    Result<CorrelationResult> correlate_signals(const std::string& trace_id);
    Result<CorrelationResult> root_cause_analysis(const std::string& service,
                                                   const std::string& time_range = "1h");

    // Anomaly Detection
    Result<std::vector<Anomaly>> detect_anomalies(const std::string& service);
    std::vector<Anomaly> list_anomalies(bool unacknowledged_only = true) const;
    Result<void> acknowledge_anomaly(const std::string& anomaly_id);

    // Stats
    [[nodiscard]] size_t log_count() const;
    [[nodiscard]] size_t metric_count() const;
    [[nodiscard]] size_t trace_count() const;
    [[nodiscard]] size_t slo_count() const;
    [[nodiscard]] size_t alert_count() const;

private:
    mutable std::shared_mutex mutex_;
    // In-memory stores (production would use time-series DB)
    std::vector<LogEntry> logs_;
    std::vector<MetricPoint> metrics_;
    std::unordered_map<std::string, std::vector<TraceSpan>> traces_;
    std::unordered_map<std::string, SLODashboard> slos_;
    std::unordered_map<std::string, Alert> alerts_;
    std::unordered_map<std::string, ServiceHealth> health_cache_;
    std::unordered_map<std::string, Dashboard> dashboards_;
    std::vector<Anomaly> anomalies_;

    static constexpr size_t MAX_LOG_BUFFER = 100000;
    static constexpr size_t MAX_METRIC_BUFFER = 500000;

    void trim_buffers();
    double calculate_health_score(double availability, double error_rate,
                                   double latency_p99, int32_t active_alerts);
    std::string health_status_from_score(double score);
    double calculate_burn_rate(const SLODashboard& slo, int32_t window_hours);
};

}  // namespace prodxcloud::platform::observability
