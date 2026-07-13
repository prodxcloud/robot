#pragma once

/// @file cicd_agent.hpp
/// @brief CICD Agent — CI/CD pipeline orchestration, build management, artifact
///        handling, deployment automation, environment management, and release
///        coordination with multi-platform support.
///
/// High-performance C++ pipeline engine supporting parallel stage execution,
/// DAG-based dependency resolution, artifact caching, and real-time streaming logs.

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "agents/agent_base.hpp"
#include "common/types.hpp"

namespace prodxcloud::agents::specialized {

// ─── CICD Operation Types ───────────────────────────────────────────────────

enum class CICDOperation {
    // Pipeline Management
    CREATE_PIPELINE, UPDATE_PIPELINE, DELETE_PIPELINE, LIST_PIPELINES,
    GET_PIPELINE, TRIGGER_PIPELINE, CANCEL_PIPELINE,
    RETRY_PIPELINE, GET_PIPELINE_STATUS, PIPELINE_HISTORY,
    // Build Operations
    TRIGGER_BUILD, CANCEL_BUILD, GET_BUILD_STATUS, GET_BUILD_LOGS,
    LIST_BUILDS, RETRY_BUILD, BUILD_MATRIX,
    // Stage/Step Execution
    EXECUTE_STAGE, SKIP_STAGE, RETRY_STAGE, GET_STAGE_STATUS,
    MANUAL_APPROVAL, GATE_CHECK,
    // Artifact Management
    PUBLISH_ARTIFACT, DOWNLOAD_ARTIFACT, LIST_ARTIFACTS,
    PROMOTE_ARTIFACT, DELETE_ARTIFACT, GET_ARTIFACT_INFO,
    ARTIFACT_CACHE_PURGE,
    // Deployment Automation
    DEPLOY_TO_ENV, ROLLBACK_DEPLOY, PROMOTE_DEPLOY,
    BLUE_GREEN_DEPLOY, CANARY_DEPLOY, ROLLING_DEPLOY,
    GET_DEPLOY_STATUS, LIST_DEPLOYMENTS, DEPLOY_HISTORY,
    // Environment Management
    CREATE_ENVIRONMENT, UPDATE_ENVIRONMENT, DELETE_ENVIRONMENT,
    LIST_ENVIRONMENTS, LOCK_ENVIRONMENT, UNLOCK_ENVIRONMENT,
    GET_ENVIRONMENT_STATUS, SET_ENV_VARIABLES,
    // Release Management
    CREATE_RELEASE, TAG_RELEASE, LIST_RELEASES,
    GENERATE_CHANGELOG, SEMANTIC_VERSION_BUMP,
    // Testing Integration
    RUN_TESTS, GET_TEST_RESULTS, TEST_COVERAGE_REPORT,
    INTEGRATION_TEST, E2E_TEST, PERFORMANCE_TEST,
    // Registry Operations
    PUSH_IMAGE, PULL_IMAGE, TAG_IMAGE, LIST_IMAGES,
    SCAN_IMAGE, DELETE_IMAGE,
    // Notification
    NOTIFY_STATUS, WEBHOOK_TRIGGER, SLACK_NOTIFY,
    // General
    HEALTH_CHECK, VALIDATE_CONFIG, LINT_PIPELINE, DRY_RUN
};

constexpr std::string_view cicd_operation_to_string(CICDOperation op);

// ─── Pipeline Status ────────────────────────────────────────────────────────

enum class PipelineStatus {
    PENDING, QUEUED, RUNNING, PAUSED, SUCCEEDED, FAILED,
    CANCELLED, TIMED_OUT, WAITING_APPROVAL, SKIPPED
};

constexpr std::string_view pipeline_status_to_string(PipelineStatus s) {
    switch (s) {
        case PipelineStatus::PENDING:          return "PENDING";
        case PipelineStatus::QUEUED:           return "QUEUED";
        case PipelineStatus::RUNNING:          return "RUNNING";
        case PipelineStatus::PAUSED:           return "PAUSED";
        case PipelineStatus::SUCCEEDED:        return "SUCCEEDED";
        case PipelineStatus::FAILED:           return "FAILED";
        case PipelineStatus::CANCELLED:        return "CANCELLED";
        case PipelineStatus::TIMED_OUT:        return "TIMED_OUT";
        case PipelineStatus::WAITING_APPROVAL: return "WAITING_APPROVAL";
        case PipelineStatus::SKIPPED:          return "SKIPPED";
    }
    return "UNKNOWN";
}

// ─── CICD Data Structures ───────────────────────────────────────────────────

struct PipelineStep {
    std::string id;
    std::string name;
    std::string command;
    std::string image;              // container image for execution
    std::string working_dir;
    std::vector<std::string> env_vars;
    std::vector<std::string> depends_on;
    int32_t timeout_seconds = 3600;
    int32_t retry_count = 0;
    int32_t max_retries = 0;
    bool allow_failure = false;
    bool parallel = false;
    std::string status = "pending";
    std::string output;
    double duration_ms = 0.0;
    std::string cache_key;
    std::string artifacts_json = "[]";
};

struct PipelineStage {
    std::string id;
    std::string name;
    std::vector<PipelineStep> steps;
    std::vector<std::string> depends_on;
    bool manual_approval = false;
    bool parallel_steps = false;
    std::string condition;          // on_success, on_failure, always, manual
    std::string status = "pending";
    double duration_ms = 0.0;
    std::string environment;        // dev, staging, prod
};

struct Pipeline {
    std::string id;
    std::string name;
    std::string description;
    std::string repository;
    std::string branch = "main";
    std::string commit_sha;
    std::string trigger_type;       // push, pr, schedule, manual, webhook, tag
    std::vector<PipelineStage> stages;
    PipelineStatus status = PipelineStatus::PENDING;
    int32_t build_number = 0;
    double total_duration_ms = 0.0;
    std::string started_at;
    std::string finished_at;
    std::string config_yaml;
    std::string variables_json = "{}";
    std::string created_at;
};

struct BuildArtifact {
    std::string id;
    std::string pipeline_id;
    std::string build_number;
    std::string name;
    std::string type;               // binary, docker_image, archive, report, log
    std::string path;
    std::string registry_url;
    int64_t size_bytes = 0;
    std::string checksum_sha256;
    std::string metadata_json = "{}";
    std::string created_at;
    std::string expires_at;
};

struct DeploymentEnvironment {
    std::string id;
    std::string name;               // dev, staging, production, canary
    std::string cluster;
    std::string namespace_name;
    std::string region;
    std::string current_version;
    std::string previous_version;
    std::string status;             // active, locked, deploying, degraded, inactive
    bool auto_deploy = false;
    bool requires_approval = false;
    std::string locked_by;
    std::string variables_json = "{}";
    std::string created_at;
    std::string last_deployed_at;
};

struct DeploymentRecord {
    std::string id;
    std::string pipeline_id;
    std::string environment;
    std::string version;
    std::string strategy;           // rolling, blue_green, canary, recreate
    std::string status;             // pending, in_progress, succeeded, failed, rolled_back
    int32_t replicas = 1;
    int32_t ready_replicas = 0;
    double canary_weight = 0.0;     // 0-100 for canary deployments
    double duration_ms = 0.0;
    std::string started_at;
    std::string finished_at;
    std::string rolled_back_at;
    std::string deployed_by;
};

struct ReleaseInfo {
    std::string id;
    std::string version;            // semantic version
    std::string tag;
    std::string title;
    std::string changelog;
    std::string commit_sha;
    std::string branch;
    std::vector<std::string> artifact_ids;
    std::string status;             // draft, published, pre-release
    std::string created_at;
    std::string published_at;
};

struct TestResult {
    std::string suite_name;
    int32_t total = 0;
    int32_t passed = 0;
    int32_t failed = 0;
    int32_t skipped = 0;
    double coverage_percent = 0.0;
    double duration_ms = 0.0;
    std::string report_url;
    std::vector<std::string> failed_tests;
};

// ─── CICD Operation Request ─────────────────────────────────────────────────

struct CICDOperationRequest {
    CICDOperation operation;
    // Pipeline params
    std::string pipeline_id;
    std::string pipeline_name;
    std::string repository;
    std::string branch = "main";
    std::string commit_sha;
    std::string trigger_type = "manual";
    std::string stages_json = "[]";
    std::string config_yaml;
    std::string variables_json = "{}";
    // Build params
    std::string build_id;
    std::string build_command;
    std::string image;
    // Stage params
    std::string stage_id;
    std::string step_id;
    bool approve = false;
    // Artifact params
    std::string artifact_id;
    std::string artifact_name;
    std::string artifact_path;
    std::string artifact_type;
    std::string registry_url;
    // Deploy params
    std::string environment;
    std::string version;
    std::string deploy_strategy = "rolling";
    int32_t replicas = 1;
    double canary_weight = 10.0;
    // Environment params
    std::string env_id;
    std::string env_name;
    std::string cluster;
    std::string namespace_name;
    // Release params
    std::string release_version;
    std::string release_title;
    std::string version_bump;       // major, minor, patch
    // Test params
    std::string test_command;
    std::string test_suite;
    std::string test_type;          // unit, integration, e2e, performance
    // Notification params
    std::string webhook_url;
    std::string notification_channel;
    std::string message;
    // General
    bool dry_run = false;
    int32_t timeout_seconds = 3600;
    int32_t concurrency = 4;
    std::string config_json = "{}";
};

// ─── CICD Operation Result ──────────────────────────────────────────────────

struct CICDOperationResult {
    bool success = false;
    CICDOperation operation;
    std::string output;
    std::string error_message;
    double duration_ms = 0.0;
    std::string timestamp;
    // Populated depending on operation
    Pipeline pipeline;
    BuildArtifact artifact;
    DeploymentRecord deployment;
    DeploymentEnvironment environment;
    ReleaseInfo release;
    TestResult test_result;
    std::vector<Pipeline> pipelines;
    std::vector<BuildArtifact> artifacts;
    std::vector<DeploymentRecord> deployments;
    std::vector<DeploymentEnvironment> environments;
    std::string build_logs;
    int32_t build_number = 0;
};

// ─── CICD Agent ─────────────────────────────────────────────────────────────

class CICDAgent : public AgentBase {
public:
    explicit CICDAgent(AgentConfig config);
    ~CICDAgent() override = default;

