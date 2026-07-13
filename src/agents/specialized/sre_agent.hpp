#pragma once

/// @file sre_agent.hpp
/// @brief SRE Agent — Site Reliability Engineering with incident management,
///        SLO/SLA tracking, alerting, capacity planning, runbook automation,
///        chaos engineering, and post-mortem generation.
///
/// Designed for high-performance reliability operations leveraging C++ concurrency
/// for real-time monitoring aggregation and rapid incident response.

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "agents/agent_base.hpp"
#include "common/types.hpp"

namespace prodxcloud::agents::specialized {

// ─── SRE Operation Types ────────────────────────────────────────────────────

enum class SREOperation {
    // Incident Management
    CREATE_INCIDENT, UPDATE_INCIDENT, RESOLVE_INCIDENT, ESCALATE_INCIDENT,
    LIST_INCIDENTS, GET_INCIDENT, ASSIGN_INCIDENT, ADD_INCIDENT_NOTE,
    // SLO/SLA Tracking
    DEFINE_SLO, GET_SLO_STATUS, LIST_SLOS, SLO_BURN_RATE,
    SLA_COMPLIANCE_REPORT, ERROR_BUDGET_STATUS,
    // Monitoring & Alerting
    CREATE_ALERT_RULE, UPDATE_ALERT_RULE, DELETE_ALERT_RULE, LIST_ALERTS,
    SILENCE_ALERT, ACKNOWLEDGE_ALERT, CHECK_ALERT_STATUS,
    QUERY_METRICS, QUERY_LOGS, CREATE_DASHBOARD,
    // Capacity Planning
    CAPACITY_FORECAST, RESOURCE_UTILIZATION, SCALING_RECOMMENDATION,
    COST_OPTIMIZATION, BOTTLENECK_ANALYSIS,
    // Chaos Engineering
    INJECT_LATENCY, INJECT_FAILURE, KILL_PROCESS, NETWORK_PARTITION,
    CPU_STRESS, MEMORY_STRESS, DISK_STRESS, DNS_FAILURE,
    CHAOS_EXPERIMENT_STATUS, ROLLBACK_CHAOS,
    // Runbook Automation
    EXECUTE_RUNBOOK, LIST_RUNBOOKS, CREATE_RUNBOOK,
    VALIDATE_RUNBOOK, RUNBOOK_DRY_RUN,
    // Post-Mortem
    GENERATE_POST_MORTEM, LIST_POST_MORTEMS, GET_POST_MORTEM,
    ADD_ACTION_ITEM, TRACK_ACTION_ITEMS,
    // On-Call
    GET_ON_CALL_SCHEDULE, UPDATE_ON_CALL, NOTIFY_ON_CALL,
    HANDOFF_ON_CALL,
    // Toil Tracking
    LOG_TOIL, GET_TOIL_REPORT, AUTOMATE_TOIL,
    // General
    HEALTH_CHECK, STATUS_PAGE_UPDATE, DEPENDENCY_MAP
};

constexpr std::string_view sre_operation_to_string(SREOperation op);

// ─── Incident Severity ──────────────────────────────────────────────────────

enum class IncidentSeverity { SEV1, SEV2, SEV3, SEV4, SEV5 };

constexpr std::string_view severity_to_string(IncidentSeverity s) {
    switch (s) {
        case IncidentSeverity::SEV1: return "SEV1";
        case IncidentSeverity::SEV2: return "SEV2";
        case IncidentSeverity::SEV3: return "SEV3";
        case IncidentSeverity::SEV4: return "SEV4";
        case IncidentSeverity::SEV5: return "SEV5";
    }
    return "UNKNOWN";
}

// ─── SRE Data Structures ────────────────────────────────────────────────────

struct Incident {
    std::string id;
    std::string title;
    std::string description;
    IncidentSeverity severity = IncidentSeverity::SEV3;
    std::string status;             // triggered, acknowledged, investigating, mitigated, resolved
    std::string assignee;
    std::string service;
    std::string impact;
    std::vector<std::string> notes;
    std::vector<std::string> affected_services;
    std::string created_at;
    std::string resolved_at;
    double ttd_minutes = 0.0;       // time to detect
    double ttr_minutes = 0.0;       // time to resolve
    std::string root_cause;
    std::string timeline_json = "[]";
};

struct SLODefinition {
    std::string id;
    std::string name;
    std::string service;
    std::string sli_type;           // availability, latency, throughput, error_rate
    double target_percent = 99.9;
    std::string window;             // 7d, 30d, 90d
    double current_percent = 100.0;
    double error_budget_remaining = 100.0;
    double burn_rate = 0.0;
    std::string status;             // healthy, warning, critical, breached
    std::string created_at;
};

struct AlertRule {
    std::string id;
    std::string name;
    std::string service;
    std::string metric;
    std::string condition;          // gt, lt, eq, neq
    double threshold = 0.0;
    std::string severity;
    int32_t evaluation_window_sec = 300;
    int32_t cooldown_sec = 600;
    bool enabled = true;
    std::string notification_channel;
    std::string created_at;
};

struct ChaosExperiment {
    std::string id;
    std::string name;
    std::string type;               // latency, failure, process_kill, etc.
    std::string target_service;
    std::string target_host;
    std::string status;             // pending, running, completed, rolled_back
    int32_t duration_seconds = 60;
    std::string parameters_json = "{}";
    std::string hypothesis;
    std::string result;
    std::string started_at;
    std::string ended_at;
};

struct Runbook {
    std::string id;
    std::string name;
    std::string description;
    std::string trigger;            // manual, alert, incident
    std::string steps_json = "[]";
    int32_t step_count = 0;
    std::string last_executed_at;
    int32_t execution_count = 0;
    double avg_duration_sec = 0.0;
    std::string created_at;
};

struct PostMortem {
    std::string id;
    std::string incident_id;
    std::string title;
    std::string summary;
    std::string root_cause;
    std::string impact;
    std::string timeline_json = "[]";
    std::vector<std::string> action_items;
    std::vector<std::string> lessons_learned;
    std::string status;             // draft, in_review, published
    std::string created_at;
};

struct CapacityForecast {
    std::string service;
    std::string resource_type;      // cpu, memory, disk, network
    double current_utilization = 0.0;
    double projected_utilization_7d = 0.0;
    double projected_utilization_30d = 0.0;
    std::string recommendation;
    double estimated_cost_delta = 0.0;
    std::string confidence;         // high, medium, low
};

// ─── SRE Operation Request ──────────────────────────────────────────────────

struct SREOperationRequest {
    SREOperation operation;
    // Incident fields
    std::string incident_id;
    std::string title;
    std::string description;
    IncidentSeverity severity = IncidentSeverity::SEV3;
    std::string assignee;
    std::string service;
    std::string note;
    std::string status;
    // SLO fields
    std::string slo_id;
    std::string sli_type;
    double target_percent = 99.9;
    std::string window = "30d";
    // Alert fields
    std::string alert_id;
    std::string metric;
    std::string condition;
    double threshold = 0.0;
    std::string notification_channel;
    // Chaos fields
    std::string experiment_id;
    std::string chaos_type;
    std::string target_host;
    int32_t duration_seconds = 60;
    std::string parameters_json = "{}";
    std::string hypothesis;
    // Runbook fields
    std::string runbook_id;
    std::string steps_json = "[]";
    // Capacity fields
    std::string resource_type;
    int32_t forecast_days = 30;
    // Query fields
    std::string query;
    std::string time_range = "1h";
    // General
    bool dry_run = false;
    std::string config_json = "{}";
};

// ─── SRE Operation Result ───────────────────────────────────────────────────

struct SREOperationResult {
    bool success = false;
    SREOperation operation;
    std::string output;
    std::string error_message;
    double duration_ms = 0.0;
    std::string timestamp;
    // Populated depending on operation
    Incident incident;
    SLODefinition slo;
    AlertRule alert;
    ChaosExperiment experiment;
    Runbook runbook;
    PostMortem post_mortem;
    CapacityForecast forecast;
    std::vector<Incident> incidents;
    std::vector<SLODefinition> slos;
    std::vector<AlertRule> alerts;
};

// ─── SRE Agent ──────────────────────────────────────────────────────────────

class SREAgent : public AgentBase {
public:
    explicit SREAgent(AgentConfig config);
    ~SREAgent() override = default;

