#include "agents/specialized/sre_agent.hpp"
#include "common/uuid.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace prodxcloud::agents::specialized {

using json = nlohmann::json;

// ─── String Conversions ─────────────────────────────────────────────────────

constexpr std::string_view sre_operation_to_string(SREOperation op) {
    switch (op) {
        case SREOperation::CREATE_INCIDENT:        return "create_incident";
        case SREOperation::UPDATE_INCIDENT:        return "update_incident";
        case SREOperation::RESOLVE_INCIDENT:       return "resolve_incident";
        case SREOperation::ESCALATE_INCIDENT:      return "escalate_incident";
        case SREOperation::LIST_INCIDENTS:         return "list_incidents";
        case SREOperation::GET_INCIDENT:           return "get_incident";
        case SREOperation::ASSIGN_INCIDENT:        return "assign_incident";
        case SREOperation::ADD_INCIDENT_NOTE:      return "add_incident_note";
        case SREOperation::DEFINE_SLO:             return "define_slo";
        case SREOperation::GET_SLO_STATUS:         return "get_slo_status";
        case SREOperation::LIST_SLOS:              return "list_slos";
        case SREOperation::SLO_BURN_RATE:          return "slo_burn_rate";
        case SREOperation::SLA_COMPLIANCE_REPORT:  return "sla_compliance_report";
        case SREOperation::ERROR_BUDGET_STATUS:    return "error_budget_status";
        case SREOperation::CREATE_ALERT_RULE:      return "create_alert_rule";
        case SREOperation::LIST_ALERTS:            return "list_alerts";
        case SREOperation::SILENCE_ALERT:          return "silence_alert";
        case SREOperation::ACKNOWLEDGE_ALERT:      return "acknowledge_alert";
        case SREOperation::INJECT_LATENCY:
        case SREOperation::INJECT_FAILURE:
        case SREOperation::KILL_PROCESS:
        case SREOperation::CPU_STRESS:
        case SREOperation::MEMORY_STRESS:          return "chaos_experiment";
        case SREOperation::CHAOS_EXPERIMENT_STATUS: return "chaos_experiment_status";
        case SREOperation::ROLLBACK_CHAOS:         return "rollback_chaos";
        case SREOperation::EXECUTE_RUNBOOK:        return "execute_runbook";
        case SREOperation::LIST_RUNBOOKS:          return "list_runbooks";
        case SREOperation::CREATE_RUNBOOK:         return "create_runbook";
        case SREOperation::GENERATE_POST_MORTEM:   return "generate_post_mortem";
        case SREOperation::LIST_POST_MORTEMS:      return "list_post_mortems";
        case SREOperation::CAPACITY_FORECAST:      return "capacity_forecast";
        case SREOperation::RESOURCE_UTILIZATION:   return "resource_utilization";
        case SREOperation::SCALING_RECOMMENDATION: return "scaling_recommendation";
        case SREOperation::LOG_TOIL:               return "log_toil";
        case SREOperation::GET_TOIL_REPORT:        return "get_toil_report";
        default:                                   return "unknown";
    }
}

// ─── Constructor ────────────────────────────────────────────────────────────

SREAgent::SREAgent(AgentConfig config) : AgentBase(std::move(config)) {
    spdlog::info("SREAgent {} initialized for tenant {}", id(), tenant_id());
}

// ─── AgentBase Interface ────────────────────────────────────────────────────

Result<TaskResult> SREAgent::execute(Task& task) {
    transition_to(AgentState::RUNNING);
    auto start = Clock::now();

    auto req = parse_task_to_request(task);
    auto result = execute_sre_operation(req);

    TaskResult tr;
    tr.task_id = task.id;
    if (result) {
        tr.success = result->success;
        tr.output = result->output;
        tr.error_message = result->error_message;
    } else {
        tr.success = false;
        tr.error_message = result.error().message;
    }
    tr.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    transition_to(tr.success ? AgentState::IDLE : AgentState::ERROR);
    return tr;
}