    Result<TaskResult> execute(Task& task) override;
    void cancel() override;
    Result<bool> health_check() override;

    // Direct operation interface
    Result<CICDOperationResult> execute_cicd_operation(const CICDOperationRequest& req);

    // Pipeline management
    Result<CICDOperationResult> create_pipeline(const CICDOperationRequest& req);
    Result<CICDOperationResult> trigger_pipeline(const CICDOperationRequest& req);
    Result<CICDOperationResult> cancel_pipeline(const CICDOperationRequest& req);
    Result<CICDOperationResult> retry_pipeline(const CICDOperationRequest& req);
    Result<CICDOperationResult> get_pipeline_status(const CICDOperationRequest& req);
    Result<CICDOperationResult> list_pipelines(const CICDOperationRequest& req);
    Result<CICDOperationResult> get_pipeline_history(const CICDOperationRequest& req);

    // Build operations
    Result<CICDOperationResult> trigger_build(const CICDOperationRequest& req);
    Result<CICDOperationResult> get_build_status(const CICDOperationRequest& req);
    Result<CICDOperationResult> get_build_logs(const CICDOperationRequest& req);

    // Stage execution
    Result<CICDOperationResult> execute_stage(const CICDOperationRequest& req);
    Result<CICDOperationResult> manual_approval(const CICDOperationRequest& req);
    Result<CICDOperationResult> gate_check(const CICDOperationRequest& req);

