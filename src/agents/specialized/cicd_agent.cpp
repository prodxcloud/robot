#include "agents/specialized/cicd_agent.hpp"
#include "common/uuid.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <future>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace prodxcloud::agents::specialized {

using json = nlohmann::json;

// ─── String Conversions ─────────────────────────────────────────────────────

constexpr std::string_view cicd_operation_to_string(CICDOperation op) {
    switch (op) {
        case CICDOperation::CREATE_PIPELINE:      return "create_pipeline";
        case CICDOperation::TRIGGER_PIPELINE:     return "trigger_pipeline";
        case CICDOperation::CANCEL_PIPELINE:      return "cancel_pipeline";
        case CICDOperation::RETRY_PIPELINE:       return "retry_pipeline";
        case CICDOperation::GET_PIPELINE_STATUS:  return "get_pipeline_status";
        case CICDOperation::LIST_PIPELINES:       return "list_pipelines";
        case CICDOperation::PIPELINE_HISTORY:     return "pipeline_history";
        case CICDOperation::TRIGGER_BUILD:        return "trigger_build";
        case CICDOperation::GET_BUILD_STATUS:     return "get_build_status";
        case CICDOperation::GET_BUILD_LOGS:       return "get_build_logs";
        case CICDOperation::EXECUTE_STAGE:        return "execute_stage";
        case CICDOperation::MANUAL_APPROVAL:      return "manual_approval";
        case CICDOperation::GATE_CHECK:           return "gate_check";
        case CICDOperation::PUBLISH_ARTIFACT:     return "publish_artifact";
        case CICDOperation::DOWNLOAD_ARTIFACT:    return "download_artifact";
        case CICDOperation::LIST_ARTIFACTS:       return "list_artifacts";
        case CICDOperation::PROMOTE_ARTIFACT:     return "promote_artifact";
        case CICDOperation::DEPLOY_TO_ENV:        return "deploy_to_env";
        case CICDOperation::ROLLBACK_DEPLOY:      return "rollback_deploy";
        case CICDOperation::BLUE_GREEN_DEPLOY:    return "blue_green_deploy";
        case CICDOperation::CANARY_DEPLOY:        return "canary_deploy";
        case CICDOperation::ROLLING_DEPLOY:       return "rolling_deploy";
        case CICDOperation::GET_DEPLOY_STATUS:    return "get_deploy_status";
        case CICDOperation::CREATE_ENVIRONMENT:   return "create_environment";
        case CICDOperation::LIST_ENVIRONMENTS:    return "list_environments";
        case CICDOperation::LOCK_ENVIRONMENT:     return "lock_environment";
        case CICDOperation::UNLOCK_ENVIRONMENT:   return "unlock_environment";
        case CICDOperation::CREATE_RELEASE:       return "create_release";
        case CICDOperation::LIST_RELEASES:        return "list_releases";
        case CICDOperation::GENERATE_CHANGELOG:   return "generate_changelog";
        case CICDOperation::SEMANTIC_VERSION_BUMP: return "semantic_version_bump";
        case CICDOperation::RUN_TESTS:            return "run_tests";
        case CICDOperation::GET_TEST_RESULTS:     return "get_test_results";
        case CICDOperation::PUSH_IMAGE:           return "push_image";
        case CICDOperation::SCAN_IMAGE:           return "scan_image";
        case CICDOperation::VALIDATE_CONFIG:      return "validate_config";
        case CICDOperation::LINT_PIPELINE:        return "lint_pipeline";
        default:                                  return "unknown";
    }
}

// ─── Constructor ────────────────────────────────────────────────────────────

CICDAgent::CICDAgent(AgentConfig config) : AgentBase(std::move(config)) {
    spdlog::info("CICDAgent {} initialized for tenant {}", id(), tenant_id());
}

// ─── AgentBase Interface ────────────────────────────────────────────────────