void SREAgent::cancel() {
    cancellation_token_.cancel();
    spdlog::info("SREAgent {} cancelled", id());
}

Result<bool> SREAgent::health_check() { return true; }

// ─── Operation Router ───────────────────────────────────────────────────────

Result<SREOperationResult> SREAgent::execute_sre_operation(const SREOperationRequest& req) {
    if (cancellation_token_.is_cancelled())
        return std::unexpected(Error::internal("Operation cancelled"));

    switch (req.operation) {
        case SREOperation::CREATE_INCIDENT:    return create_incident(req);
        case SREOperation::UPDATE_INCIDENT:    return update_incident(req);
        case SREOperation::RESOLVE_INCIDENT:   return resolve_incident(req);
        case SREOperation::ESCALATE_INCIDENT:  return escalate_incident(req);
        case SREOperation::LIST_INCIDENTS:     return list_incidents(req);
        case SREOperation::GET_INCIDENT:       return get_incident(req);
        case SREOperation::DEFINE_SLO:         return define_slo(req);
        case SREOperation::GET_SLO_STATUS:     return get_slo_status(req);
        case SREOperation::LIST_SLOS:          return list_slos(req);
        case SREOperation::SLO_BURN_RATE:      return get_slo_burn_rate(req);
        case SREOperation::ERROR_BUDGET_STATUS: return get_error_budget(req);
        case SREOperation::CREATE_ALERT_RULE:  return create_alert_rule(req);
        case SREOperation::LIST_ALERTS:        return list_alerts(req);
        case SREOperation::SILENCE_ALERT:      return silence_alert(req);
        case SREOperation::ACKNOWLEDGE_ALERT:  return acknowledge_alert(req);
        case SREOperation::INJECT_LATENCY:
        case SREOperation::INJECT_FAILURE:
        case SREOperation::KILL_PROCESS:
        case SREOperation::CPU_STRESS:
        case SREOperation::MEMORY_STRESS:      return run_chaos_experiment(req);
        case SREOperation::CHAOS_EXPERIMENT_STATUS: return get_chaos_status(req);
        case SREOperation::ROLLBACK_CHAOS:     return rollback_chaos(req);
        case SREOperation::EXECUTE_RUNBOOK:    return execute_runbook(req);
        case SREOperation::LIST_RUNBOOKS:      return list_runbooks(req);
        case SREOperation::CREATE_RUNBOOK:     return create_runbook(req);
        case SREOperation::GENERATE_POST_MORTEM: return generate_post_mortem(req);
        case SREOperation::LIST_POST_MORTEMS:  return list_post_mortems(req);
        case SREOperation::CAPACITY_FORECAST:  return capacity_forecast(req);
        case SREOperation::RESOURCE_UTILIZATION: return get_resource_utilization(req);
        case SREOperation::SCALING_RECOMMENDATION: return get_scaling_recommendation(req);
        case SREOperation::LOG_TOIL:           return log_toil(req);
        case SREOperation::GET_TOIL_REPORT:    return get_toil_report(req);
        default:
            return std::unexpected(Error::bad_request("Unsupported SRE operation"));
    }
}

// ─── Incident Management ────────────────────────────────────────────────────

Result<SREOperationResult> SREAgent::create_incident(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::CREATE_INCIDENT);

    Incident incident;
    incident.id = "INC-" + generate_uuid().substr(0, 8);
    incident.title = req.title;
    incident.description = req.description;
    incident.severity = req.severity;
    incident.status = "triggered";
    incident.assignee = req.assignee;
    incident.service = req.service;
    incident.created_at = now_iso8601();

    {
        std::unique_lock lock(data_mutex_);
        incidents_[incident.id] = incident;
    }

    result.success = true;
    result.incident = incident;
    result.output = json{
        {"incident_id", incident.id},
        {"title", incident.title},
        {"severity", std::string(severity_to_string(incident.severity))},
        {"status", incident.status},
        {"created_at", incident.created_at}
    }.dump(2);

    spdlog::info("SREAgent {} created incident {} [{}]", id(), incident.id,
                 severity_to_string(incident.severity));
    return result;
}