    // Artifact management
    Result<CICDOperationResult> publish_artifact(const CICDOperationRequest& req);
    Result<CICDOperationResult> download_artifact(const CICDOperationRequest& req);
    Result<CICDOperationResult> list_artifacts(const CICDOperationRequest& req);
    Result<CICDOperationResult> promote_artifact(const CICDOperationRequest& req);

    // Deployment
    Result<CICDOperationResult> deploy_to_env(const CICDOperationRequest& req);
    Result<CICDOperationResult> rollback_deploy(const CICDOperationRequest& req);
    Result<CICDOperationResult> promote_deploy(const CICDOperationRequest& req);
    Result<CICDOperationResult> blue_green_deploy(const CICDOperationRequest& req);
    Result<CICDOperationResult> canary_deploy(const CICDOperationRequest& req);
    Result<CICDOperationResult> rolling_deploy(const CICDOperationRequest& req);
    Result<CICDOperationResult> get_deploy_status(const CICDOperationRequest& req);

    // Environment management
    Result<CICDOperationResult> create_environment(const CICDOperationRequest& req);
    Result<CICDOperationResult> list_environments(const CICDOperationRequest& req);
    Result<CICDOperationResult> lock_environment(const CICDOperationRequest& req);
    Result<CICDOperationResult> unlock_environment(const CICDOperationRequest& req);