    Result<TaskResult> execute(Task& task) override;
    void cancel() override;
    Result<bool> health_check() override;

    // Direct operation interface
    Result<SREOperationResult> execute_sre_operation(const SREOperationRequest& req);

    // Incident management
    Result<SREOperationResult> create_incident(const SREOperationRequest& req);
    Result<SREOperationResult> update_incident(const SREOperationRequest& req);
    Result<SREOperationResult> resolve_incident(const SREOperationRequest& req);
    Result<SREOperationResult> escalate_incident(const SREOperationRequest& req);
    Result<SREOperationResult> list_incidents(const SREOperationRequest& req);
    Result<SREOperationResult> get_incident(const SREOperationRequest& req);

    // SLO tracking
    Result<SREOperationResult> define_slo(const SREOperationRequest& req);
    Result<SREOperationResult> get_slo_status(const SREOperationRequest& req);
    Result<SREOperationResult> list_slos(const SREOperationRequest& req);
    Result<SREOperationResult> get_slo_burn_rate(const SREOperationRequest& req);
    Result<SREOperationResult> get_error_budget(const SREOperationRequest& req);

    // Alerting
    Result<SREOperationResult> create_alert_rule(const SREOperationRequest& req);
    Result<SREOperationResult> list_alerts(const SREOperationRequest& req);
    Result<SREOperationResult> silence_alert(const SREOperationRequest& req);
    Result<SREOperationResult> acknowledge_alert(const SREOperationRequest& req);

