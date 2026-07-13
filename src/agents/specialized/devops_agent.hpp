#pragma once

/// @file devops_agent.hpp
/// @brief DevOps Agent — server management, Docker orchestration, SSH execution,
///        log management, systemd service control, and deployment health checks.
///
/// Mirrors the Python DevOps agent capabilities with high-performance C++ execution,
/// concurrent SSH session pooling, and multi-host parallel operations.

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "agents/agent_base.hpp"
#include "common/types.hpp"

namespace prodxcloud::agents::specialized {

// ─── DevOps Operation Types ─────────────────────────────────────────────────

enum class DevOpsOperation {
    // Server Management
    RESTART_SERVER, SERVER_HEALTH, SERVER_PROCESSES, DISK_SPACE,
    SYSTEM_INFO, UPTIME_CHECK, MEMORY_ANALYSIS,
    // SSH Execution
    EXECUTE_REMOTE_COMMAND, BATCH_REMOTE_COMMAND, MULTI_HOST_COMMAND,
    // Docker Operations
    LIST_CONTAINERS, RESTART_CONTAINER, STOP_CONTAINER, START_CONTAINER,
    PULL_IMAGE, BUILD_IMAGE, DOCKER_COMPOSE_UP, DOCKER_COMPOSE_DOWN,
    DOCKER_PRUNE, CONTAINER_LOGS, CONTAINER_STATS, DOCKER_NETWORK_LIST,
    DOCKER_VOLUME_LIST,
    // Log Management
    TAIL_LOGS, CLEAR_LOGS, ROTATE_LOGS, SEARCH_LOGS, AGGREGATE_LOGS,
    // Service Management (systemd)
    SERVICE_START, SERVICE_STOP, SERVICE_RESTART, SERVICE_RELOAD,
    SERVICE_STATUS, SERVICE_ENABLE, SERVICE_DISABLE, SERVICE_LIST,
    // Package Management
    PACKAGE_INSTALL, PACKAGE_UPDATE, PACKAGE_REMOVE, PACKAGE_LIST,
    SYSTEM_UPDATE,
    // Network Diagnostics
    PORT_CHECK, DNS_LOOKUP, TRACEROUTE, PING_HOST, BANDWIDTH_TEST,
    NETWORK_CONNECTIONS,
    // File Operations
    FILE_TRANSFER, FILE_SYNC, BACKUP_CREATE, BACKUP_RESTORE,
    // Deployment
    DEPLOYMENT_HEALTH, ROLLING_RESTART, BLUE_GREEN_SWITCH,
    CANARY_CHECK, ROLLBACK_DEPLOYMENT,
    // General
    HEALTH_CHECK, DRY_RUN, CUSTOM_OPERATION
};

constexpr std::string_view devops_operation_to_string(DevOpsOperation op);

// ─── SSH Connection Config ──────────────────────────────────────────────────

struct SSHConnectionConfig {
    std::string host;
    std::string user = "root";
    int32_t port = 22;
    std::string key_path;
    std::string password;           // fallback if no key
    int32_t timeout_seconds = 30;
    bool strict_host_checking = false;
    int32_t max_retries = 2;
};

// ─── DevOps Operation Request ───────────────────────────────────────────────

struct DevOpsOperationRequest {
    DevOpsOperation operation;
    SSHConnectionConfig ssh;
    std::vector<std::string> target_hosts;  // for multi-host operations
    std::string command;
    // Docker params
    std::string container_name;
    std::string image_name;
    std::string compose_file = "docker-compose.yml";
    bool pull_latest = false;
    bool all_containers = false;
    // Log params
    std::string log_file = "/var/log/syslog";
    std::string log_pattern;
    int32_t log_lines = 100;
    int32_t older_than_days = 30;
    // Service params
    std::string service_name;
    std::string service_action;     // start, stop, restart, etc.
    // Package params
    std::string package_name;
    std::string package_version;
    // Network params
    int32_t target_port = 0;
    std::string dns_name;
    // File params
    std::string source_path;
    std::string dest_path;
    // Deployment params
    std::string service_url;
    int32_t expected_status = 200;
    int32_t health_timeout_seconds = 10;
    std::string deploy_strategy;    // rolling, blue_green, canary
    // General
    bool dry_run = false;
    int32_t concurrency = 4;        // for multi-host parallel ops
    std::string config_json = "{}";
};

// ─── DevOps Operation Result ────────────────────────────────────────────────

struct DevOpsOperationResult {
    bool success = false;
    DevOpsOperation operation;
    std::string host;
    std::string output;
    std::string stderr_output;
    int32_t exit_code = -1;
    std::string error_message;
    double duration_ms = 0.0;
    std::string timestamp;
    // Multi-host results
    struct HostResult {
        std::string host;
        bool success = false;
        std::string output;
        int32_t exit_code = -1;
        double duration_ms = 0.0;
    };
    std::vector<HostResult> host_results;
    // Docker-specific
    struct ContainerInfo {
        std::string id;
        std::string name;
        std::string image;
        std::string status;
        std::string ports;
        std::string created;
    };
    std::vector<ContainerInfo> containers;
    // Health metrics
    struct ServerMetrics {
        double cpu_percent = 0.0;
        double memory_percent = 0.0;
        double disk_percent = 0.0;
        std::string uptime;
        double load_1m = 0.0;
        double load_5m = 0.0;
        double load_15m = 0.0;
    };
    ServerMetrics metrics;
};

// ─── DevOps Agent ───────────────────────────────────────────────────────────

class DevOpsAgent : public AgentBase {
public:
    explicit DevOpsAgent(AgentConfig config);
    ~DevOpsAgent() override = default;

