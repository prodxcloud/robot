#include "api/handlers/specialized_agent_handler.hpp"
#include "api/middleware/tenant_middleware.hpp"
#include "agents/specialized/agent_factory.hpp"
#include "common/uuid.hpp"
#include "common/types.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;
using namespace agents::specialized;

SpecializedAgentHandler::SpecializedAgentHandler(std::shared_ptr<agents::AgentController> controller)
    : controller_(std::move(controller)) {}

crow::response SpecializedAgentHandler::list_agent_types(const crow::request& req) {
    json types = json::array();
    for (const auto& type : AgentFactory::available_types()) {
        types.push_back({
            {"type", type},
            {"description", AgentFactory::describe(type)}
        });
    }
    return crow::response(200, json{{"agent_types", types}, {"count", types.size()}}.dump());
}

crow::response SpecializedAgentHandler::create_specialized_agent(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::string agent_type = body.value("type", "");
    if (agent_type.empty())
        return crow::response(400, json{{"error", "Agent type is required"}}.dump());

    AgentConfig cfg;
    cfg.id = generate_uuid();
    cfg.name = body.value("name", agent_type + "-agent");
    cfg.tenant_id = *tenant;
    cfg.max_retries = body.value("max_retries", 3);
    cfg.timeout_ms = body.value("timeout_ms", 60000);
    cfg.max_concurrent_tasks = body.value("max_concurrent_tasks", 4);

    auto agent = AgentFactory::create(agent_type, cfg);
    if (!agent)
        return crow::response(400, json{{"error", agent.error().message}}.dump());

    auto result = controller_->spawn_agent(cfg, *agent);
    if (!result)
        return crow::response(500, json{{"error", result.error().message}}.dump());

    return crow::response(201, json{
        {"agent_id", cfg.id},
        {"type", agent_type},
        {"name", cfg.name},
        {"tenant_id", cfg.tenant_id},
        {"description", AgentFactory::describe(agent_type)}
    }.dump());
}

crow::response SpecializedAgentHandler::execute_operation(const crow::request& req,
                                                           const std::string& agent_id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    // Verify agent exists and get state
    auto state = controller_->get_agent_state(agent_id);
    if (!state)
        return crow::response(404, json{{"error", state.error().message}}.dump());

    // Build task from request
    Task task;
    task.id = generate_uuid();
    task.agent_id = agent_id;
    task.tenant_id = *tenant;
    task.type = body.value("operation", "health_check");
    task.payload = body.dump();
    task.input_json = body.dump();

    // Execute synchronously for now
    auto agent_lookup = controller_->get_agent_state(agent_id);
    if (!agent_lookup)
        return crow::response(404, json{{"error", "Agent not found"}}.dump());

    return crow::response(200, json{
        {"agent_id", agent_id},
        {"task_id", task.id},
        {"operation", task.type},
        {"status", "submitted"},
        {"state", std::string(agents::agent_state_to_string(*state))}
    }.dump());
}

crow::response SpecializedAgentHandler::get_agent_capabilities(const crow::request& req,
                                                                 const std::string& agent_type) {
    json capabilities;

    if (agent_type == "devops") {
        capabilities = {
            {"type", "devops"},
            {"description", AgentFactory::describe("devops")},
            {"operations", json::array({
                "restart_server", "server_health", "server_processes", "disk_space", "system_info",
                "execute_remote_command", "multi_host_command",
                "list_containers", "restart_container", "docker_compose_up", "docker_compose_down",
                "docker_prune", "container_logs", "container_stats",
                "tail_logs", "clear_logs", "rotate_logs", "search_logs",
                "service_start", "service_stop", "service_restart", "service_status",
                "port_check", "dns_lookup", "ping_host", "network_connections",
                "deployment_health", "rolling_restart", "rollback_deployment"
            })},
            {"features", json::array({"parallel_ssh", "multi_host", "docker_orchestration"})}
        };
    } else if (agent_type == "sre") {
        capabilities = {
            {"type", "sre"},
            {"description", AgentFactory::describe("sre")},
            {"operations", json::array({
                "create_incident", "update_incident", "resolve_incident", "escalate_incident",
                "list_incidents", "get_incident",
                "define_slo", "get_slo_status", "list_slos", "slo_burn_rate", "error_budget_status",
                "create_alert_rule", "list_alerts", "silence_alert", "acknowledge_alert",
                "chaos_experiment", "chaos_experiment_status", "rollback_chaos",
                "execute_runbook", "create_runbook", "list_runbooks",
                "generate_post_mortem", "list_post_mortems",
                "capacity_forecast", "resource_utilization", "scaling_recommendation",
                "log_toil", "get_toil_report"
            })},
            {"features", json::array({"incident_management", "slo_tracking", "chaos_engineering",
                                       "runbook_automation", "capacity_planning"})}
        };
    } else if (agent_type == "openclaw") {
        capabilities = {
            {"type", "openclaw"},
            {"description", AgentFactory::describe("openclaw")},
            {"operations", json::array({
                "scan_licenses", "check_license_compatibility", "get_license_report",
                "scan_vulnerabilities", "get_cve_details", "list_vulnerabilities", "prioritize_vulnerabilities",
                "audit_dependencies", "list_dependencies", "dependency_tree", "check_outdated",
                "dependency_risk_score",
                "generate_sbom", "validate_sbom",
                "verify_signatures", "check_provenance", "detect_typosquat",
                "enforce_policy", "create_policy", "gate_check",
                "scan_secrets", "sast_scan", "container_scan", "iac_scan",
                "compliance_report", "full_audit", "remediation_plan"
            })},
            {"features", json::array({"sbom_generation", "license_compliance", "supply_chain_security",
                                       "policy_enforcement", "multi_ecosystem_scanning"})}
        };
    } else if (agent_type == "cicd") {
        capabilities = {
            {"type", "cicd"},
            {"description", AgentFactory::describe("cicd")},
            {"operations", json::array({
                "create_pipeline", "trigger_pipeline", "cancel_pipeline", "get_pipeline_status",
                "list_pipelines",
                "trigger_build", "get_build_status", "get_build_logs",
                "execute_stage", "manual_approval", "gate_check",
                "publish_artifact", "download_artifact", "list_artifacts",
                "deploy_to_env", "rollback_deploy", "blue_green_deploy", "canary_deploy",
                "rolling_deploy", "get_deploy_status",
                "create_environment", "list_environments", "lock_environment", "unlock_environment",
                "create_release", "list_releases", "generate_changelog", "semantic_version_bump",
                "run_tests", "push_image", "scan_image",
                "validate_config", "lint_pipeline"
            })},
            {"features", json::array({"dag_pipeline_execution", "multi_strategy_deployment",
                                       "artifact_management", "environment_locking",
                                       "semantic_versioning", "container_registry"})}
        };
    } else {
        return crow::response(404, json{{"error", "Unknown agent type: " + agent_type}}.dump());
    }

    return crow::response(200, capabilities.dump());
}

}  // namespace prodxcloud::api::handlers