    // Release management
    Result<CICDOperationResult> create_release(const CICDOperationRequest& req);
    Result<CICDOperationResult> list_releases(const CICDOperationRequest& req);
    Result<CICDOperationResult> generate_changelog(const CICDOperationRequest& req);
    Result<CICDOperationResult> semantic_version_bump(const CICDOperationRequest& req);

    // Testing
    Result<CICDOperationResult> run_tests(const CICDOperationRequest& req);
    Result<CICDOperationResult> get_test_results(const CICDOperationRequest& req);
    Result<CICDOperationResult> get_coverage_report(const CICDOperationRequest& req);

    // Registry
    Result<CICDOperationResult> push_image(const CICDOperationRequest& req);
    Result<CICDOperationResult> pull_image(const CICDOperationRequest& req);
    Result<CICDOperationResult> scan_image(const CICDOperationRequest& req);
    Result<CICDOperationResult> list_images(const CICDOperationRequest& req);

    // Notifications
    Result<CICDOperationResult> notify_status(const CICDOperationRequest& req);
    Result<CICDOperationResult> webhook_trigger(const CICDOperationRequest& req);

    // Config
    Result<CICDOperationResult> validate_config(const CICDOperationRequest& req);
    Result<CICDOperationResult> lint_pipeline(const CICDOperationRequest& req);

    // Metrics
    [[nodiscard]] size_t active_pipeline_count() const;
    [[nodiscard]] size_t total_build_count() const;
    [[nodiscard]] int32_t next_build_number();

private:
    mutable std::shared_mutex data_mutex_;
    std::unordered_map<std::string, Pipeline> pipelines_;
    std::unordered_map<std::string, BuildArtifact> artifacts_;
    std::unordered_map<std::string, DeploymentEnvironment> environments_;
    std::unordered_map<std::string, DeploymentRecord> deployments_;
    std::unordered_map<std::string, ReleaseInfo> releases_;
    std::atomic<int32_t> build_counter_{0};

    // Pipeline execution engine
    Result<CICDOperationResult> execute_pipeline_stages(Pipeline& pipeline);
    Result<CICDOperationResult> execute_pipeline_step(PipelineStep& step);
    Result<std::string> run_build_command(const std::string& command,
                                           const std::string& image,
                                           int32_t timeout_seconds);

    // DAG resolution
    std::vector<std::vector<std::string>> resolve_stage_dag(const Pipeline& pipeline);
    bool validate_dag(const Pipeline& pipeline);

    // Internal helpers
    CICDOperationResult make_result(CICDOperation op);
    CICDOperationRequest parse_task_to_request(const Task& task);
    std::string generate_build_log_header(const Pipeline& pipeline);
    std::string compute_semantic_version(const std::string& current, const std::string& bump);
};

}  // namespace prodxcloud::agents::specialized