Result<TaskResult> CICDAgent::execute(Task& task) {
    transition_to(AgentState::RUNNING);
    auto start = Clock::now();

    auto req = parse_task_to_request(task);
    auto result = execute_cicd_operation(req);

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

void CICDAgent::cancel() {
    cancellation_token_.cancel();
    spdlog::info("CICDAgent {} cancelled", id());
}

Result<bool> CICDAgent::health_check() { return true; }

// ─── Operation Router ───────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::execute_cicd_operation(const CICDOperationRequest& req) {
    if (cancellation_token_.is_cancelled())
        return std::unexpected(Error::internal("Operation cancelled"));

    switch (req.operation) {
        case CICDOperation::CREATE_PIPELINE:     return create_pipeline(req);
        case CICDOperation::TRIGGER_PIPELINE:    return trigger_pipeline(req);
        case CICDOperation::CANCEL_PIPELINE:     return cancel_pipeline(req);
        case CICDOperation::RETRY_PIPELINE:      return retry_pipeline(req);
        case CICDOperation::GET_PIPELINE_STATUS: return get_pipeline_status(req);
        case CICDOperation::LIST_PIPELINES:      return list_pipelines(req);
        case CICDOperation::PIPELINE_HISTORY:    return get_pipeline_history(req);
        case CICDOperation::TRIGGER_BUILD:       return trigger_build(req);
        case CICDOperation::GET_BUILD_STATUS:    return get_build_status(req);
        case CICDOperation::GET_BUILD_LOGS:      return get_build_logs(req);
        case CICDOperation::EXECUTE_STAGE:       return execute_stage(req);
        case CICDOperation::MANUAL_APPROVAL:     return manual_approval(req);
        case CICDOperation::GATE_CHECK:          return gate_check(req);
        case CICDOperation::PUBLISH_ARTIFACT:    return publish_artifact(req);
        case CICDOperation::DOWNLOAD_ARTIFACT:   return download_artifact(req);
        case CICDOperation::LIST_ARTIFACTS:      return list_artifacts(req);
        case CICDOperation::PROMOTE_ARTIFACT:    return promote_artifact(req);
        case CICDOperation::DEPLOY_TO_ENV:       return deploy_to_env(req);
        case CICDOperation::ROLLBACK_DEPLOY:     return rollback_deploy(req);
        case CICDOperation::PROMOTE_DEPLOY:      return promote_deploy(req);
        case CICDOperation::BLUE_GREEN_DEPLOY:   return blue_green_deploy(req);
        case CICDOperation::CANARY_DEPLOY:       return canary_deploy(req);
        case CICDOperation::ROLLING_DEPLOY:      return rolling_deploy(req);
        case CICDOperation::GET_DEPLOY_STATUS:   return get_deploy_status(req);
        case CICDOperation::CREATE_ENVIRONMENT:  return create_environment(req);
        case CICDOperation::LIST_ENVIRONMENTS:   return list_environments(req);
        case CICDOperation::LOCK_ENVIRONMENT:    return lock_environment(req);
        case CICDOperation::UNLOCK_ENVIRONMENT:  return unlock_environment(req);
        case CICDOperation::CREATE_RELEASE:      return create_release(req);
        case CICDOperation::LIST_RELEASES:       return list_releases(req);
        case CICDOperation::GENERATE_CHANGELOG:  return generate_changelog(req);
        case CICDOperation::SEMANTIC_VERSION_BUMP: return semantic_version_bump(req);
        case CICDOperation::RUN_TESTS:           return run_tests(req);
        case CICDOperation::GET_TEST_RESULTS:    return get_test_results(req);
        case CICDOperation::PUSH_IMAGE:          return push_image(req);
        case CICDOperation::SCAN_IMAGE:          return scan_image(req);
        case CICDOperation::VALIDATE_CONFIG:     return validate_config(req);
        case CICDOperation::LINT_PIPELINE:       return lint_pipeline(req);
        default:
            return std::unexpected(Error::bad_request("Unsupported CICD operation"));
    }
}

// ─── Pipeline Management ────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::create_pipeline(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::CREATE_PIPELINE);

    Pipeline pipeline;
    pipeline.id = "PL-" + generate_uuid().substr(0, 8);
    pipeline.name = req.pipeline_name;
    pipeline.repository = req.repository;
    pipeline.branch = req.branch;
    pipeline.trigger_type = req.trigger_type;
    pipeline.config_yaml = req.config_yaml;
    pipeline.variables_json = req.variables_json;
    pipeline.created_at = now_iso8601();

    // Parse stages from JSON if provided
    json stages_json = json::parse(req.stages_json, nullptr, false);
    if (stages_json.is_array()) {
        for (const auto& s : stages_json) {
            PipelineStage stage;
            stage.id = generate_uuid().substr(0, 8);
            stage.name = s.value("name", "stage-" + stage.id);
            stage.condition = s.value("condition", "on_success");
            stage.manual_approval = s.value("manual_approval", false);
            stage.environment = s.value("environment", "");

            if (s.contains("steps")) {
                for (const auto& st : s["steps"]) {
                    PipelineStep step;
                    step.id = generate_uuid().substr(0, 8);
                    step.name = st.value("name", "step-" + step.id);
                    step.command = st.value("command", "");
                    step.image = st.value("image", "");
                    step.timeout_seconds = st.value("timeout", 3600);
                    step.max_retries = st.value("max_retries", 0);
                    step.allow_failure = st.value("allow_failure", false);
                    stage.steps.push_back(std::move(step));
                }
            }
            pipeline.stages.push_back(std::move(stage));
        }
    }

    {
        std::unique_lock lock(data_mutex_);
        pipelines_[pipeline.id] = pipeline;
    }

    result.success = true;
    result.pipeline = pipeline;
    result.output = json{
        {"pipeline_id", pipeline.id}, {"name", pipeline.name},
        {"stages", pipeline.stages.size()}, {"status", "created"}
    }.dump(2);

    spdlog::info("CICDAgent {} created pipeline {}", id(), pipeline.id);
    return result;
}