Result<SREOperationResult> SREAgent::update_incident(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::UPDATE_INCIDENT);

    std::unique_lock lock(data_mutex_);
    auto it = incidents_.find(req.incident_id);
    if (it == incidents_.end())
        return std::unexpected(Error::not_found("Incident not found: " + req.incident_id));

    if (!req.status.empty()) it->second.status = req.status;
    if (!req.assignee.empty()) it->second.assignee = req.assignee;
    if (!req.note.empty()) it->second.notes.push_back(req.note);

    result.success = true;
    result.incident = it->second;
    result.output = json{{"incident_id", req.incident_id}, {"status", "updated"}}.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::resolve_incident(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::RESOLVE_INCIDENT);

    std::unique_lock lock(data_mutex_);
    auto it = incidents_.find(req.incident_id);
    if (it == incidents_.end())
        return std::unexpected(Error::not_found("Incident not found: " + req.incident_id));

    it->second.status = "resolved";
    it->second.resolved_at = now_iso8601();
    it->second.root_cause = req.description;

    result.success = true;
    result.incident = it->second;
    result.output = json{
        {"incident_id", req.incident_id},
        {"status", "resolved"},
        {"resolved_at", it->second.resolved_at}
    }.dump(2);

    spdlog::info("SREAgent {} resolved incident {}", id(), req.incident_id);
    return result;
}

