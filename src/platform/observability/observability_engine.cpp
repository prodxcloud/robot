#include "platform/observability/observability_engine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "common/uuid.hpp"

namespace prodxcloud::platform::observability {

using json = nlohmann::json;
using prodxcloud::Error;
using prodxcloud::generate_uuid;
using prodxcloud::now_iso8601;

// ─── Constructor ────────────────────────────────────────────────────────────

ObservabilityEngine::ObservabilityEngine() {
    spdlog::info("ObservabilityEngine initialized (max_logs={}, max_metrics={})",
                 MAX_LOG_BUFFER, MAX_METRIC_BUFFER);
}

// ─── Internal Helpers ───────────────────────────────────────────────────────

void ObservabilityEngine::trim_buffers() {
    // Caller must already hold unique_lock
    if (logs_.size() > MAX_LOG_BUFFER) {
        size_t excess = logs_.size() - MAX_LOG_BUFFER;
        logs_.erase(logs_.begin(), logs_.begin() + static_cast<ptrdiff_t>(excess));
        spdlog::debug("Trimmed {} log entries from buffer", excess);
    }
    if (metrics_.size() > MAX_METRIC_BUFFER) {
        size_t excess = metrics_.size() - MAX_METRIC_BUFFER;
        metrics_.erase(metrics_.begin(), metrics_.begin() + static_cast<ptrdiff_t>(excess));
        spdlog::debug("Trimmed {} metric points from buffer", excess);
    }
}

double ObservabilityEngine::calculate_health_score(double availability, double error_rate,
                                                    double latency_p99, int32_t active_alerts) {
    // Weighted formula:
    //   availability  -> 40%  (higher is better, scale 0-100)
    //   error_rate    -> 30%  (lower is better, convert to penalty)
    //   latency_p99   -> 20%  (lower is better, penalty if > 1000ms)
    //   active_alerts -> 10%  (lower is better, each alert penalises)
    double avail_component = availability;  // already 0-100
    double error_component = std::max(0.0, 100.0 - (error_rate * 100.0));  // 0-100
    double latency_component = std::max(0.0, 100.0 - (latency_p99 / 10.0));  // 100ms=90, 1000ms=0
    double alert_component = std::max(0.0, 100.0 - (active_alerts * 20.0));  // each alert = -20

    double score = (avail_component * 0.40)
                 + (error_component * 0.30)
                 + (latency_component * 0.20)
                 + (alert_component * 0.10);
    return std::clamp(score, 0.0, 100.0);
}

std::string ObservabilityEngine::health_status_from_score(double score) {
    if (score >= 90.0) return "healthy";
    if (score >= 70.0) return "degraded";
    if (score >= 0.0)  return "unhealthy";
    return "unknown";
}

double ObservabilityEngine::calculate_burn_rate(const SLODashboard& slo, int32_t window_hours) {
    // burn_rate = (budget_consumed / expected_budget) for the window
    // error budget total = 100.0 - target (percentage points)
    double total_budget = 100.0 - slo.target;  // e.g. 0.1 for 99.9% target
    if (total_budget <= 0.0) return 0.0;

    double budget_consumed = total_budget - (slo.error_budget_remaining / 100.0 * total_budget);
    // Expected budget consumption: linear over 30-day window
    double total_window_hours = 30.0 * 24.0;  // 30d default
    double expected_budget = total_budget * (static_cast<double>(window_hours) / total_window_hours);
    if (expected_budget <= 0.0) return 0.0;

    return budget_consumed / expected_budget;
}

// ─── Log Pipeline ───────────────────────────────────────────────────────────

Result<void> ObservabilityEngine::ingest_log(LogEntry entry) {
    if (entry.message.empty()) {
        return std::unexpected(Error::validation("Log message cannot be empty"));
    }

    std::unique_lock lock(mutex_);
    if (entry.id.empty()) entry.id = generate_uuid();
    if (entry.timestamp.empty()) entry.timestamp = now_iso8601();
    if (entry.level.empty()) entry.level = "info";

    spdlog::trace("Ingesting log id={} service={} level={}",
                  entry.id, entry.service, entry.level);

    logs_.push_back(std::move(entry));
    trim_buffers();
    return {};
}

Result<std::vector<LogEntry>> ObservabilityEngine::query_logs(const std::string& service,
                                                               const std::string& level,
                                                               const std::string& /*time_range*/,
                                                               int32_t limit) {
    std::shared_lock lock(mutex_);
    std::vector<LogEntry> result;
    result.reserve(static_cast<size_t>(limit));

    // Iterate in reverse to return most recent first
    for (auto it = logs_.rbegin(); it != logs_.rend(); ++it) {
        if (static_cast<int32_t>(result.size()) >= limit) break;
        if (!service.empty() && it->service != service) continue;
        if (!level.empty() && it->level != level) continue;
        result.push_back(*it);
    }

    spdlog::debug("query_logs service={} level={} returned {} entries",
                  service, level, result.size());
    return result;
}

Result<std::vector<LogEntry>> ObservabilityEngine::search_logs(const std::string& query,
                                                                const std::string& /*time_range*/,
                                                                int32_t limit) {
    if (query.empty()) {
        return std::unexpected(Error::validation("Search query cannot be empty"));
    }

    std::shared_lock lock(mutex_);
    std::vector<LogEntry> result;
    result.reserve(static_cast<size_t>(limit));

    for (auto it = logs_.rbegin(); it != logs_.rend(); ++it) {
        if (static_cast<int32_t>(result.size()) >= limit) break;
        // Simple substring match on message and attributes
        if (it->message.find(query) != std::string::npos ||
            it->attributes_json.find(query) != std::string::npos ||
            it->service.find(query) != std::string::npos) {
            result.push_back(*it);
        }
    }

    spdlog::debug("search_logs query='{}' returned {} entries", query, result.size());
    return result;
}

// ─── Metric Pipeline ────────────────────────────────────────────────────────

Result<void> ObservabilityEngine::ingest_metric(MetricPoint point) {
    if (point.name.empty()) {
        return std::unexpected(Error::validation("Metric name cannot be empty"));
    }

    std::unique_lock lock(mutex_);
    if (point.timestamp.empty()) point.timestamp = now_iso8601();

    spdlog::trace("Ingesting metric name={} service={} value={}",
                  point.name, point.service, point.value);

    metrics_.push_back(std::move(point));
    trim_buffers();
    return {};
}

Result<std::vector<MetricPoint>> ObservabilityEngine::query_metrics(const std::string& metric_name,
                                                                     const std::string& service,
                                                                     const std::string& /*time_range*/) {
    std::shared_lock lock(mutex_);
    std::vector<MetricPoint> result;

    for (const auto& m : metrics_) {
        if (m.name != metric_name) continue;
        if (!service.empty() && m.service != service) continue;
        result.push_back(m);
    }

    spdlog::debug("query_metrics name={} service={} returned {} points",
                  metric_name, service, result.size());
    return result;
}

Result<double> ObservabilityEngine::get_metric_value(const std::string& metric_name,
                                                      const std::string& service) {
    std::shared_lock lock(mutex_);

    // Return most recent value for the given metric + service
    for (auto it = metrics_.rbegin(); it != metrics_.rend(); ++it) {
        if (it->name == metric_name &&
            (service.empty() || it->service == service)) {
            return it->value;
        }
    }

    return std::unexpected(Error::not_found(
        "No metric found: name=" + metric_name + " service=" + service));
}

// ─── Trace Pipeline ─────────────────────────────────────────────────────────

Result<void> ObservabilityEngine::ingest_span(TraceSpan span) {
    if (span.trace_id.empty()) {
        return std::unexpected(Error::validation("Trace ID cannot be empty"));
    }

    std::unique_lock lock(mutex_);
    if (span.span_id.empty()) span.span_id = generate_uuid();
    if (span.start_time.empty()) span.start_time = now_iso8601();

    spdlog::trace("Ingesting span trace_id={} span_id={} operation={}",
                  span.trace_id, span.span_id, span.operation);

    traces_[span.trace_id].push_back(std::move(span));
    return {};
}

Result<std::vector<TraceSpan>> ObservabilityEngine::get_trace(const std::string& trace_id) {
    std::shared_lock lock(mutex_);

    auto it = traces_.find(trace_id);
    if (it == traces_.end()) {
        return std::unexpected(Error::not_found("Trace not found: " + trace_id));
    }

    return it->second;
}

Result<std::vector<TraceSpan>> ObservabilityEngine::query_traces(const std::string& service,
                                                                  double min_duration_ms,
                                                                  int32_t limit) {
    std::shared_lock lock(mutex_);
    std::vector<TraceSpan> result;

    for (const auto& [tid, spans] : traces_) {
        for (const auto& span : spans) {
            if (static_cast<int32_t>(result.size()) >= limit) break;
            if (!service.empty() && span.service != service) continue;
            if (span.duration_ms < min_duration_ms) continue;
            result.push_back(span);
        }
        if (static_cast<int32_t>(result.size()) >= limit) break;
    }

    // Sort by duration descending (slowest first)
    std::sort(result.begin(), result.end(),
              [](const TraceSpan& a, const TraceSpan& b) {
                  return a.duration_ms > b.duration_ms;
              });

    if (static_cast<int32_t>(result.size()) > limit) {
        result.resize(static_cast<size_t>(limit));
    }

    spdlog::debug("query_traces service={} min_duration={}ms returned {} spans",
                  service, min_duration_ms, result.size());
    return result;
}

// ─── SLO Dashboards ─────────────────────────────────────────────────────────

Result<SLODashboard> ObservabilityEngine::create_slo(const std::string& name,
                                                      const std::string& service,
                                                      const std::string& sli_type,
                                                      double target,
                                                      const std::string& window) {
    if (name.empty() || service.empty()) {
        return std::unexpected(Error::validation("SLO name and service are required"));
    }
    if (target <= 0.0 || target > 100.0) {
        return std::unexpected(Error::validation("SLO target must be between 0 and 100"));
    }

    std::unique_lock lock(mutex_);

    SLODashboard slo;
    slo.id = generate_uuid();
    slo.name = name;
    slo.service = service;
    slo.sli_type = sli_type;
    slo.target = target;
    slo.window = window;
    slo.current_value = 100.0;
    slo.error_budget_total = 100.0 - target;
    slo.error_budget_remaining = 100.0;
    slo.burn_rate_1h = 0.0;
    slo.burn_rate_6h = 0.0;
    slo.burn_rate_24h = 0.0;
    slo.status = "healthy";
    slo.violations_30d = 0;
    slo.uptime_30d = 100.0;
    slo.created_at = now_iso8601();
    slo.updated_at = slo.created_at;

    spdlog::info("Created SLO id={} name='{}' service={} target={}% window={}",
                 slo.id, slo.name, slo.service, slo.target, slo.window);

    auto id = slo.id;
    slos_.emplace(id, std::move(slo));
    return slos_.at(id);
}

Result<SLODashboard> ObservabilityEngine::get_slo(const std::string& slo_id) const {
    std::shared_lock lock(mutex_);

    auto it = slos_.find(slo_id);
    if (it == slos_.end()) {
        return std::unexpected(Error::not_found("SLO not found: " + slo_id));
    }
    return it->second;
}

std::vector<SLODashboard> ObservabilityEngine::list_slos(const std::string& service) const {
    std::shared_lock lock(mutex_);
    std::vector<SLODashboard> result;

    for (const auto& [id, slo] : slos_) {
        if (!service.empty() && slo.service != service) continue;
        result.push_back(slo);
    }

    return result;
}

Result<SLODashboard> ObservabilityEngine::update_slo_value(const std::string& slo_id,
                                                            double current_value) {
    std::unique_lock lock(mutex_);

    auto it = slos_.find(slo_id);
    if (it == slos_.end()) {
        return std::unexpected(Error::not_found("SLO not found: " + slo_id));
    }

    auto& slo = it->second;
    slo.current_value = current_value;
    slo.updated_at = now_iso8601();

    // Update error budget remaining
    double total_budget = 100.0 - slo.target;
    if (total_budget > 0.0) {
        double consumed = std::max(0.0, slo.target - current_value);
        double remaining_pct = std::max(0.0, 1.0 - (consumed / total_budget)) * 100.0;
        slo.error_budget_remaining = std::clamp(remaining_pct, 0.0, 100.0);
    }

    // Recalculate burn rates
    slo.burn_rate_1h = calculate_burn_rate(slo, 1);
    slo.burn_rate_6h = calculate_burn_rate(slo, 6);
    slo.burn_rate_24h = calculate_burn_rate(slo, 24);

    // Update status based on burn rate
    if (slo.error_budget_remaining <= 0.0) {
        slo.status = "breached";
        slo.violations_30d++;
    } else if (slo.burn_rate_1h >= slo.critical_burn_rate) {
        slo.status = "critical";
    } else if (slo.burn_rate_1h >= slo.warn_burn_rate) {
        slo.status = "warning";
    } else {
        slo.status = "healthy";
    }

    slo.uptime_30d = current_value;

    spdlog::info("Updated SLO id={} current_value={} budget_remaining={}% status={}",
                 slo_id, current_value, slo.error_budget_remaining, slo.status);
    return slo;
}

Result<void> ObservabilityEngine::delete_slo(const std::string& slo_id) {
    std::unique_lock lock(mutex_);

    auto it = slos_.find(slo_id);
    if (it == slos_.end()) {
        return std::unexpected(Error::not_found("SLO not found: " + slo_id));
    }

    spdlog::info("Deleted SLO id={} name='{}'", it->second.id, it->second.name);
    slos_.erase(it);
    return {};
}

// ─── Alerting ───────────────────────────────────────────────────────────────

Result<Alert> ObservabilityEngine::create_alert(const std::string& name,
                                                 const std::string& service,
                                                 const std::string& metric,
                                                 const std::string& comparison,
                                                 double threshold,
                                                 const std::string& severity) {
    if (name.empty() || metric.empty()) {
        return std::unexpected(Error::validation("Alert name and metric are required"));
    }
    if (comparison != "gt" && comparison != "lt" && comparison != "gte" &&
        comparison != "lte" && comparison != "eq") {
        return std::unexpected(Error::validation(
            "Invalid comparison operator: " + comparison +
            " (must be gt, lt, gte, lte, or eq)"));
    }

    std::unique_lock lock(mutex_);

    Alert alert;
    alert.id = generate_uuid();
    alert.name = name;
    alert.service = service;
    alert.severity = severity;
    alert.condition = metric + " " + comparison + " " + std::to_string(threshold);
    alert.metric = metric;
    alert.threshold = threshold;
    alert.comparison = comparison;
    alert.enabled = true;
    alert.state = "ok";
    alert.fire_count = 0;
    alert.created_at = now_iso8601();

    spdlog::info("Created alert id={} name='{}' condition='{}'",
                 alert.id, alert.name, alert.condition);

    auto id = alert.id;
    alerts_.emplace(id, std::move(alert));
    return alerts_.at(id);
}

Result<Alert> ObservabilityEngine::get_alert(const std::string& alert_id) const {
    std::shared_lock lock(mutex_);

    auto it = alerts_.find(alert_id);
    if (it == alerts_.end()) {
        return std::unexpected(Error::not_found("Alert not found: " + alert_id));
    }
    return it->second;
}

std::vector<Alert> ObservabilityEngine::list_alerts(const std::string& service,
                                                     const std::string& state) const {
    std::shared_lock lock(mutex_);
    std::vector<Alert> result;

    for (const auto& [id, alert] : alerts_) {
        if (!service.empty() && alert.service != service) continue;
        if (!state.empty() && alert.state != state) continue;
        result.push_back(alert);
    }

    return result;
}

Result<void> ObservabilityEngine::acknowledge_alert(const std::string& alert_id) {
    std::unique_lock lock(mutex_);

    auto it = alerts_.find(alert_id);
    if (it == alerts_.end()) {
        return std::unexpected(Error::not_found("Alert not found: " + alert_id));
    }

    if (it->second.state == "firing" || it->second.state == "pending") {
        it->second.state = "resolved";
        spdlog::info("Acknowledged alert id={} name='{}'", alert_id, it->second.name);
    }

    return {};
}

Result<void> ObservabilityEngine::silence_alert(const std::string& alert_id,
                                                 int32_t /*duration_min*/) {
    std::unique_lock lock(mutex_);

    auto it = alerts_.find(alert_id);
    if (it == alerts_.end()) {
        return std::unexpected(Error::not_found("Alert not found: " + alert_id));
    }

    it->second.enabled = false;
    spdlog::info("Silenced alert id={} name='{}'", alert_id, it->second.name);
    return {};
}

Result<void> ObservabilityEngine::evaluate_alerts() {
    std::unique_lock lock(mutex_);

    int fired = 0;
    int resolved = 0;

    for (auto& [id, alert] : alerts_) {
        if (!alert.enabled) continue;

        // Find latest metric value for this alert's metric + service
        double latest_value = std::numeric_limits<double>::quiet_NaN();
        for (auto it = metrics_.rbegin(); it != metrics_.rend(); ++it) {
            if (it->name == alert.metric &&
                (alert.service.empty() || it->service == alert.service)) {
                latest_value = it->value;
                break;
            }
        }

        if (std::isnan(latest_value)) {
            // No metric data available, skip evaluation
            continue;
        }

        // Compare against threshold
        bool condition_met = false;
        if (alert.comparison == "gt")       condition_met = latest_value > alert.threshold;
        else if (alert.comparison == "lt")  condition_met = latest_value < alert.threshold;
        else if (alert.comparison == "gte") condition_met = latest_value >= alert.threshold;
        else if (alert.comparison == "lte") condition_met = latest_value <= alert.threshold;
        else if (alert.comparison == "eq")  condition_met = std::abs(latest_value - alert.threshold) < 1e-9;

        if (condition_met) {
            if (alert.state == "ok" || alert.state == "resolved") {
                alert.state = "pending";
            } else if (alert.state == "pending") {
                alert.state = "firing";
                alert.fire_count++;
                alert.last_fired_at = now_iso8601();
                fired++;
                spdlog::warn("Alert FIRING id={} name='{}' metric={} value={} threshold={}",
                             id, alert.name, alert.metric, latest_value, alert.threshold);
            }
            // Already firing stays firing
        } else {
            if (alert.state == "firing" || alert.state == "pending") {
                alert.state = "resolved";
                resolved++;
                spdlog::info("Alert RESOLVED id={} name='{}'", id, alert.name);
            }
        }
    }

    spdlog::info("Alert evaluation complete: {} fired, {} resolved", fired, resolved);
    return {};
}

// ─── Health Scoring ─────────────────────────────────────────────────────────

Result<ServiceHealth> ObservabilityEngine::get_service_health(const std::string& service) const {
    std::shared_lock lock(mutex_);

    auto it = health_cache_.find(service);
    if (it == health_cache_.end()) {
        return std::unexpected(Error::not_found("No health data for service: " + service));
    }
    return it->second;
}

std::vector<ServiceHealth> ObservabilityEngine::get_all_service_health() const {
    std::shared_lock lock(mutex_);
    std::vector<ServiceHealth> result;
    result.reserve(health_cache_.size());

    for (const auto& [svc, health] : health_cache_) {
        result.push_back(health);
    }

    return result;
}

Result<ServiceHealth> ObservabilityEngine::compute_health_score(const std::string& service) {
    std::unique_lock lock(mutex_);

    ServiceHealth health;
    health.service = service;
    health.last_updated = now_iso8601();

    // Gather latest metric values for this service
    auto find_latest_metric = [&](const std::string& metric_name) -> double {
        for (auto it = metrics_.rbegin(); it != metrics_.rend(); ++it) {
            if (it->name == metric_name && it->service == service) {
                return it->value;
            }
        }
        return -1.0;  // sentinel: not found
    };

    // Availability: look for "availability" metric
    double avail = find_latest_metric("availability");
    health.availability = (avail >= 0.0) ? avail : 100.0;

    // Error rate
    double err = find_latest_metric("error_rate");
    health.error_rate = (err >= 0.0) ? err : 0.0;

    // Latency percentiles
    double p50 = find_latest_metric("latency_p50");
    health.latency_p50_ms = (p50 >= 0.0) ? p50 : 0.0;

    double p95 = find_latest_metric("latency_p95");
    health.latency_p95_ms = (p95 >= 0.0) ? p95 : 0.0;

    double p99 = find_latest_metric("latency_p99");
    health.latency_p99_ms = (p99 >= 0.0) ? p99 : 0.0;

    // Throughput
    double rps = find_latest_metric("throughput_rps");
    health.throughput_rps = (rps >= 0.0) ? rps : 0.0;

    // Count active alerts for this service
    int32_t active_alerts = 0;
    for (const auto& [aid, alert] : alerts_) {
        if (alert.service == service &&
            (alert.state == "firing" || alert.state == "pending")) {
            active_alerts++;
        }
    }
    health.active_alerts = active_alerts;

    // Count active incidents (alerts in firing state)
    int32_t active_incidents = 0;
    for (const auto& [aid, alert] : alerts_) {
        if (alert.service == service && alert.state == "firing") {
            active_incidents++;
        }
    }
    health.active_incidents = active_incidents;

    // Count SLO violations
    int32_t slo_violations = 0;
    for (const auto& [sid, slo] : slos_) {
        if (slo.service == service && slo.status == "breached") {
            slo_violations++;
        }
    }
    health.slo_violations = slo_violations;

    // Compute health score
    health.health_score = calculate_health_score(
        health.availability, health.error_rate,
        health.latency_p99_ms, health.active_alerts);
    health.status = health_status_from_score(health.health_score);

    // Build dependencies JSON (scan traces for cross-service calls)
    json deps = json::array();
    for (const auto& [tid, spans] : traces_) {
        for (const auto& span : spans) {
            if (span.service == service && !span.parent_span_id.empty()) {
                // Find the parent span's service
                for (const auto& [tid2, spans2] : traces_) {
                    for (const auto& ps : spans2) {
                        if (ps.span_id == span.parent_span_id && ps.service != service) {
                            bool already = false;
                            for (const auto& d : deps) {
                                if (d.get<std::string>() == ps.service) {
                                    already = true;
                                    break;
                                }
                            }
                            if (!already) deps.push_back(ps.service);
                        }
                    }
                }
            }
        }
    }
    health.dependencies_json = deps.dump();

    spdlog::info("Computed health for service={} score={:.1f} status={}",
                 service, health.health_score, health.status);

    health_cache_[service] = health;
    return health;
}

// ─── Dashboards ─────────────────────────────────────────────────────────────

Result<Dashboard> ObservabilityEngine::create_dashboard(const std::string& tenant_id,
                                                         const std::string& name,
                                                         const std::vector<DashboardWidget>& widgets) {
    if (name.empty()) {
        return std::unexpected(Error::validation("Dashboard name is required"));
    }

    std::unique_lock lock(mutex_);

    Dashboard dashboard;
    dashboard.id = generate_uuid();
    dashboard.name = name;
    dashboard.tenant_id = tenant_id;
    dashboard.description = "";
    dashboard.widgets = widgets;

    // Assign IDs to widgets that don't have them
    for (auto& w : dashboard.widgets) {
        if (w.id.empty()) w.id = generate_uuid();
    }

    dashboard.is_default = false;
    dashboard.created_at = now_iso8601();
    dashboard.updated_at = dashboard.created_at;

    spdlog::info("Created dashboard id={} name='{}' tenant={} widgets={}",
                 dashboard.id, dashboard.name, dashboard.tenant_id, dashboard.widgets.size());

    auto id = dashboard.id;
    dashboards_.emplace(id, std::move(dashboard));
    return dashboards_.at(id);
}

Result<Dashboard> ObservabilityEngine::get_dashboard(const std::string& dashboard_id) const {
    std::shared_lock lock(mutex_);

    auto it = dashboards_.find(dashboard_id);
    if (it == dashboards_.end()) {
        return std::unexpected(Error::not_found("Dashboard not found: " + dashboard_id));
    }
    return it->second;
}

std::vector<Dashboard> ObservabilityEngine::list_dashboards(const std::string& tenant_id) const {
    std::shared_lock lock(mutex_);
    std::vector<Dashboard> result;

    for (const auto& [id, dashboard] : dashboards_) {
        if (!tenant_id.empty() && dashboard.tenant_id != tenant_id) continue;
        result.push_back(dashboard);
    }

    return result;
}

Result<Dashboard> ObservabilityEngine::create_default_slo_dashboard(const std::string& tenant_id,
                                                                     const std::string& service) {
    if (service.empty()) {
        return std::unexpected(Error::validation("Service name is required"));
    }

    std::vector<DashboardWidget> widgets;

    // Widget 1: Availability gauge
    {
        DashboardWidget w;
        w.id = generate_uuid();
        w.type = "gauge";
        w.title = "Availability";
        w.metric = "availability";
        w.query = "avg(up{service=\"" + service + "\"})";
        w.time_range = "1h";
        w.refresh_interval_sec = 30;
        w.position_x = 0;
        w.position_y = 0;
        w.width = 4;
        w.height = 4;
        widgets.push_back(std::move(w));
    }

    // Widget 2: Latency P99 line chart
    {
        DashboardWidget w;
        w.id = generate_uuid();
        w.type = "line_chart";
        w.title = "Latency P99";
        w.metric = "latency_p99";
        w.query = "histogram_quantile(0.99, rate(request_duration_bucket{service=\"" + service + "\"}[5m]))";
        w.time_range = "1h";
        w.refresh_interval_sec = 30;
        w.position_x = 4;
        w.position_y = 0;
        w.width = 4;
        w.height = 4;
        widgets.push_back(std::move(w));
    }

    // Widget 3: Error Rate line chart
    {
        DashboardWidget w;
        w.id = generate_uuid();
        w.type = "line_chart";
        w.title = "Error Rate";
        w.metric = "error_rate";
        w.query = "rate(http_requests_total{service=\"" + service + "\",status=~\"5..\"}[5m]) / rate(http_requests_total{service=\"" + service + "\"}[5m])";
        w.time_range = "1h";
        w.refresh_interval_sec = 30;
        w.position_x = 8;
        w.position_y = 0;
        w.width = 4;
        w.height = 4;
        widgets.push_back(std::move(w));
    }

    // Widget 4: Error Budget Remaining stat
    {
        DashboardWidget w;
        w.id = generate_uuid();
        w.type = "stat";
        w.title = "Error Budget Remaining";
        w.metric = "error_budget_remaining";
        w.query = "slo:error_budget:remaining{service=\"" + service + "\"}";
        w.time_range = "30d";
        w.refresh_interval_sec = 60;
        w.position_x = 0;
        w.position_y = 4;
        w.width = 4;
        w.height = 3;
        widgets.push_back(std::move(w));
    }

    // Widget 5: Burn Rate bar chart
    {
        DashboardWidget w;
        w.id = generate_uuid();
        w.type = "bar_chart";
        w.title = "SLO Burn Rate";
        w.metric = "burn_rate";
        w.query = "slo:burn_rate:1h{service=\"" + service + "\"}";
        w.time_range = "6h";
        w.refresh_interval_sec = 60;
        w.position_x = 4;
        w.position_y = 4;
        w.width = 4;
        w.height = 3;
        widgets.push_back(std::move(w));
    }

    // Widget 6: Throughput line chart
    {
        DashboardWidget w;
        w.id = generate_uuid();
        w.type = "line_chart";
        w.title = "Throughput (RPS)";
        w.metric = "throughput_rps";
        w.query = "rate(http_requests_total{service=\"" + service + "\"}[5m])";
        w.time_range = "1h";
        w.refresh_interval_sec = 30;
        w.position_x = 8;
        w.position_y = 4;
        w.width = 4;
        w.height = 3;
        widgets.push_back(std::move(w));
    }

    // Widget 7: Recent logs table
    {
        DashboardWidget w;
        w.id = generate_uuid();
        w.type = "logs";
        w.title = "Recent Errors";
        w.metric = "";
        w.query = "{service=\"" + service + "\", level=~\"error|fatal\"}";
        w.time_range = "1h";
        w.refresh_interval_sec = 15;
        w.position_x = 0;
        w.position_y = 7;
        w.width = 12;
        w.height = 4;
        widgets.push_back(std::move(w));
    }

    // Widget 8: SLO Violations heatmap
    {
        DashboardWidget w;
        w.id = generate_uuid();
        w.type = "heatmap";
        w.title = "SLO Violations (30d)";
        w.metric = "slo_violations";
        w.query = "slo:violations:count{service=\"" + service + "\"}";
        w.time_range = "30d";
        w.refresh_interval_sec = 300;
        w.position_x = 0;
        w.position_y = 11;
        w.width = 12;
        w.height = 3;
        widgets.push_back(std::move(w));
    }

    std::unique_lock lock(mutex_);

    Dashboard dashboard;
    dashboard.id = generate_uuid();
    dashboard.name = "SLO Dashboard - " + service;
    dashboard.tenant_id = tenant_id;
    dashboard.description = "Default SLO dashboard with availability, latency, error rate, "
                            "burn rate, and error budget widgets for service: " + service;
    dashboard.widgets = std::move(widgets);
    dashboard.tags_json = json::array({"slo", "default", service}).dump();
    dashboard.is_default = true;
    dashboard.created_at = now_iso8601();
    dashboard.updated_at = dashboard.created_at;

    spdlog::info("Created default SLO dashboard id={} service={} widgets={}",
                 dashboard.id, service, dashboard.widgets.size());

    auto id = dashboard.id;
    dashboards_.emplace(id, std::move(dashboard));
    return dashboards_.at(id);
}

// ─── Correlation & Root Cause ───────────────────────────────────────────────

Result<CorrelationResult> ObservabilityEngine::correlate_signals(const std::string& trace_id) {
    std::shared_lock lock(mutex_);

    CorrelationResult result;
    result.trace_id = trace_id;

    // 1. Find all spans for this trace
    auto trace_it = traces_.find(trace_id);
    if (trace_it != traces_.end()) {
        result.spans = trace_it->second;
    }

    // 2. Find all logs with this trace_id
    for (const auto& log : logs_) {
        if (log.trace_id == trace_id) {
            result.related_logs.push_back(log);
        }
    }

    // 3. Find related metrics by service names present in spans
    std::unordered_map<std::string, bool> services_seen;
    for (const auto& span : result.spans) {
        services_seen[span.service] = true;
    }

    for (const auto& m : metrics_) {
        if (services_seen.count(m.service)) {
            result.related_metrics.push_back(m);
        }
    }

    // 4. Determine root cause service: look for error spans, find the deepest one
    std::string root_cause;
    double max_duration = 0.0;
    for (const auto& span : result.spans) {
        if (span.status == "error" || span.status == "timeout") {
            if (span.duration_ms > max_duration) {
                max_duration = span.duration_ms;
                root_cause = span.service;
            }
        }
    }
    result.root_cause_service = root_cause;

    // 5. Confidence based on signal coverage
    int signal_types = 0;
    if (!result.spans.empty()) signal_types++;
    if (!result.related_logs.empty()) signal_types++;
    if (!result.related_metrics.empty()) signal_types++;
    result.confidence = static_cast<double>(signal_types) / 3.0;

    // 6. Build summary
    std::ostringstream summary;
    summary << "Trace " << trace_id << ": "
            << result.spans.size() << " spans, "
            << result.related_logs.size() << " logs, "
            << result.related_metrics.size() << " metric points";
    if (!root_cause.empty()) {
        summary << ". Root cause service: " << root_cause
                << " (duration=" << max_duration << "ms)";
    }
    result.summary = summary.str();

    spdlog::info("Correlated signals for trace={} confidence={:.2f}",
                 trace_id, result.confidence);
    return result;
}

Result<CorrelationResult> ObservabilityEngine::root_cause_analysis(const std::string& service,
                                                                    const std::string& /*time_range*/) {
    std::shared_lock lock(mutex_);

    CorrelationResult result;
    result.trace_id = "";  // RCA is not trace-specific
    result.root_cause_service = service;

    // Collect error logs for this service
    for (const auto& log : logs_) {
        if (log.service == service &&
            (log.level == "error" || log.level == "fatal")) {
            result.related_logs.push_back(log);
        }
    }

    // Collect recent metrics for this service
    for (const auto& m : metrics_) {
        if (m.service == service) {
            result.related_metrics.push_back(m);
        }
    }

    // Collect error spans for this service
    for (const auto& [tid, spans] : traces_) {
        for (const auto& span : spans) {
            if (span.service == service &&
                (span.status == "error" || span.status == "timeout")) {
                result.spans.push_back(span);
                if (result.trace_id.empty()) {
                    result.trace_id = span.trace_id;
                }
            }
        }
    }

    // Analyze patterns: look for most common error message
    std::unordered_map<std::string, int> error_counts;
    for (const auto& log : result.related_logs) {
        error_counts[log.message]++;
    }

    std::string top_error;
    int top_count = 0;
    for (const auto& [msg, count] : error_counts) {
        if (count > top_count) {
            top_count = count;
            top_error = msg;
        }
    }

    // Confidence based on signal density
    double signal_density = static_cast<double>(
        result.related_logs.size() + result.spans.size()) / 10.0;
    result.confidence = std::clamp(signal_density, 0.0, 1.0);

    // Build summary
    std::ostringstream summary;
    summary << "Root cause analysis for service=" << service << ": "
            << result.related_logs.size() << " error logs, "
            << result.spans.size() << " error spans, "
            << result.related_metrics.size() << " metric data points";
    if (!top_error.empty()) {
        summary << ". Most frequent error (" << top_count << "x): " << top_error;
    }
    result.summary = summary.str();

    spdlog::info("RCA for service={}: confidence={:.2f}", service, result.confidence);
    return result;
}

// ─── Anomaly Detection ──────────────────────────────────────────────────────

Result<std::vector<Anomaly>> ObservabilityEngine::detect_anomalies(const std::string& service) {
    std::unique_lock lock(mutex_);

    // Group metrics by name for this service
    std::unordered_map<std::string, std::vector<double>> metric_series;
    for (const auto& m : metrics_) {
        if (m.service == service) {
            metric_series[m.name].push_back(m.value);
        }
    }

    std::vector<Anomaly> detected;

    for (const auto& [metric_name, values] : metric_series) {
        if (values.size() < 3) continue;  // Need enough data for statistics

        // Compute mean and standard deviation
        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double mean = sum / static_cast<double>(values.size());

        double sq_sum = 0.0;
        for (double v : values) {
            sq_sum += (v - mean) * (v - mean);
        }
        double stddev = std::sqrt(sq_sum / static_cast<double>(values.size()));

        if (stddev < 1e-9) continue;  // No variance, skip

        // Check the latest value using z-score
        double latest = values.back();
        double z_score = std::abs(latest - mean) / stddev;

        // Z-score > 2 = anomaly
        if (z_score > 2.0) {
            Anomaly anomaly;
            anomaly.id = generate_uuid();
            anomaly.service = service;
            anomaly.metric = metric_name;
            anomaly.expected_value = mean;
            anomaly.actual_value = latest;
            anomaly.deviation_percent = ((latest - mean) / mean) * 100.0;
            anomaly.detected_at = now_iso8601();
            anomaly.acknowledged = false;

            // Classify type and severity
            if (latest > mean) {
                anomaly.type = "spike";
            } else {
                anomaly.type = "drop";
            }

            if (z_score > 4.0) {
                anomaly.severity = "high";
            } else if (z_score > 3.0) {
                anomaly.severity = "medium";
            } else {
                anomaly.severity = "low";
            }

            spdlog::warn("Anomaly detected: service={} metric={} type={} z_score={:.2f} "
                         "expected={:.2f} actual={:.2f}",
                         service, metric_name, anomaly.type, z_score,
                         mean, latest);

            detected.push_back(anomaly);
            anomalies_.push_back(anomaly);
        }
    }

    spdlog::info("Anomaly detection for service={}: {} anomalies found",
                 service, detected.size());
    return detected;
}

std::vector<Anomaly> ObservabilityEngine::list_anomalies(bool unacknowledged_only) const {
    std::shared_lock lock(mutex_);
    std::vector<Anomaly> result;

    for (const auto& a : anomalies_) {
        if (unacknowledged_only && a.acknowledged) continue;
        result.push_back(a);
    }

    return result;
}

Result<void> ObservabilityEngine::acknowledge_anomaly(const std::string& anomaly_id) {
    std::unique_lock lock(mutex_);

    for (auto& a : anomalies_) {
        if (a.id == anomaly_id) {
            a.acknowledged = true;
            spdlog::info("Acknowledged anomaly id={} service={} metric={}",
                         anomaly_id, a.service, a.metric);
            return {};
        }
    }

    return std::unexpected(Error::not_found("Anomaly not found: " + anomaly_id));
}

// ─── Stats ──────────────────────────────────────────────────────────────────

size_t ObservabilityEngine::log_count() const {
    std::shared_lock lock(mutex_);
    return logs_.size();
}

size_t ObservabilityEngine::metric_count() const {
    std::shared_lock lock(mutex_);
    return metrics_.size();
}

size_t ObservabilityEngine::trace_count() const {
    std::shared_lock lock(mutex_);
    return traces_.size();
}

size_t ObservabilityEngine::slo_count() const {
    std::shared_lock lock(mutex_);
    return slos_.size();
}

size_t ObservabilityEngine::alert_count() const {
    std::shared_lock lock(mutex_);
    return alerts_.size();
}

}  // namespace prodxcloud::platform::observability