    Result<TaskResult> execute(Task& task) override;
    void cancel() override;
    Result<bool> health_check() override;

    // Direct operation interface
    Result<DevOpsOperationResult> execute_devops_operation(const DevOpsOperationRequest& req);

    // Server management
    Result<DevOpsOperationResult> restart_server(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> check_server_health(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> get_server_processes(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> check_disk_space(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> get_system_info(const DevOpsOperationRequest& req);

    // SSH execution
    Result<DevOpsOperationResult> execute_remote_command(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> batch_remote_command(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> multi_host_command(const DevOpsOperationRequest& req);

    // Docker operations
    Result<DevOpsOperationResult> list_containers(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> restart_container(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> docker_compose_up(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> docker_compose_down(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> docker_prune(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> get_container_logs(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> get_container_stats(const DevOpsOperationRequest& req);

    // Log management
    Result<DevOpsOperationResult> tail_logs(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> clear_logs(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> rotate_logs(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> search_logs(const DevOpsOperationRequest& req);

    // Service management
    Result<DevOpsOperationResult> manage_service(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> list_services(const DevOpsOperationRequest& req);

    // Package management
    Result<DevOpsOperationResult> manage_package(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> system_update(const DevOpsOperationRequest& req);

    // Network diagnostics
    Result<DevOpsOperationResult> check_port(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> dns_lookup(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> ping_host(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> get_network_connections(const DevOpsOperationRequest& req);

    // Deployment operations
    Result<DevOpsOperationResult> check_deployment_health(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> rolling_restart(const DevOpsOperationRequest& req);
    Result<DevOpsOperationResult> rollback_deployment(const DevOpsOperationRequest& req);

private:
    mutable std::shared_mutex session_mutex_;
    std::unordered_map<std::string, int64_t> ssh_session_pool_;  // host -> last_used timestamp
    int32_t max_concurrent_sessions_ = 16;

    // SSH execution backend
    Result<DevOpsOperationResult::HostResult> run_ssh_command(
        const SSHConnectionConfig& ssh, const std::string& command);
    Result<std::vector<DevOpsOperationResult::HostResult>> run_parallel_ssh(
        const std::vector<SSHConnectionConfig>& hosts,
        const std::string& command, int32_t concurrency);

    // HTTP health check backend
    Result<DevOpsOperationResult> check_http_health(const std::string& url,
                                                     int32_t expected_status,
                                                     int32_t timeout_seconds);

    // Internal helpers
    DevOpsOperationResult make_result(DevOpsOperation op, const std::string& host);
    DevOpsOperationRequest parse_task_to_request(const Task& task);
    std::vector<DevOpsOperationResult::ContainerInfo> parse_docker_ps(const std::string& output);
    DevOpsOperationResult::ServerMetrics parse_health_output(const std::string& output);
};

}  // namespace prodxcloud::agents::specialized