Result<SREOperationResult> SREAgent::escalate_incident(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::ESCALATE_INCIDENT);

    std::unique_lock lock(data_mutex_);
    auto it = incidents_.find(req.incident_id);
    if (it == incidents_.end())
        return std::unexpected(Error::not_found("Incident not found: " + req.incident_id));

    // Escalate severity by one level
    auto& sev = it->second.severity;
    if (sev == IncidentSeverity::SEV5) sev = IncidentSeverity::SEV4;
    else if (sev == IncidentSeverity::SEV4) sev = IncidentSeverity::SEV3;
    else if (sev == IncidentSeverity::SEV3) sev = IncidentSeverity::SEV2;
    else if (sev == IncidentSeverity::SEV2) sev = IncidentSeverity::SEV1;

    it->second.notes.push_back("Escalated to " + std::string(severity_to_string(sev)));

    result.success = true;
    result.incident = it->second;
    result.output = json{
        {"incident_id", req.incident_id},
        {"new_severity", std::string(severity_to_string(sev))},
        {"status", "escalated"}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::list_incidents(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::LIST_INCIDENTS);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, inc] : incidents_) {
        if (!req.status.empty() && inc.status != req.status) continue;
        if (!req.service.empty() && inc.service != req.service) continue;
        arr.push_back({
            {"id", inc.id}, {"title", inc.title},
            {"severity", std::string(severity_to_string(inc.severity))},
            {"status", inc.status}, {"service", inc.service},
            {"assignee", inc.assignee}, {"created_at", inc.created_at}
        });
        result.incidents.push_back(inc);
    }

    result.success = true;
    result.output = json{{"incidents", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::get_incident(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::GET_INCIDENT);

    std::shared_lock lock(data_mutex_);
    auto it = incidents_.find(req.incident_id);
    if (it == incidents_.end())
        return std::unexpected(Error::not_found("Incident not found: " + req.incident_id));

    result.success = true;
    result.incident = it->second;
    result.output = json{
        {"id", it->second.id}, {"title", it->second.title},
        {"description", it->second.description},
        {"severity", std::string(severity_to_string(it->second.severity))},
        {"status", it->second.status}, {"assignee", it->second.assignee},
        {"service", it->second.service}, {"created_at", it->second.created_at},
        {"resolved_at", it->second.resolved_at}, {"root_cause", it->second.root_cause},
        {"notes_count", it->second.notes.size()}
    }.dump(2);
    return result;
}

// ─── SLO Tracking ───────────────────────────────────────────────────────────

Result<SREOperationResult> SREAgent::define_slo(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::DEFINE_SLO);

    SLODefinition slo;
    slo.id = "SLO-" + generate_uuid().substr(0, 8);
    slo.name = req.title;
    slo.service = req.service;
    slo.sli_type = req.sli_type;
    slo.target_percent = req.target_percent;
    slo.window = req.window;
    slo.current_percent = 100.0;
    slo.error_budget_remaining = 100.0;
    slo.burn_rate = 0.0;
    slo.status = "healthy";
    slo.created_at = now_iso8601();

    {
        std::unique_lock lock(data_mutex_);
        slos_[slo.id] = slo;
    }

    result.success = true;
    result.slo = slo;
    result.output = json{
        {"slo_id", slo.id}, {"name", slo.name}, {"service", slo.service},
        {"target", slo.target_percent}, {"window", slo.window}, {"status", "created"}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::get_slo_status(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::GET_SLO_STATUS);

    std::shared_lock lock(data_mutex_);
    auto it = slos_.find(req.slo_id);
    if (it == slos_.end())
        return std::unexpected(Error::not_found("SLO not found: " + req.slo_id));

    result.success = true;
    result.slo = it->second;
    result.slo.status = determine_slo_status(it->second);
    result.output = json{
        {"slo_id", it->second.id}, {"name", it->second.name},
        {"current_percent", it->second.current_percent},
        {"target_percent", it->second.target_percent},
        {"error_budget_remaining", it->second.error_budget_remaining},
        {"burn_rate", it->second.burn_rate},
        {"status", result.slo.status}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::list_slos(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::LIST_SLOS);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, slo] : slos_) {
        arr.push_back({
            {"id", slo.id}, {"name", slo.name}, {"service", slo.service},
            {"target", slo.target_percent}, {"current", slo.current_percent},
            {"status", determine_slo_status(slo)}
        });
        result.slos.push_back(slo);
    }

    result.success = true;
    result.output = json{{"slos", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::get_slo_burn_rate(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::SLO_BURN_RATE);

    std::shared_lock lock(data_mutex_);
    auto it = slos_.find(req.slo_id);
    if (it == slos_.end())
        return std::unexpected(Error::not_found("SLO not found: " + req.slo_id));

    double burn_rate = calculate_burn_rate(it->second);
    result.success = true;
    result.output = json{
        {"slo_id", it->second.id}, {"burn_rate", burn_rate},
        {"interpretation", burn_rate > 1.0 ? "CONSUMING_FAST" :
                           burn_rate > 0.5 ? "MODERATE" : "HEALTHY"}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::get_error_budget(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::ERROR_BUDGET_STATUS);

    std::shared_lock lock(data_mutex_);
    auto it = slos_.find(req.slo_id);
    if (it == slos_.end())
        return std::unexpected(Error::not_found("SLO not found: " + req.slo_id));

    double total_budget = 100.0 - it->second.target_percent;  // e.g., 0.1% for 99.9% SLO
    double consumed = total_budget * (1.0 - it->second.error_budget_remaining / 100.0);

    result.success = true;
    result.output = json{
        {"slo_id", it->second.id},
        {"total_error_budget_percent", total_budget},
        {"consumed_percent", consumed},
        {"remaining_percent", it->second.error_budget_remaining},
        {"status", it->second.error_budget_remaining > 50 ? "healthy" :
                   it->second.error_budget_remaining > 20 ? "warning" : "critical"}
    }.dump(2);
    return result;
}

// ─── Alerting ───────────────────────────────────────────────────────────────

Result<SREOperationResult> SREAgent::create_alert_rule(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::CREATE_ALERT_RULE);

    AlertRule alert;
    alert.id = "ALERT-" + generate_uuid().substr(0, 8);
    alert.name = req.title;
    alert.service = req.service;
    alert.metric = req.metric;
    alert.condition = req.condition;
    alert.threshold = req.threshold;
    alert.notification_channel = req.notification_channel;
    alert.created_at = now_iso8601();

    {
        std::unique_lock lock(data_mutex_);
        alert_rules_[alert.id] = alert;
    }

    result.success = true;
    result.alert = alert;
    result.output = json{
        {"alert_id", alert.id}, {"name", alert.name},
        {"metric", alert.metric}, {"condition", alert.condition},
        {"threshold", alert.threshold}, {"status", "created"}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::list_alerts(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::LIST_ALERTS);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, alert] : alert_rules_) {
        arr.push_back({
            {"id", alert.id}, {"name", alert.name}, {"service", alert.service},
            {"metric", alert.metric}, {"enabled", alert.enabled}
        });
        result.alerts.push_back(alert);
    }

    result.success = true;
    result.output = json{{"alerts", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::silence_alert(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::SILENCE_ALERT);

    std::unique_lock lock(data_mutex_);
    auto it = alert_rules_.find(req.alert_id);
    if (it == alert_rules_.end())
        return std::unexpected(Error::not_found("Alert not found: " + req.alert_id));

    it->second.enabled = false;
    result.success = true;
    result.output = json{{"alert_id", req.alert_id}, {"status", "silenced"}}.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::acknowledge_alert(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::ACKNOWLEDGE_ALERT);
    result.success = true;
    result.output = json{{"alert_id", req.alert_id}, {"status", "acknowledged"},
                          {"acknowledged_by", req.assignee}}.dump(2);
    return result;
}

// ─── Chaos Engineering ──────────────────────────────────────────────────────

Result<SREOperationResult> SREAgent::run_chaos_experiment(const SREOperationRequest& req) {
    auto result = make_result(req.operation);

    ChaosExperiment exp;
    exp.id = "CHAOS-" + generate_uuid().substr(0, 8);
    exp.name = req.title.empty() ? "chaos-" + exp.id : req.title;
    exp.target_service = req.service;
    exp.target_host = req.target_host;
    exp.duration_seconds = req.duration_seconds;
    exp.hypothesis = req.hypothesis;
    exp.parameters_json = req.parameters_json;
    exp.status = "running";
    exp.started_at = now_iso8601();

    switch (req.operation) {
        case SREOperation::INJECT_LATENCY:  exp.type = "latency_injection"; break;
        case SREOperation::INJECT_FAILURE:  exp.type = "failure_injection"; break;
        case SREOperation::KILL_PROCESS:    exp.type = "process_kill"; break;
        case SREOperation::CPU_STRESS:      exp.type = "cpu_stress"; break;
        case SREOperation::MEMORY_STRESS:   exp.type = "memory_stress"; break;
        default:                            exp.type = req.chaos_type; break;
    }

    {
        std::unique_lock lock(data_mutex_);
        experiments_[exp.id] = exp;
    }

    result.success = true;
    result.experiment = exp;
    result.output = json{
        {"experiment_id", exp.id}, {"type", exp.type},
        {"target_service", exp.target_service}, {"target_host", exp.target_host},
        {"duration_seconds", exp.duration_seconds}, {"status", "running"},
        {"hypothesis", exp.hypothesis}
    }.dump(2);

    spdlog::info("SREAgent {} started chaos experiment {} [{}]", id(), exp.id, exp.type);
    return result;
}

Result<SREOperationResult> SREAgent::get_chaos_status(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::CHAOS_EXPERIMENT_STATUS);

    std::shared_lock lock(data_mutex_);
    auto it = experiments_.find(req.experiment_id);
    if (it == experiments_.end())
        return std::unexpected(Error::not_found("Experiment not found: " + req.experiment_id));

    result.success = true;
    result.experiment = it->second;
    result.output = json{
        {"experiment_id", it->second.id}, {"type", it->second.type},
        {"status", it->second.status}, {"result", it->second.result}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::rollback_chaos(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::ROLLBACK_CHAOS);

    std::unique_lock lock(data_mutex_);
    auto it = experiments_.find(req.experiment_id);
    if (it == experiments_.end())
        return std::unexpected(Error::not_found("Experiment not found: " + req.experiment_id));

    it->second.status = "rolled_back";
    it->second.ended_at = now_iso8601();

    result.success = true;
    result.output = json{{"experiment_id", req.experiment_id}, {"status", "rolled_back"}}.dump(2);
    spdlog::info("SREAgent {} rolled back chaos experiment {}", id(), req.experiment_id);
    return result;
}

// ─── Runbooks ───────────────────────────────────────────────────────────────

Result<SREOperationResult> SREAgent::execute_runbook(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::EXECUTE_RUNBOOK);

    std::unique_lock lock(data_mutex_);
    auto it = runbooks_.find(req.runbook_id);
    if (it == runbooks_.end())
        return std::unexpected(Error::not_found("Runbook not found: " + req.runbook_id));

    it->second.last_executed_at = now_iso8601();
    it->second.execution_count++;

    result.success = true;
    result.runbook = it->second;
    result.output = json{
        {"runbook_id", it->second.id}, {"name", it->second.name},
        {"execution_count", it->second.execution_count},
        {"status", "executed"}, {"steps", it->second.step_count}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::list_runbooks(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::LIST_RUNBOOKS);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, rb] : runbooks_) {
        arr.push_back({
            {"id", rb.id}, {"name", rb.name}, {"trigger", rb.trigger},
            {"step_count", rb.step_count}, {"execution_count", rb.execution_count}
        });
    }

    result.success = true;
    result.output = json{{"runbooks", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::create_runbook(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::CREATE_RUNBOOK);

    Runbook rb;
    rb.id = "RB-" + generate_uuid().substr(0, 8);
    rb.name = req.title;
    rb.description = req.description;
    rb.trigger = req.status.empty() ? "manual" : req.status;
    rb.steps_json = req.steps_json;
    rb.created_at = now_iso8601();

    // Count steps
    json steps = json::parse(req.steps_json, nullptr, false);
    rb.step_count = steps.is_array() ? static_cast<int32_t>(steps.size()) : 0;

    {
        std::unique_lock lock(data_mutex_);
        runbooks_[rb.id] = rb;
    }

    result.success = true;
    result.runbook = rb;
    result.output = json{
        {"runbook_id", rb.id}, {"name", rb.name},
        {"steps", rb.step_count}, {"status", "created"}
    }.dump(2);
    return result;
}

// ─── Post-Mortems ───────────────────────────────────────────────────────────

Result<SREOperationResult> SREAgent::generate_post_mortem(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::GENERATE_POST_MORTEM);

    std::shared_lock lock(data_mutex_);
    auto inc_it = incidents_.find(req.incident_id);

    PostMortem pm;
    pm.id = "PM-" + generate_uuid().substr(0, 8);
    pm.incident_id = req.incident_id;
    pm.status = "draft";
    pm.created_at = now_iso8601();

    if (inc_it != incidents_.end()) {
        pm.title = "Post-Mortem: " + inc_it->second.title;
        pm.summary = inc_it->second.description;
        pm.root_cause = inc_it->second.root_cause;
        pm.impact = inc_it->second.impact;
    } else {
        pm.title = req.title;
        pm.summary = req.description;
    }

    lock.unlock();
    {
        std::unique_lock wlock(data_mutex_);
        post_mortems_[pm.id] = pm;
    }

    result.success = true;
    result.post_mortem = pm;
    result.output = json{
        {"post_mortem_id", pm.id}, {"incident_id", pm.incident_id},
        {"title", pm.title}, {"status", pm.status}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::list_post_mortems(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::LIST_POST_MORTEMS);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, pm] : post_mortems_) {
        arr.push_back({
            {"id", pm.id}, {"incident_id", pm.incident_id},
            {"title", pm.title}, {"status", pm.status}
        });
    }

    result.success = true;
    result.output = json{{"post_mortems", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

// ─── Capacity Planning ──────────────────────────────────────────────────────

Result<SREOperationResult> SREAgent::capacity_forecast(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::CAPACITY_FORECAST);

    CapacityForecast forecast;
    forecast.service = req.service;
    forecast.resource_type = req.resource_type;
    forecast.current_utilization = 65.0;  // would come from monitoring
    forecast.projected_utilization_7d = 68.0;
    forecast.projected_utilization_30d = 75.0;
    forecast.confidence = "medium";

    if (forecast.projected_utilization_30d > 85.0) {
        forecast.recommendation = "SCALE_UP: Projected to exceed 85% utilization within 30 days";
    } else if (forecast.projected_utilization_30d > 70.0) {
        forecast.recommendation = "MONITOR: Approaching capacity threshold, plan scaling";
    } else {
        forecast.recommendation = "STABLE: Sufficient capacity for projected growth";
    }

    result.success = true;
    result.forecast = forecast;
    result.output = json{
        {"service", forecast.service}, {"resource_type", forecast.resource_type},
        {"current_utilization", forecast.current_utilization},
        {"projected_7d", forecast.projected_utilization_7d},
        {"projected_30d", forecast.projected_utilization_30d},
        {"recommendation", forecast.recommendation},
        {"confidence", forecast.confidence}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::get_resource_utilization(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::RESOURCE_UTILIZATION);
    result.success = true;
    result.output = json{
        {"service", req.service},
        {"cpu_percent", 42.5}, {"memory_percent", 61.2},
        {"disk_percent", 55.8}, {"network_mbps", 125.4},
        {"timestamp", now_iso8601()}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::get_scaling_recommendation(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::SCALING_RECOMMENDATION);
    result.success = true;
    result.output = json{
        {"service", req.service},
        {"current_replicas", 3},
        {"recommended_replicas", 5},
        {"reason", "Projected 30-day growth exceeds current capacity"},
        {"estimated_cost_delta_monthly", 150.0},
        {"confidence", "medium"}
    }.dump(2);
    return result;
}

// ─── Toil Tracking ──────────────────────────────────────────────────────────

Result<SREOperationResult> SREAgent::log_toil(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::LOG_TOIL);
    result.success = true;
    result.output = json{
        {"status", "logged"}, {"task", req.title},
        {"service", req.service}, {"timestamp", now_iso8601()}
    }.dump(2);
    return result;
}

Result<SREOperationResult> SREAgent::get_toil_report(const SREOperationRequest& req) {
    auto result = make_result(SREOperation::GET_TOIL_REPORT);
    result.success = true;
    result.output = json{
        {"total_toil_hours_week", 12.5},
        {"top_toil_categories", json::array({"deployment", "log_rotation", "cert_renewal"})},
        {"automation_opportunity_hours", 8.0},
        {"recommendation", "Automate certificate renewal and log rotation"}
    }.dump(2);
    return result;
}

// ─── Metrics ────────────────────────────────────────────────────────────────

size_t SREAgent::active_incident_count() const {
    std::shared_lock lock(data_mutex_);
    return std::count_if(incidents_.begin(), incidents_.end(),
        [](const auto& pair) { return pair.second.status != "resolved"; });
}

size_t SREAgent::total_slo_count() const {
    std::shared_lock lock(data_mutex_);
    return slos_.size();
}

// ─── Internal Helpers ───────────────────────────────────────────────────────

SREOperationResult SREAgent::make_result(SREOperation op) {
    SREOperationResult result;
    result.operation = op;
    result.timestamp = now_iso8601();
    return result;
}

double SREAgent::calculate_burn_rate(const SLODefinition& slo) {
    double total_budget = 100.0 - slo.target_percent;
    if (total_budget <= 0) return 0.0;
    double consumed = total_budget * (1.0 - slo.error_budget_remaining / 100.0);
    return consumed / total_budget;
}

std::string SREAgent::determine_slo_status(const SLODefinition& slo) {
    if (slo.current_percent >= slo.target_percent && slo.error_budget_remaining > 50)
        return "healthy";
    if (slo.current_percent >= slo.target_percent && slo.error_budget_remaining > 20)
        return "warning";
    if (slo.current_percent < slo.target_percent)
        return "breached";
    return "critical";
}

SREOperationRequest SREAgent::parse_task_to_request(const Task& task) {
    SREOperationRequest req;
    json payload = json::parse(task.payload, nullptr, false);
    if (payload.is_discarded()) payload = json::parse(task.input_json, nullptr, false);
    if (payload.is_discarded()) return req;

    std::string op_str = payload.value("operation", "health_check");
    if (op_str == "create_incident") req.operation = SREOperation::CREATE_INCIDENT;
    else if (op_str == "update_incident") req.operation = SREOperation::UPDATE_INCIDENT;
    else if (op_str == "resolve_incident") req.operation = SREOperation::RESOLVE_INCIDENT;
    else if (op_str == "escalate_incident") req.operation = SREOperation::ESCALATE_INCIDENT;
    else if (op_str == "list_incidents") req.operation = SREOperation::LIST_INCIDENTS;
    else if (op_str == "get_incident") req.operation = SREOperation::GET_INCIDENT;
    else if (op_str == "define_slo") req.operation = SREOperation::DEFINE_SLO;
    else if (op_str == "get_slo_status") req.operation = SREOperation::GET_SLO_STATUS;
    else if (op_str == "list_slos") req.operation = SREOperation::LIST_SLOS;
    else if (op_str == "create_alert_rule") req.operation = SREOperation::CREATE_ALERT_RULE;
    else if (op_str == "list_alerts") req.operation = SREOperation::LIST_ALERTS;
    else if (op_str == "chaos_experiment") req.operation = SREOperation::INJECT_FAILURE;
    else if (op_str == "execute_runbook") req.operation = SREOperation::EXECUTE_RUNBOOK;
    else if (op_str == "create_runbook") req.operation = SREOperation::CREATE_RUNBOOK;
    else if (op_str == "generate_post_mortem") req.operation = SREOperation::GENERATE_POST_MORTEM;
    else if (op_str == "capacity_forecast") req.operation = SREOperation::CAPACITY_FORECAST;
    else req.operation = SREOperation::HEALTH_CHECK;

    req.incident_id = payload.value("incident_id", "");
    req.title = payload.value("title", "");
    req.description = payload.value("description", "");
    req.assignee = payload.value("assignee", "");
    req.service = payload.value("service", "");
    req.note = payload.value("note", "");
    req.status = payload.value("status", "");
    req.slo_id = payload.value("slo_id", "");
    req.sli_type = payload.value("sli_type", "availability");
    req.target_percent = payload.value("target_percent", 99.9);
    req.window = payload.value("window", "30d");
    req.alert_id = payload.value("alert_id", "");
    req.metric = payload.value("metric", "");
    req.condition = payload.value("condition", "gt");
    req.threshold = payload.value("threshold", 0.0);
    req.experiment_id = payload.value("experiment_id", "");
    req.target_host = payload.value("target_host", "");
    req.duration_seconds = payload.value("duration_seconds", 60);
    req.hypothesis = payload.value("hypothesis", "");
    req.runbook_id = payload.value("runbook_id", "");
    req.resource_type = payload.value("resource_type", "cpu");
    req.dry_run = payload.value("dry_run", false);

    std::string sev_str = payload.value("severity", "SEV3");
    if (sev_str == "SEV1") req.severity = IncidentSeverity::SEV1;
    else if (sev_str == "SEV2") req.severity = IncidentSeverity::SEV2;
    else if (sev_str == "SEV3") req.severity = IncidentSeverity::SEV3;
    else if (sev_str == "SEV4") req.severity = IncidentSeverity::SEV4;
    else if (sev_str == "SEV5") req.severity = IncidentSeverity::SEV5;

    return req;
}

}  // namespace prodxcloud::agents::specialized
