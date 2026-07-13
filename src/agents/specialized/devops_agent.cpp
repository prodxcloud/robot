#include "agents/specialized/devops_agent.hpp"
#include "common/uuid.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <future>
#include <sstream>
#include <thread>

namespace prodxcloud::agents::specialized {

using json = nlohmann::json;

// ─── String Conversions ─────────────────────────────────────────────────────

constexpr std::string_view devops_operation_to_string(DevOpsOperation op) {
    switch (op) {
        case DevOpsOperation::RESTART_SERVER:        return "restart_server";
        case DevOpsOperation::SERVER_HEALTH:         return "server_health";
        case DevOpsOperation::SERVER_PROCESSES:      return "server_processes";
        case DevOpsOperation::DISK_SPACE:            return "disk_space";
        case DevOpsOperation::SYSTEM_INFO:           return "system_info";
        case DevOpsOperation::UPTIME_CHECK:          return "uptime_check";
        case DevOpsOperation::MEMORY_ANALYSIS:       return "memory_analysis";
        case DevOpsOperation::EXECUTE_REMOTE_COMMAND: return "execute_remote_command";
        case DevOpsOperation::BATCH_REMOTE_COMMAND:  return "batch_remote_command";
        case DevOpsOperation::MULTI_HOST_COMMAND:    return "multi_host_command";
        case DevOpsOperation::LIST_CONTAINERS:       return "list_containers";
        case DevOpsOperation::RESTART_CONTAINER:     return "restart_container";
        case DevOpsOperation::STOP_CONTAINER:        return "stop_container";
        case DevOpsOperation::START_CONTAINER:       return "start_container";
        case DevOpsOperation::PULL_IMAGE:            return "pull_image";
        case DevOpsOperation::BUILD_IMAGE:           return "build_image";
        case DevOpsOperation::DOCKER_COMPOSE_UP:     return "docker_compose_up";
        case DevOpsOperation::DOCKER_COMPOSE_DOWN:   return "docker_compose_down";
        case DevOpsOperation::DOCKER_PRUNE:          return "docker_prune";
        case DevOpsOperation::CONTAINER_LOGS:        return "container_logs";
        case DevOpsOperation::CONTAINER_STATS:       return "container_stats";
        case DevOpsOperation::DOCKER_NETWORK_LIST:   return "docker_network_list";
        case DevOpsOperation::DOCKER_VOLUME_LIST:    return "docker_volume_list";
        case DevOpsOperation::TAIL_LOGS:             return "tail_logs";
        case DevOpsOperation::CLEAR_LOGS:            return "clear_logs";
        case DevOpsOperation::ROTATE_LOGS:           return "rotate_logs";
        case DevOpsOperation::SEARCH_LOGS:           return "search_logs";
        case DevOpsOperation::AGGREGATE_LOGS:        return "aggregate_logs";
        case DevOpsOperation::SERVICE_START:         return "service_start";
        case DevOpsOperation::SERVICE_STOP:          return "service_stop";
        case DevOpsOperation::SERVICE_RESTART:       return "service_restart";
        case DevOpsOperation::SERVICE_RELOAD:        return "service_reload";
        case DevOpsOperation::SERVICE_STATUS:        return "service_status";
        case DevOpsOperation::SERVICE_ENABLE:        return "service_enable";
        case DevOpsOperation::SERVICE_DISABLE:       return "service_disable";
        case DevOpsOperation::SERVICE_LIST:          return "service_list";
        case DevOpsOperation::PACKAGE_INSTALL:       return "package_install";
        case DevOpsOperation::PACKAGE_UPDATE:        return "package_update";
        case DevOpsOperation::PACKAGE_REMOVE:        return "package_remove";
        case DevOpsOperation::PACKAGE_LIST:          return "package_list";
        case DevOpsOperation::SYSTEM_UPDATE:         return "system_update";
        case DevOpsOperation::PORT_CHECK:            return "port_check";
        case DevOpsOperation::DNS_LOOKUP:            return "dns_lookup";
        case DevOpsOperation::TRACEROUTE:            return "traceroute";
        case DevOpsOperation::PING_HOST:             return "ping_host";
        case DevOpsOperation::BANDWIDTH_TEST:        return "bandwidth_test";
        case DevOpsOperation::NETWORK_CONNECTIONS:   return "network_connections";
        case DevOpsOperation::FILE_TRANSFER:         return "file_transfer";
        case DevOpsOperation::FILE_SYNC:             return "file_sync";
        case DevOpsOperation::BACKUP_CREATE:         return "backup_create";
        case DevOpsOperation::BACKUP_RESTORE:        return "backup_restore";
        case DevOpsOperation::DEPLOYMENT_HEALTH:     return "deployment_health";
        case DevOpsOperation::ROLLING_RESTART:       return "rolling_restart";
        case DevOpsOperation::BLUE_GREEN_SWITCH:     return "blue_green_switch";
        case DevOpsOperation::CANARY_CHECK:          return "canary_check";
        case DevOpsOperation::ROLLBACK_DEPLOYMENT:   return "rollback_deployment";
        case DevOpsOperation::HEALTH_CHECK:          return "health_check";
        case DevOpsOperation::DRY_RUN:               return "dry_run";
        case DevOpsOperation::CUSTOM_OPERATION:      return "custom_operation";
    }
    return "unknown";
}

// ─── Constructor ────────────────────────────────────────────────────────────

DevOpsAgent::DevOpsAgent(AgentConfig config) : AgentBase(std::move(config)) {
    spdlog::info("DevOpsAgent {} initialized for tenant {}", id(), tenant_id());
}

// ─── AgentBase Interface ────────────────────────────────────────────────────

Result<TaskResult> DevOpsAgent::execute(Task& task) {
    transition_to(AgentState::RUNNING);
    auto start = Clock::now();

    auto req = parse_task_to_request(task);
    auto result = execute_devops_operation(req);

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

void DevOpsAgent::cancel() {
    cancellation_token_.cancel();
    spdlog::info("DevOpsAgent {} cancelled", id());
}

Result<bool> DevOpsAgent::health_check() { return true; }

// ─── Operation Router ───────────────────────────────────────────────────────

Result<DevOpsOperationResult> DevOpsAgent::execute_devops_operation(const DevOpsOperationRequest& req) {
    if (cancellation_token_.is_cancelled())
        return std::unexpected(Error::internal("Operation cancelled"));

    switch (req.operation) {
        case DevOpsOperation::RESTART_SERVER:        return restart_server(req);
        case DevOpsOperation::SERVER_HEALTH:         return check_server_health(req);
        case DevOpsOperation::SERVER_PROCESSES:      return get_server_processes(req);
        case DevOpsOperation::DISK_SPACE:            return check_disk_space(req);
        case DevOpsOperation::SYSTEM_INFO:           return get_system_info(req);
        case DevOpsOperation::EXECUTE_REMOTE_COMMAND: return execute_remote_command(req);
        case DevOpsOperation::BATCH_REMOTE_COMMAND:  return batch_remote_command(req);
        case DevOpsOperation::MULTI_HOST_COMMAND:    return multi_host_command(req);
        case DevOpsOperation::LIST_CONTAINERS:       return list_containers(req);
        case DevOpsOperation::RESTART_CONTAINER:     return restart_container(req);
        case DevOpsOperation::DOCKER_COMPOSE_UP:     return docker_compose_up(req);
        case DevOpsOperation::DOCKER_COMPOSE_DOWN:   return docker_compose_down(req);
        case DevOpsOperation::DOCKER_PRUNE:          return docker_prune(req);
        case DevOpsOperation::CONTAINER_LOGS:        return get_container_logs(req);
        case DevOpsOperation::CONTAINER_STATS:       return get_container_stats(req);
        case DevOpsOperation::TAIL_LOGS:             return tail_logs(req);
        case DevOpsOperation::CLEAR_LOGS:            return clear_logs(req);
        case DevOpsOperation::ROTATE_LOGS:           return rotate_logs(req);
        case DevOpsOperation::SEARCH_LOGS:           return search_logs(req);
        case DevOpsOperation::SERVICE_START:
        case DevOpsOperation::SERVICE_STOP:
        case DevOpsOperation::SERVICE_RESTART:
        case DevOpsOperation::SERVICE_RELOAD:
        case DevOpsOperation::SERVICE_STATUS:
        case DevOpsOperation::SERVICE_ENABLE:
        case DevOpsOperation::SERVICE_DISABLE:       return manage_service(req);
        case DevOpsOperation::SERVICE_LIST:          return list_services(req);
        case DevOpsOperation::PACKAGE_INSTALL:
        case DevOpsOperation::PACKAGE_UPDATE:
        case DevOpsOperation::PACKAGE_REMOVE:        return manage_package(req);
        case DevOpsOperation::SYSTEM_UPDATE:         return system_update(req);
        case DevOpsOperation::PORT_CHECK:            return check_port(req);
        case DevOpsOperation::DNS_LOOKUP:            return dns_lookup(req);
        case DevOpsOperation::PING_HOST:             return ping_host(req);
        case DevOpsOperation::NETWORK_CONNECTIONS:   return get_network_connections(req);
        case DevOpsOperation::DEPLOYMENT_HEALTH:     return check_deployment_health(req);
        case DevOpsOperation::ROLLING_RESTART:       return rolling_restart(req);
        case DevOpsOperation::ROLLBACK_DEPLOYMENT:   return rollback_deployment(req);
        default:
            return std::unexpected(Error::bad_request("Unsupported DevOps operation"));
    }
}

// ─── Server Management ──────────────────────────────────────────────────────

Result<DevOpsOperationResult> DevOpsAgent::restart_server(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::RESTART_SERVER, req.ssh.host);
    auto host_result = run_ssh_command(req.ssh, "sudo reboot");
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::check_server_health(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::SERVER_HEALTH, req.ssh.host);
    std::string cmd =
        "echo '=== CPU ===' && top -bn1 | head -5 && "
        "echo '=== MEMORY ===' && free -h && "
        "echo '=== DISK ===' && df -h | grep -v tmpfs && "
        "echo '=== UPTIME ===' && uptime && "
        "echo '=== LOAD ===' && cat /proc/loadavg";

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
        result.metrics = parse_health_output(host_result->output);
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::get_server_processes(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::SERVER_PROCESSES, req.ssh.host);
    std::string cmd = "ps aux --sort=-%cpu | head -20";
    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::check_disk_space(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::DISK_SPACE, req.ssh.host);
    std::string cmd = "df -h | grep -v tmpfs | grep -v devtmpfs";
    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::get_system_info(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::SYSTEM_INFO, req.ssh.host);
    std::string cmd =
        "echo '=== OS ===' && cat /etc/os-release && "
        "echo '=== KERNEL ===' && uname -a && "
        "echo '=== CPU ===' && lscpu | head -15 && "
        "echo '=== MEMORY ===' && free -h && "
        "echo '=== DISK ===' && lsblk";

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

// ─── SSH Execution ──────────────────────────────────────────────────────────

Result<DevOpsOperationResult> DevOpsAgent::execute_remote_command(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::EXECUTE_REMOTE_COMMAND, req.ssh.host);
    auto host_result = run_ssh_command(req.ssh, req.command);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::batch_remote_command(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::BATCH_REMOTE_COMMAND, req.ssh.host);
    auto host_result = run_ssh_command(req.ssh, req.command);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::multi_host_command(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::MULTI_HOST_COMMAND, "multi");
    auto start = Clock::now();

    std::vector<SSHConnectionConfig> hosts;
    for (const auto& host : req.target_hosts) {
        SSHConnectionConfig ssh = req.ssh;
        ssh.host = host;
        hosts.push_back(std::move(ssh));
    }

    auto parallel_result = run_parallel_ssh(hosts, req.command, req.concurrency);
    if (parallel_result) {
        result.host_results = std::move(*parallel_result);
        result.success = true;
        json output_arr = json::array();
        for (const auto& hr : result.host_results) {
            output_arr.push_back({
                {"host", hr.host},
                {"success", hr.success},
                {"exit_code", hr.exit_code},
                {"output", hr.output},
                {"duration_ms", hr.duration_ms}
            });
            if (!hr.success) result.success = false;
        }
        result.output = output_arr.dump(2);
    } else {
        result.error_message = parallel_result.error().message;
    }

    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

// ─── Docker Operations ──────────────────────────────────────────────────────

Result<DevOpsOperationResult> DevOpsAgent::list_containers(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::LIST_CONTAINERS, req.ssh.host);
    std::string cmd = "docker ps --format '{{.ID}}|{{.Names}}|{{.Image}}|{{.Status}}|{{.Ports}}|{{.CreatedAt}}'";
    if (req.all_containers) cmd = "docker ps -a --format '{{.ID}}|{{.Names}}|{{.Image}}|{{.Status}}|{{.Ports}}|{{.CreatedAt}}'";

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
        result.containers = parse_docker_ps(host_result->output);
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::restart_container(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::RESTART_CONTAINER, req.ssh.host);
    std::string cmd;
    if (req.pull_latest && !req.image_name.empty()) {
        cmd = "docker pull " + req.image_name + " && docker restart " + req.container_name;
    } else {
        cmd = "docker restart " + req.container_name;
    }

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::docker_compose_up(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::DOCKER_COMPOSE_UP, req.ssh.host);
    std::string cmd = "docker compose -f " + req.compose_file + " up -d";
    if (req.pull_latest) cmd += " --pull always";

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::docker_compose_down(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::DOCKER_COMPOSE_DOWN, req.ssh.host);
    std::string cmd = "docker compose -f " + req.compose_file + " down";

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::docker_prune(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::DOCKER_PRUNE, req.ssh.host);
    std::string cmd = "docker system prune -af --volumes";

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::get_container_logs(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::CONTAINER_LOGS, req.ssh.host);
    std::string cmd = "docker logs --tail " + std::to_string(req.log_lines) + " " + req.container_name;

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::get_container_stats(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::CONTAINER_STATS, req.ssh.host);
    std::string cmd = "docker stats --no-stream --format '{{.Name}}|{{.CPUPerc}}|{{.MemUsage}}|{{.NetIO}}|{{.BlockIO}}'";

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

// ─── Log Management ─────────────────────────────────────────────────────────

Result<DevOpsOperationResult> DevOpsAgent::tail_logs(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::TAIL_LOGS, req.ssh.host);
    std::string cmd = "tail -n " + std::to_string(req.log_lines) + " " + req.log_file;
    if (!req.log_pattern.empty())
        cmd += " | grep '" + req.log_pattern + "'";

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::clear_logs(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::CLEAR_LOGS, req.ssh.host);
    std::string pattern = req.log_pattern.empty() ? "*.log" : req.log_pattern;
    std::string cmd;
    if (req.dry_run) {
        cmd = "find " + req.log_file + " -name '" + pattern + "' -mtime +" +
              std::to_string(req.older_than_days) + " -print";
    } else {
        cmd = "find " + req.log_file + " -name '" + pattern + "' -mtime +" +
              std::to_string(req.older_than_days) + " -delete -print";
    }

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::rotate_logs(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::ROTATE_LOGS, req.ssh.host);
    std::string cmd = "sudo logrotate -f /etc/logrotate.conf";
    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::search_logs(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::SEARCH_LOGS, req.ssh.host);
    std::string cmd = "grep -rn '" + req.log_pattern + "' " + req.log_file + " | tail -" +
                      std::to_string(req.log_lines);

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

// ─── Service Management ─────────────────────────────────────────────────────

Result<DevOpsOperationResult> DevOpsAgent::manage_service(const DevOpsOperationRequest& req) {
    auto result = make_result(req.operation, req.ssh.host);
    std::string action;
    switch (req.operation) {
        case DevOpsOperation::SERVICE_START:   action = "start"; break;
        case DevOpsOperation::SERVICE_STOP:    action = "stop"; break;
        case DevOpsOperation::SERVICE_RESTART: action = "restart"; break;
        case DevOpsOperation::SERVICE_RELOAD:  action = "reload"; break;
        case DevOpsOperation::SERVICE_STATUS:  action = "status"; break;
        case DevOpsOperation::SERVICE_ENABLE:  action = "enable"; break;
        case DevOpsOperation::SERVICE_DISABLE: action = "disable"; break;
        default: action = req.service_action; break;
    }

    std::string cmd = "sudo systemctl " + action + " " + req.service_name;
    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::list_services(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::SERVICE_LIST, req.ssh.host);
    std::string cmd = "systemctl list-units --type=service --state=running --no-pager";
    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

// ─── Package Management ─────────────────────────────────────────────────────

Result<DevOpsOperationResult> DevOpsAgent::manage_package(const DevOpsOperationRequest& req) {
    auto result = make_result(req.operation, req.ssh.host);
    std::string cmd;

    switch (req.operation) {
        case DevOpsOperation::PACKAGE_INSTALL:
            cmd = "sudo apt-get install -y " + req.package_name;
            if (!req.package_version.empty()) cmd += "=" + req.package_version;
            break;
        case DevOpsOperation::PACKAGE_UPDATE:
            cmd = "sudo apt-get update && sudo apt-get upgrade -y " + req.package_name;
            break;
        case DevOpsOperation::PACKAGE_REMOVE:
            cmd = "sudo apt-get remove -y " + req.package_name;
            break;
        default:
            cmd = "dpkg -l | grep " + req.package_name;
            break;
    }

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::system_update(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::SYSTEM_UPDATE, req.ssh.host);
    std::string cmd = "sudo apt-get update && sudo apt-get upgrade -y && sudo apt-get autoremove -y";
    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

// ─── Network Diagnostics ────────────────────────────────────────────────────

Result<DevOpsOperationResult> DevOpsAgent::check_port(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::PORT_CHECK, req.ssh.host);
    std::string cmd = "nc -zv " + req.ssh.host + " " + std::to_string(req.target_port) + " 2>&1";
    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::dns_lookup(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::DNS_LOOKUP, req.ssh.host);
    std::string cmd = "dig " + req.dns_name + " +short && dig " + req.dns_name + " MX +short";
    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::ping_host(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::PING_HOST, req.ssh.host);
    std::string cmd = "ping -c 4 " + req.dns_name;
    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::get_network_connections(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::NETWORK_CONNECTIONS, req.ssh.host);
    std::string cmd = "ss -tunlp | head -50";
    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = true;
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

// ─── Deployment Operations ──────────────────────────────────────────────────

Result<DevOpsOperationResult> DevOpsAgent::check_deployment_health(const DevOpsOperationRequest& req) {
    return check_http_health(req.service_url, req.expected_status, req.health_timeout_seconds);
}

Result<DevOpsOperationResult> DevOpsAgent::rolling_restart(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::ROLLING_RESTART, "multi");
    auto start = Clock::now();

    result.success = true;
    for (const auto& host : req.target_hosts) {
        if (cancellation_token_.is_cancelled()) {
            result.success = false;
            result.error_message = "Rolling restart cancelled";
            break;
        }

        SSHConnectionConfig ssh = req.ssh;
        ssh.host = host;

        auto hr = run_ssh_command(ssh, "sudo systemctl restart " + req.service_name);
        DevOpsOperationResult::HostResult host_res;
        host_res.host = host;
        if (hr) {
            host_res.success = (hr->exit_code == 0);
            host_res.output = hr->output;
            host_res.exit_code = hr->exit_code;
        } else {
            host_res.success = false;
            result.success = false;
        }
        result.host_results.push_back(std::move(host_res));

        // Wait between restarts for rolling behavior
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

Result<DevOpsOperationResult> DevOpsAgent::rollback_deployment(const DevOpsOperationRequest& req) {
    auto result = make_result(DevOpsOperation::ROLLBACK_DEPLOYMENT, req.ssh.host);
    std::string cmd = req.command;
    if (cmd.empty()) cmd = "echo 'No rollback command specified'";

    auto host_result = run_ssh_command(req.ssh, cmd);
    if (host_result) {
        result.success = (host_result->exit_code == 0);
        result.output = host_result->output;
        result.exit_code = host_result->exit_code;
    } else {
        result.error_message = host_result.error().message;
    }
    return result;
}

// ─── SSH Backend ────────────────────────────────────────────────────────────

Result<DevOpsOperationResult::HostResult> DevOpsAgent::run_ssh_command(
    const SSHConnectionConfig& ssh, const std::string& command) {
    auto start = Clock::now();
    DevOpsOperationResult::HostResult result;
    result.host = ssh.host;

    std::string ssh_cmd = "ssh -o StrictHostKeyChecking=no -o ConnectTimeout=" +
                          std::to_string(ssh.timeout_seconds) +
                          " -p " + std::to_string(ssh.port);

    if (!ssh.key_path.empty())
        ssh_cmd += " -i " + ssh.key_path;

    ssh_cmd += " " + ssh.user + "@" + ssh.host + " '" + command + "' 2>&1";

    spdlog::debug("DevOpsAgent SSH: {}", ssh_cmd);

    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(ssh_cmd.c_str(), "r");
    if (!pipe) {
        return std::unexpected(Error::internal("Failed to open SSH connection to " + ssh.host));
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        if (cancellation_token_.is_cancelled()) {
            pclose(pipe);
            return std::unexpected(Error::internal("SSH command cancelled"));
        }
        output += buffer.data();
    }

    int status = pclose(pipe);
    result.exit_code = WEXITSTATUS(status);
    result.output = std::move(output);
    result.success = (result.exit_code == 0);
    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    return result;
}

Result<std::vector<DevOpsOperationResult::HostResult>> DevOpsAgent::run_parallel_ssh(
    const std::vector<SSHConnectionConfig>& hosts,
    const std::string& command, int32_t concurrency) {

    std::vector<std::future<Result<DevOpsOperationResult::HostResult>>> futures;
    futures.reserve(hosts.size());

    // Launch SSH commands in parallel, limited by concurrency
    size_t batch_size = static_cast<size_t>(concurrency);
    std::vector<DevOpsOperationResult::HostResult> results;

    for (size_t i = 0; i < hosts.size(); i += batch_size) {
        std::vector<std::future<Result<DevOpsOperationResult::HostResult>>> batch;
        for (size_t j = i; j < std::min(i + batch_size, hosts.size()); ++j) {
            batch.push_back(std::async(std::launch::async,
                [this, &hosts, j, &command]() {
                    return run_ssh_command(hosts[j], command);
                }));
        }

        for (auto& f : batch) {
            auto r = f.get();
            if (r) {
                results.push_back(std::move(*r));
            } else {
                DevOpsOperationResult::HostResult hr;
                hr.success = false;
                hr.output = r.error().message;
                results.push_back(std::move(hr));
            }
        }
    }

    return results;
}

// ─── HTTP Health Backend ────────────────────────────────────────────────────

Result<DevOpsOperationResult> DevOpsAgent::check_http_health(const std::string& url,
                                                              int32_t expected_status,
                                                              int32_t timeout_seconds) {
    auto result = make_result(DevOpsOperation::DEPLOYMENT_HEALTH, url);
    auto start = Clock::now();

    std::string cmd = "curl -s -o /dev/null -w '%{http_code}' --max-time " +
                      std::to_string(timeout_seconds) + " '" + url + "'";

    std::array<char, 256> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.error_message = "Failed to execute health check";
        return result;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        output += buffer.data();
    pclose(pipe);

    int status_code = 0;
    try { status_code = std::stoi(output); } catch (...) {}

    result.success = (status_code == expected_status);
    result.exit_code = status_code;
    result.output = json{
        {"url", url},
        {"status_code", status_code},
        {"expected_status", expected_status},
        {"healthy", result.success}
    }.dump(2);
    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

// ─── Internal Helpers ───────────────────────────────────────────────────────

DevOpsOperationResult DevOpsAgent::make_result(DevOpsOperation op, const std::string& host) {
    DevOpsOperationResult result;
    result.operation = op;
    result.host = host;
    result.timestamp = now_iso8601();
    return result;
}

std::vector<DevOpsOperationResult::ContainerInfo> DevOpsAgent::parse_docker_ps(const std::string& output) {
    std::vector<DevOpsOperationResult::ContainerInfo> containers;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        DevOpsOperationResult::ContainerInfo c;
        std::istringstream ls(line);
        std::string field;
        int idx = 0;
        while (std::getline(ls, field, '|')) {
            switch (idx++) {
                case 0: c.id = field; break;
                case 1: c.name = field; break;
                case 2: c.image = field; break;
                case 3: c.status = field; break;
                case 4: c.ports = field; break;
                case 5: c.created = field; break;
            }
        }
        containers.push_back(std::move(c));
    }
    return containers;
}

DevOpsOperationResult::ServerMetrics DevOpsAgent::parse_health_output(const std::string& output) {
    DevOpsOperationResult::ServerMetrics metrics;
    // Simple parsing — real implementation would be more robust
    if (auto pos = output.find("load average:"); pos != std::string::npos) {
        std::istringstream iss(output.substr(pos + 14));
        char comma;
        iss >> metrics.load_1m >> comma >> metrics.load_5m >> comma >> metrics.load_15m;
    }
    return metrics;
}

DevOpsOperationRequest DevOpsAgent::parse_task_to_request(const Task& task) {
    DevOpsOperationRequest req;
    json payload = json::parse(task.payload, nullptr, false);
    if (payload.is_discarded()) payload = json::parse(task.input_json, nullptr, false);
    if (payload.is_discarded()) return req;

    std::string op_str = payload.value("operation", "health_check");
    if (op_str == "restart_server") req.operation = DevOpsOperation::RESTART_SERVER;
    else if (op_str == "server_health") req.operation = DevOpsOperation::SERVER_HEALTH;
    else if (op_str == "server_processes") req.operation = DevOpsOperation::SERVER_PROCESSES;
    else if (op_str == "disk_space") req.operation = DevOpsOperation::DISK_SPACE;
    else if (op_str == "system_info") req.operation = DevOpsOperation::SYSTEM_INFO;
    else if (op_str == "execute_remote_command") req.operation = DevOpsOperation::EXECUTE_REMOTE_COMMAND;
    else if (op_str == "multi_host_command") req.operation = DevOpsOperation::MULTI_HOST_COMMAND;
    else if (op_str == "list_containers") req.operation = DevOpsOperation::LIST_CONTAINERS;
    else if (op_str == "restart_container") req.operation = DevOpsOperation::RESTART_CONTAINER;
    else if (op_str == "docker_compose_up") req.operation = DevOpsOperation::DOCKER_COMPOSE_UP;
    else if (op_str == "docker_compose_down") req.operation = DevOpsOperation::DOCKER_COMPOSE_DOWN;
    else if (op_str == "docker_prune") req.operation = DevOpsOperation::DOCKER_PRUNE;
    else if (op_str == "container_logs") req.operation = DevOpsOperation::CONTAINER_LOGS;
    else if (op_str == "container_stats") req.operation = DevOpsOperation::CONTAINER_STATS;
    else if (op_str == "tail_logs") req.operation = DevOpsOperation::TAIL_LOGS;
    else if (op_str == "clear_logs") req.operation = DevOpsOperation::CLEAR_LOGS;
    else if (op_str == "rotate_logs") req.operation = DevOpsOperation::ROTATE_LOGS;
    else if (op_str == "search_logs") req.operation = DevOpsOperation::SEARCH_LOGS;
    else if (op_str == "service_start") req.operation = DevOpsOperation::SERVICE_START;
    else if (op_str == "service_stop") req.operation = DevOpsOperation::SERVICE_STOP;
    else if (op_str == "service_restart") req.operation = DevOpsOperation::SERVICE_RESTART;
    else if (op_str == "service_status") req.operation = DevOpsOperation::SERVICE_STATUS;
    else if (op_str == "deployment_health") req.operation = DevOpsOperation::DEPLOYMENT_HEALTH;
    else if (op_str == "rolling_restart") req.operation = DevOpsOperation::ROLLING_RESTART;
    else req.operation = DevOpsOperation::HEALTH_CHECK;

    req.ssh.host = payload.value("host", "");
    req.ssh.user = payload.value("user", "root");
    req.ssh.port = payload.value("port", 22);
    req.ssh.key_path = payload.value("key_path", "");
    req.ssh.timeout_seconds = payload.value("timeout", 30);
    req.command = payload.value("command", "");
    req.container_name = payload.value("container_name", "");
    req.image_name = payload.value("image_name", "");
    req.compose_file = payload.value("compose_file", "docker-compose.yml");
    req.pull_latest = payload.value("pull_latest", false);
    req.all_containers = payload.value("all_containers", false);
    req.log_file = payload.value("log_file", "/var/log/syslog");
    req.log_pattern = payload.value("log_pattern", "");
    req.log_lines = payload.value("log_lines", 100);
    req.older_than_days = payload.value("older_than_days", 30);
    req.service_name = payload.value("service_name", "");
    req.package_name = payload.value("package_name", "");
    req.service_url = payload.value("service_url", "");
    req.expected_status = payload.value("expected_status", 200);
    req.dns_name = payload.value("dns_name", "");
    req.target_port = payload.value("target_port", 0);
    req.dry_run = payload.value("dry_run", false);
    req.concurrency = payload.value("concurrency", 4);

    if (payload.contains("target_hosts")) {
        for (const auto& h : payload["target_hosts"])
            req.target_hosts.push_back(h.get<std::string>());
    }

    return req;
}

}  // namespace prodxcloud::agents::specialized