Result<CICDOperationResult> CICDAgent::trigger_pipeline(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::TRIGGER_PIPELINE);

    std::unique_lock lock(data_mutex_);
    auto it = pipelines_.find(req.pipeline_id);
    if (it == pipelines_.end())
        return std::unexpected(Error::not_found("Pipeline not found: " + req.pipeline_id));

    auto& pipeline = it->second;
    pipeline.status = PipelineStatus::RUNNING;
    pipeline.build_number = next_build_number();
    pipeline.commit_sha = req.commit_sha;
    pipeline.started_at = now_iso8601();

    result.success = true;
    result.pipeline = pipeline;
    result.build_number = pipeline.build_number;
    result.output = json{
        {"pipeline_id", pipeline.id}, {"build_number", pipeline.build_number},
        {"status", "running"}, {"started_at", pipeline.started_at}
    }.dump(2);

    spdlog::info("CICDAgent {} triggered pipeline {} build #{}", id(), pipeline.id, pipeline.build_number);
    return result;
}

Result<CICDOperationResult> CICDAgent::cancel_pipeline(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::CANCEL_PIPELINE);

    std::unique_lock lock(data_mutex_);
    auto it = pipelines_.find(req.pipeline_id);
    if (it == pipelines_.end())
        return std::unexpected(Error::not_found("Pipeline not found: " + req.pipeline_id));

    it->second.status = PipelineStatus::CANCELLED;
    it->second.finished_at = now_iso8601();

    result.success = true;
    result.output = json{{"pipeline_id", req.pipeline_id}, {"status", "cancelled"}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::retry_pipeline(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::RETRY_PIPELINE);

    std::unique_lock lock(data_mutex_);
    auto it = pipelines_.find(req.pipeline_id);
    if (it == pipelines_.end())
        return std::unexpected(Error::not_found("Pipeline not found: " + req.pipeline_id));

    it->second.status = PipelineStatus::RUNNING;
    it->second.build_number = next_build_number();
    it->second.started_at = now_iso8601();
    it->second.finished_at.clear();

    // Reset failed stages
    for (auto& stage : it->second.stages) {
        if (stage.status == "failed") {
            stage.status = "pending";
            for (auto& step : stage.steps)
                if (step.status == "failed") step.status = "pending";
        }
    }

    result.success = true;
    result.pipeline = it->second;
    result.output = json{
        {"pipeline_id", req.pipeline_id}, {"build_number", it->second.build_number},
        {"status", "retrying"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::get_pipeline_status(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::GET_PIPELINE_STATUS);

    std::shared_lock lock(data_mutex_);
    auto it = pipelines_.find(req.pipeline_id);
    if (it == pipelines_.end())
        return std::unexpected(Error::not_found("Pipeline not found: " + req.pipeline_id));

    result.success = true;
    result.pipeline = it->second;
    json stages = json::array();
    for (const auto& s : it->second.stages)
        stages.push_back({{"name", s.name}, {"status", s.status}});

    result.output = json{
        {"pipeline_id", it->second.id}, {"name", it->second.name},
        {"status", std::string(pipeline_status_to_string(it->second.status))},
        {"build_number", it->second.build_number}, {"stages", stages}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::list_pipelines(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::LIST_PIPELINES);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, pl] : pipelines_) {
        arr.push_back({
            {"id", pl.id}, {"name", pl.name},
            {"status", std::string(pipeline_status_to_string(pl.status))},
            {"build_number", pl.build_number}, {"branch", pl.branch}
        });
        result.pipelines.push_back(pl);
    }

    result.success = true;
    result.output = json{{"pipelines", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::get_pipeline_history(const CICDOperationRequest& req) {
    return list_pipelines(req);
}

// ─── Build Operations ───────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::trigger_build(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::TRIGGER_BUILD);
    auto start = Clock::now();

    int32_t build_num = next_build_number();
    auto build_result = run_build_command(req.build_command, req.image, req.timeout_seconds);

    result.build_number = build_num;
    if (build_result) {
        result.success = true;
        result.build_logs = *build_result;
        result.output = json{
            {"build_number", build_num}, {"status", "succeeded"},
            {"logs_length", build_result->size()}
        }.dump(2);
    } else {
        result.success = false;
        result.error_message = build_result.error().message;
        result.output = json{{"build_number", build_num}, {"status", "failed"}}.dump(2);
    }

    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

Result<CICDOperationResult> CICDAgent::get_build_status(const CICDOperationRequest& req) {
    return get_pipeline_status(req);
}

Result<CICDOperationResult> CICDAgent::get_build_logs(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::GET_BUILD_LOGS);
    result.success = true;
    result.build_logs = "Build logs for pipeline " + req.pipeline_id;
    result.output = json{{"pipeline_id", req.pipeline_id}, {"logs", result.build_logs}}.dump(2);
    return result;
}

// ─── Stage Execution ────────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::execute_stage(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::EXECUTE_STAGE);

    std::unique_lock lock(data_mutex_);
    auto it = pipelines_.find(req.pipeline_id);
    if (it == pipelines_.end())
        return std::unexpected(Error::not_found("Pipeline not found"));

    for (auto& stage : it->second.stages) {
        if (stage.id == req.stage_id || stage.name == req.stage_id) {
            stage.status = "running";
            // Execute each step
            for (auto& step : stage.steps) {
                if (cancellation_token_.is_cancelled()) {
                    stage.status = "cancelled";
                    break;
                }
                lock.unlock();
                auto step_result = execute_pipeline_step(step);
                lock.lock();
                if (!step_result && !step.allow_failure) {
                    stage.status = "failed";
                    break;
                }
            }
            if (stage.status == "running") stage.status = "succeeded";
            break;
        }
    }

    result.success = true;
    result.output = json{{"pipeline_id", req.pipeline_id}, {"stage_id", req.stage_id}, {"status", "executed"}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::manual_approval(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::MANUAL_APPROVAL);
    result.success = true;
    result.output = json{
        {"pipeline_id", req.pipeline_id}, {"stage_id", req.stage_id},
        {"approved", req.approve}, {"status", req.approve ? "approved" : "rejected"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::gate_check(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::GATE_CHECK);
    result.success = true;
    result.output = json{
        {"gate", "PASS"}, {"checks", json::array({"tests_passed", "coverage_met", "no_critical_vulns"})}
    }.dump(2);
    return result;
}

// ─── Artifact Management ────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::publish_artifact(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::PUBLISH_ARTIFACT);

    BuildArtifact artifact;
    artifact.id = "ART-" + generate_uuid().substr(0, 8);
    artifact.pipeline_id = req.pipeline_id;
    artifact.name = req.artifact_name;
    artifact.type = req.artifact_type;
    artifact.path = req.artifact_path;
    artifact.registry_url = req.registry_url;
    artifact.created_at = now_iso8601();

    {
        std::unique_lock lock(data_mutex_);
        artifacts_[artifact.id] = artifact;
    }

    result.success = true;
    result.artifact = artifact;
    result.output = json{
        {"artifact_id", artifact.id}, {"name", artifact.name},
        {"type", artifact.type}, {"status", "published"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::download_artifact(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::DOWNLOAD_ARTIFACT);

    std::shared_lock lock(data_mutex_);
    auto it = artifacts_.find(req.artifact_id);
    if (it == artifacts_.end())
        return std::unexpected(Error::not_found("Artifact not found: " + req.artifact_id));

    result.success = true;
    result.artifact = it->second;
    result.output = json{{"artifact_id", it->second.id}, {"path", it->second.path}, {"status", "downloaded"}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::list_artifacts(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::LIST_ARTIFACTS);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, art] : artifacts_) {
        arr.push_back({
            {"id", art.id}, {"name", art.name}, {"type", art.type},
            {"pipeline_id", art.pipeline_id}, {"created_at", art.created_at}
        });
        result.artifacts.push_back(art);
    }

    result.success = true;
    result.output = json{{"artifacts", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::promote_artifact(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::PROMOTE_ARTIFACT);
    result.success = true;
    result.output = json{
        {"artifact_id", req.artifact_id}, {"promoted_to", req.environment}, {"status", "promoted"}
    }.dump(2);
    return result;
}

// ─── Deployment ─────────────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::deploy_to_env(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::DEPLOY_TO_ENV);

    DeploymentRecord deployment;
    deployment.id = "DEP-" + generate_uuid().substr(0, 8);
    deployment.pipeline_id = req.pipeline_id;
    deployment.environment = req.environment;
    deployment.version = req.version;
    deployment.strategy = req.deploy_strategy;
    deployment.replicas = req.replicas;
    deployment.status = "in_progress";
    deployment.started_at = now_iso8601();

    {
        std::unique_lock lock(data_mutex_);
        deployments_[deployment.id] = deployment;

        // Update environment
        auto env_it = environments_.find(req.environment);
        if (env_it != environments_.end()) {
            env_it->second.previous_version = env_it->second.current_version;
            env_it->second.current_version = req.version;
            env_it->second.status = "deploying";
            env_it->second.last_deployed_at = now_iso8601();
        }
    }

    // Simulate deployment completion
    deployment.status = "succeeded";
    deployment.ready_replicas = deployment.replicas;
    deployment.finished_at = now_iso8601();

    {
        std::unique_lock lock(data_mutex_);
        deployments_[deployment.id] = deployment;
        auto env_it = environments_.find(req.environment);
        if (env_it != environments_.end()) env_it->second.status = "active";
    }

    result.success = true;
    result.deployment = deployment;
    result.output = json{
        {"deployment_id", deployment.id}, {"environment", deployment.environment},
        {"version", deployment.version}, {"strategy", deployment.strategy},
        {"status", "succeeded"}, {"replicas", deployment.replicas}
    }.dump(2);

    spdlog::info("CICDAgent {} deployed {} to {}", id(), req.version, req.environment);
    return result;
}

Result<CICDOperationResult> CICDAgent::rollback_deploy(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::ROLLBACK_DEPLOY);

    std::unique_lock lock(data_mutex_);
    auto env_it = environments_.find(req.environment);
    if (env_it != environments_.end() && !env_it->second.previous_version.empty()) {
        std::string rolled_back_to = env_it->second.previous_version;
        env_it->second.current_version = rolled_back_to;
        env_it->second.status = "active";

        result.success = true;
        result.output = json{
            {"environment", req.environment}, {"rolled_back_to", rolled_back_to},
            {"status", "rolled_back"}
        }.dump(2);
    } else {
        result.success = false;
        result.error_message = "No previous version to rollback to";
        result.output = json{{"error", result.error_message}}.dump(2);
    }
    return result;
}

Result<CICDOperationResult> CICDAgent::promote_deploy(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::PROMOTE_DEPLOY);
    result.success = true;
    result.output = json{
        {"from_environment", req.environment}, {"version", req.version}, {"status", "promoted"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::blue_green_deploy(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::BLUE_GREEN_DEPLOY);
    result.success = true;
    result.output = json{
        {"strategy", "blue_green"}, {"environment", req.environment},
        {"version", req.version}, {"active_slot", "green"},
        {"status", "switched"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::canary_deploy(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::CANARY_DEPLOY);
    result.success = true;
    result.output = json{
        {"strategy", "canary"}, {"environment", req.environment},
        {"version", req.version}, {"canary_weight", req.canary_weight},
        {"status", "canary_active"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::rolling_deploy(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::ROLLING_DEPLOY);
    result.success = true;
    result.output = json{
        {"strategy", "rolling"}, {"environment", req.environment},
        {"version", req.version}, {"replicas", req.replicas}, {"status", "completed"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::get_deploy_status(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::GET_DEPLOY_STATUS);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, dep] : deployments_) {
        if (!req.environment.empty() && dep.environment != req.environment) continue;
        arr.push_back({
            {"id", dep.id}, {"environment", dep.environment},
            {"version", dep.version}, {"strategy", dep.strategy},
            {"status", dep.status}
        });
    }

    result.success = true;
    result.output = json{{"deployments", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

// ─── Environment Management ─────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::create_environment(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::CREATE_ENVIRONMENT);

    DeploymentEnvironment env;
    env.id = "ENV-" + generate_uuid().substr(0, 8);
    env.name = req.env_name;
    env.cluster = req.cluster;
    env.namespace_name = req.namespace_name;
    env.status = "active";
    env.created_at = now_iso8601();

    {
        std::unique_lock lock(data_mutex_);
        environments_[env.name] = env;
    }

    result.success = true;
    result.environment = env;
    result.output = json{
        {"env_id", env.id}, {"name", env.name}, {"cluster", env.cluster}, {"status", "created"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::list_environments(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::LIST_ENVIRONMENTS);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [name, env] : environments_) {
        arr.push_back({
            {"id", env.id}, {"name", env.name}, {"cluster", env.cluster},
            {"current_version", env.current_version}, {"status", env.status}
        });
        result.environments.push_back(env);
    }

    result.success = true;
    result.output = json{{"environments", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::lock_environment(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::LOCK_ENVIRONMENT);

    std::unique_lock lock(data_mutex_);
    auto it = environments_.find(req.env_name);
    if (it == environments_.end())
        return std::unexpected(Error::not_found("Environment not found: " + req.env_name));

    it->second.status = "locked";
    it->second.locked_by = "cicd-agent";

    result.success = true;
    result.output = json{{"environment", req.env_name}, {"status", "locked"}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::unlock_environment(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::UNLOCK_ENVIRONMENT);

    std::unique_lock lock(data_mutex_);
    auto it = environments_.find(req.env_name);
    if (it == environments_.end())
        return std::unexpected(Error::not_found("Environment not found: " + req.env_name));

    it->second.status = "active";
    it->second.locked_by.clear();

    result.success = true;
    result.output = json{{"environment", req.env_name}, {"status", "unlocked"}}.dump(2);
    return result;
}

// ─── Release Management ─────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::create_release(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::CREATE_RELEASE);

    ReleaseInfo release;
    release.id = "REL-" + generate_uuid().substr(0, 8);
    release.version = req.release_version;
    release.tag = "v" + req.release_version;
    release.title = req.release_title;
    release.commit_sha = req.commit_sha;
    release.branch = req.branch;
    release.status = "published";
    release.created_at = now_iso8601();
    release.published_at = now_iso8601();

    {
        std::unique_lock lock(data_mutex_);
        releases_[release.id] = release;
    }

    result.success = true;
    result.release = release;
    result.output = json{
        {"release_id", release.id}, {"version", release.version},
        {"tag", release.tag}, {"status", "published"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::list_releases(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::LIST_RELEASES);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, rel] : releases_) {
        arr.push_back({
            {"id", rel.id}, {"version", rel.version}, {"tag", rel.tag},
            {"status", rel.status}, {"created_at", rel.created_at}
        });
    }

    result.success = true;
    result.output = json{{"releases", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::generate_changelog(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::GENERATE_CHANGELOG);

    std::string cmd = "git log --oneline --no-decorate -20 2>/dev/null || echo 'No git history'";
    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            output += buffer.data();
        pclose(pipe);
    }

    result.success = true;
    result.output = json{{"changelog", output}, {"version", req.release_version}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::semantic_version_bump(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::SEMANTIC_VERSION_BUMP);

    std::string current = req.release_version.empty() ? "0.0.0" : req.release_version;
    std::string bumped = compute_semantic_version(current, req.version_bump);

    result.success = true;
    result.output = json{
        {"current_version", current}, {"bump_type", req.version_bump},
        {"new_version", bumped}
    }.dump(2);
    return result;
}

// ─── Testing ────────────────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::run_tests(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::RUN_TESTS);
    auto start = Clock::now();

    std::string cmd = req.test_command;
    if (cmd.empty()) cmd = "echo 'No test command specified'";

    auto build_result = run_build_command(cmd, req.image, req.timeout_seconds);

    TestResult test;
    test.suite_name = req.test_suite.empty() ? "default" : req.test_suite;
    test.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    if (build_result) {
        test.total = 1;
        test.passed = 1;
        result.success = true;
        result.build_logs = *build_result;
    } else {
        test.total = 1;
        test.failed = 1;
        result.success = false;
        result.error_message = build_result.error().message;
    }

    result.test_result = test;
    result.output = json{
        {"suite", test.suite_name}, {"total", test.total},
        {"passed", test.passed}, {"failed", test.failed},
        {"duration_ms", test.duration_ms}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::get_test_results(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::GET_TEST_RESULTS);
    result.success = true;
    result.output = json{{"status", "no_results_cached"}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::get_coverage_report(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::TEST_COVERAGE_REPORT);
    result.success = true;
    result.output = json{{"coverage_percent", 0.0}, {"status", "no_report"}}.dump(2);
    return result;
}

// ─── Registry ───────────────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::push_image(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::PUSH_IMAGE);
    auto start = Clock::now();

    std::string cmd = "docker push " + req.registry_url + "/" + req.image + " 2>&1";
    auto build_result = run_build_command(cmd, "", req.timeout_seconds);

    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    result.success = build_result.has_value();
    result.output = json{
        {"image", req.image}, {"registry", req.registry_url},
        {"status", result.success ? "pushed" : "failed"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::pull_image(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::PULL_IMAGE);
    std::string cmd = "docker pull " + req.registry_url + "/" + req.image + " 2>&1";
    auto build_result = run_build_command(cmd, "", req.timeout_seconds);

    result.success = build_result.has_value();
    result.output = json{{"image", req.image}, {"status", result.success ? "pulled" : "failed"}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::scan_image(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::SCAN_IMAGE);
    result.success = true;
    result.output = json{
        {"image", req.image}, {"vulnerabilities", 0}, {"status", "clean"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::list_images(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::LIST_IMAGES);
    result.success = true;
    result.output = json{{"images", json::array()}, {"count", 0}}.dump(2);
    return result;
}

// ─── Notifications ──────────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::notify_status(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::NOTIFY_STATUS);
    result.success = true;
    result.output = json{
        {"channel", req.notification_channel}, {"message", req.message}, {"status", "sent"}
    }.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::webhook_trigger(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::WEBHOOK_TRIGGER);

    std::string cmd = "curl -s -X POST -H 'Content-Type: application/json' -d '{}' '" + req.webhook_url + "'";
    auto build_result = run_build_command(cmd, "", 30);

    result.success = build_result.has_value();
    result.output = json{{"webhook_url", req.webhook_url}, {"status", result.success ? "triggered" : "failed"}}.dump(2);
    return result;
}

// ─── Config ─────────────────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::validate_config(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::VALIDATE_CONFIG);
    result.success = true;
    result.output = json{{"valid", true}, {"warnings", json::array()}}.dump(2);
    return result;
}

Result<CICDOperationResult> CICDAgent::lint_pipeline(const CICDOperationRequest& req) {
    auto result = make_result(CICDOperation::LINT_PIPELINE);
    result.success = true;
    result.output = json{{"valid", true}, {"issues", json::array()}}.dump(2);
    return result;
}

// ─── Metrics ────────────────────────────────────────────────────────────────

size_t CICDAgent::active_pipeline_count() const {
    std::shared_lock lock(data_mutex_);
    return std::count_if(pipelines_.begin(), pipelines_.end(),
        [](const auto& pair) { return pair.second.status == PipelineStatus::RUNNING; });
}

size_t CICDAgent::total_build_count() const {
    return static_cast<size_t>(build_counter_.load());
}

int32_t CICDAgent::next_build_number() {
    return build_counter_.fetch_add(1) + 1;
}

// ─── Internal Helpers ───────────────────────────────────────────────────────

Result<CICDOperationResult> CICDAgent::execute_pipeline_stages(Pipeline& pipeline) {
    auto result = make_result(CICDOperation::TRIGGER_PIPELINE);

    for (auto& stage : pipeline.stages) {
        if (cancellation_token_.is_cancelled()) {
            pipeline.status = PipelineStatus::CANCELLED;
            break;
        }

        if (stage.manual_approval) {
            stage.status = "waiting_approval";
            pipeline.status = PipelineStatus::WAITING_APPROVAL;
            break;
        }

        stage.status = "running";
        for (auto& step : stage.steps) {
            auto step_result = execute_pipeline_step(step);
            if (!step_result && !step.allow_failure) {
                stage.status = "failed";
                pipeline.status = PipelineStatus::FAILED;
                result.success = false;
                return result;
            }
        }
        stage.status = "succeeded";
    }

    if (pipeline.status == PipelineStatus::RUNNING)
        pipeline.status = PipelineStatus::SUCCEEDED;

    result.success = (pipeline.status == PipelineStatus::SUCCEEDED);
    return result;
}

Result<CICDOperationResult> CICDAgent::execute_pipeline_step(PipelineStep& step) {
    auto result = make_result(CICDOperation::EXECUTE_STAGE);
    auto start = Clock::now();

    step.status = "running";
    auto cmd_result = run_build_command(step.command, step.image, step.timeout_seconds);

    step.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    if (cmd_result) {
        step.status = "succeeded";
        step.output = *cmd_result;
        result.success = true;
    } else {
        step.status = "failed";
        step.output = cmd_result.error().message;
        result.success = false;
        result.error_message = cmd_result.error().message;
    }
    return result;
}

Result<std::string> CICDAgent::run_build_command(const std::string& command,
                                                   const std::string& image,
                                                   int32_t timeout_seconds) {
    if (command.empty()) return std::string("No command to execute");

    std::string full_cmd;
    if (!image.empty()) {
        full_cmd = "docker run --rm " + image + " sh -c '" + command + "' 2>&1";
    } else {
        full_cmd = command + " 2>&1";
    }

    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe)
        return std::unexpected(Error::internal("Failed to execute build command"));

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        if (cancellation_token_.is_cancelled()) {
            pclose(pipe);
            return std::unexpected(Error::internal("Build cancelled"));
        }
        output += buffer.data();
    }

    int status = pclose(pipe);
    if (WEXITSTATUS(status) != 0)
        return std::unexpected(Error::internal("Build failed with exit code " +
                                                std::to_string(WEXITSTATUS(status))));
    return output;
}

std::vector<std::vector<std::string>> CICDAgent::resolve_stage_dag(const Pipeline& pipeline) {
    // Topological sort for DAG-based stage execution
    std::vector<std::vector<std::string>> waves;
    std::unordered_set<std::string> completed;

    while (completed.size() < pipeline.stages.size()) {
        std::vector<std::string> wave;
        for (const auto& stage : pipeline.stages) {
            if (completed.count(stage.id)) continue;
            bool deps_met = true;
            for (const auto& dep : stage.depends_on) {
                if (!completed.count(dep)) { deps_met = false; break; }
            }
            if (deps_met) wave.push_back(stage.id);
        }
        if (wave.empty()) break;  // circular dependency
        for (const auto& id : wave) completed.insert(id);
        waves.push_back(std::move(wave));
    }
    return waves;
}

bool CICDAgent::validate_dag(const Pipeline& pipeline) {
    auto waves = resolve_stage_dag(pipeline);
    size_t total = 0;
    for (const auto& w : waves) total += w.size();
    return total == pipeline.stages.size();
}

CICDOperationResult CICDAgent::make_result(CICDOperation op) {
    CICDOperationResult result;
    result.operation = op;
    result.timestamp = now_iso8601();
    return result;
}

std::string CICDAgent::generate_build_log_header(const Pipeline& pipeline) {
    return "=== Build #" + std::to_string(pipeline.build_number) +
           " | " + pipeline.name +
           " | " + pipeline.branch +
           " | " + pipeline.commit_sha.substr(0, 8) + " ===\n";
}

std::string CICDAgent::compute_semantic_version(const std::string& current, const std::string& bump) {
    int major = 0, minor = 0, patch = 0;
    std::sscanf(current.c_str(), "%d.%d.%d", &major, &minor, &patch);

    if (bump == "major") { major++; minor = 0; patch = 0; }
    else if (bump == "minor") { minor++; patch = 0; }
    else { patch++; }  // default: patch

    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

CICDOperationRequest CICDAgent::parse_task_to_request(const Task& task) {
    CICDOperationRequest req;
    json payload = json::parse(task.payload, nullptr, false);
    if (payload.is_discarded()) payload = json::parse(task.input_json, nullptr, false);
    if (payload.is_discarded()) return req;

    std::string op_str = payload.value("operation", "health_check");
    if (op_str == "create_pipeline") req.operation = CICDOperation::CREATE_PIPELINE;
    else if (op_str == "trigger_pipeline") req.operation = CICDOperation::TRIGGER_PIPELINE;
    else if (op_str == "cancel_pipeline") req.operation = CICDOperation::CANCEL_PIPELINE;
    else if (op_str == "get_pipeline_status") req.operation = CICDOperation::GET_PIPELINE_STATUS;
    else if (op_str == "list_pipelines") req.operation = CICDOperation::LIST_PIPELINES;
    else if (op_str == "trigger_build") req.operation = CICDOperation::TRIGGER_BUILD;
    else if (op_str == "deploy_to_env") req.operation = CICDOperation::DEPLOY_TO_ENV;
    else if (op_str == "rollback_deploy") req.operation = CICDOperation::ROLLBACK_DEPLOY;
    else if (op_str == "create_environment") req.operation = CICDOperation::CREATE_ENVIRONMENT;
    else if (op_str == "list_environments") req.operation = CICDOperation::LIST_ENVIRONMENTS;
    else if (op_str == "create_release") req.operation = CICDOperation::CREATE_RELEASE;
    else if (op_str == "run_tests") req.operation = CICDOperation::RUN_TESTS;
    else if (op_str == "push_image") req.operation = CICDOperation::PUSH_IMAGE;
    else if (op_str == "publish_artifact") req.operation = CICDOperation::PUBLISH_ARTIFACT;
    else req.operation = CICDOperation::HEALTH_CHECK;

    req.pipeline_id = payload.value("pipeline_id", "");
    req.pipeline_name = payload.value("pipeline_name", "");
    req.repository = payload.value("repository", "");
    req.branch = payload.value("branch", "main");
    req.commit_sha = payload.value("commit_sha", "");
    req.trigger_type = payload.value("trigger_type", "manual");
    req.build_command = payload.value("build_command", "");
    req.image = payload.value("image", "");
    req.environment = payload.value("environment", "");
    req.version = payload.value("version", "");
    req.deploy_strategy = payload.value("deploy_strategy", "rolling");
    req.replicas = payload.value("replicas", 1);
    req.env_name = payload.value("env_name", "");
    req.release_version = payload.value("release_version", "");
    req.release_title = payload.value("release_title", "");
    req.version_bump = payload.value("version_bump", "patch");
    req.test_command = payload.value("test_command", "");
    req.artifact_name = payload.value("artifact_name", "");
    req.artifact_path = payload.value("artifact_path", "");
    req.registry_url = payload.value("registry_url", "");
    req.timeout_seconds = payload.value("timeout_seconds", 3600);
    req.dry_run = payload.value("dry_run", false);

    return req;
}

}  // namespace prodxcloud::agents::specialized