    // Chaos engineering
    Result<SREOperationResult> run_chaos_experiment(const SREOperationRequest& req);
    Result<SREOperationResult> get_chaos_status(const SREOperationRequest& req);
    Result<SREOperationResult> rollback_chaos(const SREOperationRequest& req);

    // Runbooks
    Result<SREOperationResult> execute_runbook(const SREOperationRequest& req);
    Result<SREOperationResult> list_runbooks(const SREOperationRequest& req);
    Result<SREOperationResult> create_runbook(const SREOperationRequest& req);

    // Post-mortems
    Result<SREOperationResult> generate_post_mortem(const SREOperationRequest& req);
    Result<SREOperationResult> list_post_mortems(const SREOperationRequest& req);

    // Capacity planning
    Result<SREOperationResult> capacity_forecast(const SREOperationRequest& req);
    Result<SREOperationResult> get_resource_utilization(const SREOperationRequest& req);
    Result<SREOperationResult> get_scaling_recommendation(const SREOperationRequest& req);

    // Toil tracking
    Result<SREOperationResult> log_toil(const SREOperationRequest& req);
    Result<SREOperationResult> get_toil_report(const SREOperationRequest& req);

    // Metrics
    [[nodiscard]] size_t active_incident_count() const;
    [[nodiscard]] size_t total_slo_count() const;

private:
    mutable std::shared_mutex data_mutex_;
    std::unordered_map<std::string, Incident> incidents_;
    std::unordered_map<std::string, SLODefinition> slos_;
    std::unordered_map<std::string, AlertRule> alert_rules_;
    std::unordered_map<std::string, ChaosExperiment> experiments_;
    std::unordered_map<std::string, Runbook> runbooks_;
    std::unordered_map<std::string, PostMortem> post_mortems_;

    // Internal helpers
    SREOperationResult make_result(SREOperation op);
    SREOperationRequest parse_task_to_request(const Task& task);
    double calculate_burn_rate(const SLODefinition& slo);
    std::string determine_slo_status(const SLODefinition& slo);
};

}  // namespace prodxcloud::agents::specialized
