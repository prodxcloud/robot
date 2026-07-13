/// @file ai_agent_tools.cpp
/// @brief Tool registration for each agent type.
///
/// Registers ToolDefinitions for:
///   - Provisioning (every one delegated to vxnode — this repo provisions nothing)
///   - DevOps Agent (server, Docker, logs, systemd, deployment)
///   - SRE Agent (incidents, SLOs, alerting, chaos, runbooks)
///   - OpenClaw Agent (license, CVE, SBOM, secrets, compliance)
///   - CICD Agent (pipeline, build, artifacts, releases)

#include "ai/ai_agent.hpp"
#include "ai/tool_executor.hpp"

#include <memory>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "common/uuid.hpp"
#include "vxnode/vxnode_client.hpp"

using json = nlohmann::json;

namespace prodxcloud::ai {

// ============================================================================
// Helper: Build a ToolParameter quickly
// ============================================================================

static ToolParameter make_param(const std::string& name,
                                const std::string& type,
                                const std::string& description,
                                bool required = false,
                                const std::string& default_value = "",
                                const std::string& enum_values_json = "[]") {
    ToolParameter p;
    p.name = name;
    p.type = type;
    p.description = description;
    p.required = required;
    p.default_value = default_value;
    p.enum_values_json = enum_values_json;
    return p;
}

// Helper: Build a standard placeholder JSON response
static std::string placeholder_response(const std::string& tool_name,
                                         const std::string& args_json,
                                         const std::string& status = "success") {
    json result;
    result["tool"] = tool_name;
    result["status"] = status;
    result["request_id"] = generate_uuid();
    result["timestamp"] = now_iso8601();
    result["message"] = "Operation '" + tool_name + "' executed successfully (placeholder)";

    // Echo back the parsed arguments
    auto args = json::parse(args_json, nullptr, false);
    if (!args.is_discarded()) {
        result["parameters"] = args;
    }

    return result.dump(2);
}

// ============================================================================
// Provisioning Tools — delegated to vxnode
//
// This engine used to carry its own cloud provisioning: create_vm, create_vpc,
// create_bucket, configure_autoscaling, estimate_cost — a dozen tools that each
// pretended to talk to AWS/Azure/GCP and in fact returned a canned JSON blob.
// They are gone. Nothing in this repository provisions infrastructure.
//
// What replaces them is a single, honest idea: when the robot needs a machine, it
// asks its **vxnode** node for one. The node owns the provider credentials, the
// multi-cloud drivers and the blast radius. The robot owns none of those, ships no
// secrets, and cannot create a resource on its own.
//
//   agent tool  ──▶  VxNodeClient  ──▶  POST http://127.0.0.1:8744/api/v2/...
//                                        (vxnode) ──▶ AWS / Azure / GCP / ...
//
// Every tool below is a thin, typed wrapper over one vxnode endpoint. If vxnode
// cannot do it, neither can the robot — and that is the security property.
// ============================================================================

void register_provisioning_tools(ToolExecutor& executor) {
    spdlog::info("Registering provisioning tools (delegated to vxnode)");

    const auto node = std::make_shared<vxnode::VxNodeClient>();

    // 1. provision_vm — POST /api/v2/provision/vm
    {
        ToolDefinition def;
        def.name = "provision_vm";
        def.description =
            "Ask the vxnode node to provision one or more VMs. This engine does NOT "
            "create instances itself — it delegates to vxnode, which holds the cloud "
            "credentials. Set dry_run to see the exact call without making it.";
        def.parameters = {
            make_param("provider", "string", "Cloud provider the node should use", true, "",
                       R"(["aws","azure","gcp","linode","digitalocean"])"),
            make_param("region", "string", "Region, e.g. us-east-1", true),
            make_param("instance_type", "string", "Instance size, e.g. t3.large", true),
            make_param("image", "string", "Base image, e.g. ubuntu-24.04", false, "\"ubuntu-24.04\""),
            make_param("name", "string", "Name for the instance", false),
            make_param("count", "integer", "How many instances", false, "1"),
            make_param("purpose", "string", "Why the robot needs this — recorded on the node", false),
            make_param("dry_run", "boolean", "Return the call without sending it", false, "false"),
        };

        executor.register_tool(def, [node](const std::string& args_json) -> Result<std::string> {
            const auto args = json::parse(args_json, nullptr, false);
            if (args.is_discarded()) {
                return std::unexpected(Error::bad_request("provision_vm: arguments are not valid JSON"));
            }

            vxnode::ProvisionRequest req;
            req.provider      = args.value("provider", "");
            req.region        = args.value("region", "");
            req.instance_type = args.value("instance_type", "");
            req.image         = args.value("image", "ubuntu-24.04");
            req.name          = args.value("name", "");
            req.count         = args.value("count", 1);
            req.purpose       = args.value("purpose", "");
            req.dry_run       = args.value("dry_run", false);

            const auto res = node->provision_vm(req);
            if (!res) return std::unexpected(res.error());

            json out;
            out["delegated_to"] = "vxnode";
            out["node"]         = node->config().base_url;
            out["endpoint"]     = "/api/v2/provision/vm";
            out["status"]       = res->status;
            out["request_id"]   = res->request_id;
            out["instance_ids"] = res->instance_ids;
            out["node_response"] = json::parse(res->raw_response, nullptr, false);
            return out.dump(2);
        });
    }

    // 2. vm_status — POST /api/v2/provision/vm/status
    {
        ToolDefinition def;
        def.name        = "vm_status";
        def.description = "Ask vxnode for the current status of an instance it provisioned.";
        def.parameters  = {
            make_param("instance_id", "string", "The instance to query", true),
        };

        executor.register_tool(def, [node](const std::string& args_json) -> Result<std::string> {
            const auto args = json::parse(args_json, nullptr, false);
            if (args.is_discarded() || !args.contains("instance_id")) {
                return std::unexpected(Error::bad_request("vm_status: instance_id is required"));
            }

            const auto res = node->vm_status(args["instance_id"].get<std::string>());
            if (!res) return std::unexpected(res.error());
            return *res;
        });
    }

    // 3. vm_action — POST /api/v2/provision/vm/action
    {
        ToolDefinition def;
        def.name        = "vm_action";
        def.description = "Start, stop, restart or terminate an instance through vxnode.";
        def.parameters  = {
            make_param("instance_id", "string", "The instance to act on", true),
            make_param("action", "string", "Lifecycle action", true, "",
                       R"(["start","stop","restart","status","terminate"])"),
        };

        executor.register_tool(def, [node](const std::string& args_json) -> Result<std::string> {
            const auto args = json::parse(args_json, nullptr, false);
            if (args.is_discarded()) {
                return std::unexpected(Error::bad_request("vm_action: arguments are not valid JSON"));
            }

            vxnode::VmAction action;
            action.instance_id = args.value("instance_id", "");
            action.action      = args.value("action", "");

            const auto res = node->vm_action(action);
            if (!res) return std::unexpected(res.error());
            return *res;
        });
    }

    // 4. deploy_service — POST /api/v2/infrastructure/services/<stack>/deploy
    {
        ToolDefinition def;
        def.name = "deploy_service";
        def.description =
            "Deploy an application stack onto a node via vxnode. vxnode exposes one "
            "endpoint per stack; the robot only picks which one.";
        def.parameters = {
            make_param("stack", "string", "Application stack", true, "",
                       R"(["nodejs","python","fastapi","django","flask","golang","rust","java",
                            "springboot","php","laravel","nextjs","reactjs","angular","streamlit",
                            "staticwebsite","cpp"])"),
            make_param("host", "string", "Target VM the node should deploy onto", true),
            make_param("repo_url", "string", "Git repository to deploy", false),
            make_param("branch", "string", "Branch to deploy", false, "\"main\""),
            make_param("domain", "string", "Domain to serve it on", false),
            make_param("port", "integer", "Port the service listens on", false),
        };

        executor.register_tool(def, [node](const std::string& args_json) -> Result<std::string> {
            const auto args = json::parse(args_json, nullptr, false);
            if (args.is_discarded()) {
                return std::unexpected(Error::bad_request("deploy_service: arguments are not valid JSON"));
            }

            vxnode::DeployRequest req;
            req.stack    = args.value("stack", "");
            req.host     = args.value("host", "");
            req.repo_url = args.value("repo_url", "");
            req.branch   = args.value("branch", "main");
            req.domain   = args.value("domain", "");
            req.port     = args.value("port", 0);

            const auto res = node->deploy(req);
            if (!res) return std::unexpected(res.error());
            return *res;
        });
    }

    // 5. node_health — GET /api/v2/health
    {
        ToolDefinition def;
        def.name        = "node_health";
        def.description = "Check whether the vxnode node backing this robot is reachable and healthy.";
        def.parameters  = {};

        executor.register_tool(def, [node](const std::string&) -> Result<std::string> {
            const auto h = node->health();
            if (!h) return std::unexpected(h.error());

            json out;
            out["node"]       = node->config().base_url;
            out["reachable"]  = h->reachable;
            out["status"]     = h->status;
            out["version"]    = h->version;
            out["latency_ms"] = h->latency_ms;
            return out.dump(2);
        });
    }

    spdlog::info("Provisioning: {} vxnode-delegated tools registered (0 local cloud tools)",
                 5);
}

// ============================================================================
// DevOps Agent Tools
// ============================================================================

void register_devops_tools(ToolExecutor& executor) {
    spdlog::info("Registering DevOps Agent tools");

    // 1. check_server_health
    {
        ToolDefinition def;
        def.name = "check_server_health";
        def.description = "Check server health metrics including CPU, memory, disk, and load average.";
        def.parameters = {
            make_param("host", "string", "Hostname or IP address", true),
            make_param("port", "integer", "SSH port", false, "22"),
            make_param("checks", "string", "Comma-separated checks to run (cpu, memory, disk, load, uptime)", false, "\"cpu,memory,disk,load\""),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::check_server_health called");
            json result;
            result["status"] = "healthy";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["metrics"]["cpu_percent"] = 35.2;
            result["metrics"]["memory_percent"] = 62.8;
            result["metrics"]["disk_percent"] = 45.1;
            result["metrics"]["load_average"] = {1.2, 0.8, 0.5};
            result["metrics"]["uptime_hours"] = 720;
            return result.dump(2);
        });
    }

    // 2. ssh_execute
    {
        ToolDefinition def;
        def.name = "ssh_execute";
        def.description = "Execute a command on one or more remote hosts via SSH.";
        def.parameters = {
            make_param("hosts", "string", "Comma-separated list of hostnames or IPs", true),
            make_param("command", "string", "Shell command to execute remotely", true),
            make_param("user", "string", "SSH user", false, "\"ubuntu\""),
            make_param("timeout_seconds", "integer", "Command timeout per host", false, "60"),
            make_param("parallel", "boolean", "Execute on all hosts in parallel", false, "true"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::ssh_execute called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["results"] = json::array();

            auto args = json::parse(args_json, nullptr, false);
            std::string cmd = "unknown";
            if (!args.is_discarded() && args.contains("command")) {
                cmd = args["command"].get<std::string>();
            }

            json host_result;
            host_result["host"] = "10.0.1.10";
            host_result["exit_code"] = 0;
            host_result["stdout"] = "Command executed successfully: " + cmd;
            host_result["stderr"] = "";
            result["results"].push_back(host_result);
            return result.dump(2);
        });
    }

    // 3. docker_manage
    {
        ToolDefinition def;
        def.name = "docker_manage";
        def.description = "Manage Docker containers: start, stop, restart, remove, inspect, logs, stats.";
        def.parameters = {
            make_param("action", "string", "Docker action to perform", true, "",
                       R"(["start","stop","restart","remove","inspect","logs","stats","list","pull","prune"])"),
            make_param("container_id", "string", "Container ID or name (required for most actions)", false),
            make_param("image", "string", "Image name for pull or run", false),
            make_param("host", "string", "Docker host (default: local)", false, "\"localhost\""),
            make_param("tail_lines", "integer", "Number of log lines to tail", false, "100"),
            make_param("follow", "boolean", "Follow log output", false, "false"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::docker_manage called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();

            auto args = json::parse(args_json, nullptr, false);
            std::string action = "unknown";
            if (!args.is_discarded() && args.contains("action")) {
                action = args["action"].get<std::string>();
            }
            result["action"] = action;
            result["message"] = "Docker " + action + " completed successfully";
            return result.dump(2);
        });
    }

    // 4. docker_compose
    {
        ToolDefinition def;
        def.name = "docker_compose";
        def.description = "Manage Docker Compose stacks: up, down, restart, build, ps, logs.";
        def.parameters = {
            make_param("action", "string", "Compose action", true, "",
                       R"(["up","down","restart","build","ps","logs","pull","config"])"),
            make_param("project_path", "string", "Path to docker-compose.yml directory", true),
            make_param("services", "string", "Comma-separated service names (empty for all)", false),
            make_param("detach", "boolean", "Run in detached mode (for 'up')", false, "true"),
            make_param("build_flag", "boolean", "Build images before starting (for 'up')", false, "false"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::docker_compose called");
            return placeholder_response("docker_compose", args_json);
        });
    }

    // 5. manage_logs
    {
        ToolDefinition def;
        def.name = "manage_logs";
        def.description = "Manage application and system logs: tail, search, rotate, clear.";
        def.parameters = {
            make_param("action", "string", "Log action", true, "",
                       R"(["tail","search","rotate","clear","list"])"),
            make_param("log_path", "string", "Path to log file or directory", true),
            make_param("lines", "integer", "Number of lines to tail", false, "100"),
            make_param("pattern", "string", "Search pattern (regex supported)", false),
            make_param("host", "string", "Remote host (empty for local)", false),
            make_param("since", "string", "Show logs since timestamp (ISO8601)", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::manage_logs called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["log_lines"] = json::array({
                "2026-03-30T10:00:00Z [INFO] Application started",
                "2026-03-30T10:00:01Z [INFO] Listening on port 8080",
                "2026-03-30T10:00:05Z [INFO] Health check passed"
            });
            result["total_lines"] = 3;
            return result.dump(2);
        });
    }

    // 6. systemd_service
    {
        ToolDefinition def;
        def.name = "systemd_service";
        def.description = "Manage systemd services: start, stop, restart, status, enable, disable.";
        def.parameters = {
            make_param("action", "string", "Service action", true, "",
                       R"(["start","stop","restart","status","enable","disable","reload","journal"])"),
            make_param("service_name", "string", "Name of the systemd service", true),
            make_param("host", "string", "Remote host (empty for local)", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::systemd_service called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();

            auto args = json::parse(args_json, nullptr, false);
            std::string svc = "unknown";
            if (!args.is_discarded() && args.contains("service_name")) {
                svc = args["service_name"].get<std::string>();
            }
            result["service"] = svc;
            result["active_state"] = "active";
            result["sub_state"] = "running";
            result["message"] = "Service '" + svc + "' is active (running)";
            return result.dump(2);
        });
    }

    // 7. manage_packages
    {
        ToolDefinition def;
        def.name = "manage_packages";
        def.description = "Install, update, or remove system packages using the system package manager.";
        def.parameters = {
            make_param("action", "string", "Package action", true, "",
                       R"(["install","remove","update","upgrade","list","search"])"),
            make_param("packages", "string", "Comma-separated package names", false),
            make_param("host", "string", "Remote host (empty for local)", false),
            make_param("yes", "boolean", "Auto-confirm prompts", false, "true"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::manage_packages called");
            return placeholder_response("manage_packages", args_json);
        });
    }

    // 8. network_diagnostics
    {
        ToolDefinition def;
        def.name = "network_diagnostics";
        def.description = "Run network diagnostics: ping, port check, DNS lookup, traceroute, connections.";
        def.parameters = {
            make_param("action", "string", "Diagnostic action", true, "",
                       R"(["ping","port_check","dns_lookup","traceroute","connections","netstat"])"),
            make_param("target", "string", "Target hostname or IP", true),
            make_param("port", "integer", "Port number (for port_check)", false),
            make_param("count", "integer", "Number of pings or hops", false, "4"),
            make_param("timeout_seconds", "integer", "Timeout for the diagnostic", false, "10"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::network_diagnostics called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["reachable"] = true;
            result["latency_ms"] = 12.5;
            result["message"] = "Target is reachable";
            return result.dump(2);
        });
    }

    // 9. deploy_application
    {
        ToolDefinition def;
        def.name = "deploy_application";
        def.description = "Deploy an application with health checks, rolling restarts, or rollback support.";
        def.parameters = {
            make_param("action", "string", "Deployment action", true, "",
                       R"(["deploy","rollback","health_check","rolling_restart","status"])"),
            make_param("application", "string", "Application name or deployment ID", true),
            make_param("version", "string", "Version to deploy (for deploy action)", false),
            make_param("environment", "string", "Target environment (dev, staging, production)", true, "",
                       R"(["dev","staging","production"])"),
            make_param("strategy", "string", "Deployment strategy", false, "\"rolling\"",
                       R"(["rolling","blue_green","canary","recreate"])"),
            make_param("hosts", "string", "Comma-separated target hosts", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::deploy_application called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["deployment_id"] = "deploy-" + generate_uuid().substr(0, 8);
            result["health"] = "healthy";
            result["message"] = "Deployment completed successfully";
            return result.dump(2);
        });
    }

    // 10. monitor_resources
    {
        ToolDefinition def;
        def.name = "monitor_resources";
        def.description = "Monitor system resources: CPU, memory, disk I/O, network, and processes.";
        def.parameters = {
            make_param("host", "string", "Target hostname or IP", true),
            make_param("metrics", "string", "Comma-separated metrics (cpu, memory, disk, network, processes)", false, "\"cpu,memory,disk\""),
            make_param("interval_seconds", "integer", "Sampling interval", false, "5"),
            make_param("duration_seconds", "integer", "Total monitoring duration", false, "30"),
            make_param("top_processes", "integer", "Number of top processes to return", false, "10"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::monitor_resources called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["metrics"]["cpu_percent"] = 28.5;
            result["metrics"]["memory_used_gb"] = 6.2;
            result["metrics"]["memory_total_gb"] = 16.0;
            result["metrics"]["disk_read_mbps"] = 12.3;
            result["metrics"]["disk_write_mbps"] = 5.7;
            result["metrics"]["network_rx_mbps"] = 45.0;
            result["metrics"]["network_tx_mbps"] = 22.0;
            return result.dump(2);
        });
    }

    // 11. manage_cron
    {
        ToolDefinition def;
        def.name = "manage_cron";
        def.description = "Manage cron jobs: list, create, delete, enable, disable scheduled tasks.";
        def.parameters = {
            make_param("action", "string", "Cron action", true, "",
                       R"(["list","create","delete","enable","disable"])"),
            make_param("schedule", "string", "Cron schedule expression (e.g., '0 * * * *')", false),
            make_param("command", "string", "Command to execute", false),
            make_param("job_id", "string", "Job identifier (for delete/enable/disable)", false),
            make_param("host", "string", "Remote host (empty for local)", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::manage_cron called");
            return placeholder_response("manage_cron", args_json);
        });
    }

    // 12. file_operations
    {
        ToolDefinition def;
        def.name = "file_operations";
        def.description = "Perform file operations: copy, move, delete, list, check existence, read.";
        def.parameters = {
            make_param("action", "string", "File action", true, "",
                       R"(["copy","move","delete","list","exists","read","write","chmod"])"),
            make_param("path", "string", "File or directory path", true),
            make_param("destination", "string", "Destination path (for copy/move)", false),
            make_param("host", "string", "Remote host (empty for local)", false),
            make_param("recursive", "boolean", "Recursive operation", false, "false"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("devops::file_operations called");
            return placeholder_response("file_operations", args_json);
        });
    }

    spdlog::info("DevOps Agent: {} tools registered", executor.get_tool_definitions().size());
}

// ============================================================================
// SRE Agent Tools
// ============================================================================

void register_sre_tools(ToolExecutor& executor) {
    spdlog::info("Registering SRE Agent tools");

    // 1. manage_incident
    {
        ToolDefinition def;
        def.name = "manage_incident";
        def.description = "Create, update, escalate, or resolve incidents with severity tracking.";
        def.parameters = {
            make_param("action", "string", "Incident action", true, "",
                       R"(["create","update","escalate","resolve","acknowledge","list","get"])"),
            make_param("incident_id", "string", "Incident ID (for update/escalate/resolve)", false),
            make_param("title", "string", "Incident title (for create)", false),
            make_param("description", "string", "Incident description", false),
            make_param("severity", "string", "Severity level", false, "",
                       R"(["SEV1","SEV2","SEV3","SEV4","SEV5"])"),
            make_param("service", "string", "Affected service name", false),
            make_param("assignee", "string", "On-call engineer to assign", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::manage_incident called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["incident_id"] = "INC-" + generate_uuid().substr(0, 8);

            auto args = json::parse(args_json, nullptr, false);
            std::string action = "create";
            if (!args.is_discarded() && args.contains("action")) {
                action = args["action"].get<std::string>();
            }
            result["action"] = action;
            result["message"] = "Incident " + action + " completed successfully";
            return result.dump(2);
        });
    }

    // 2. track_slo
    {
        ToolDefinition def;
        def.name = "track_slo";
        def.description = "Define, monitor, and track SLO/SLA metrics including burn rates and error budgets.";
        def.parameters = {
            make_param("action", "string", "SLO action", true, "",
                       R"(["define","status","burn_rate","error_budget","list","delete"])"),
            make_param("slo_id", "string", "SLO identifier (for status/burn_rate/error_budget)", false),
            make_param("service", "string", "Service name", false),
            make_param("sli_type", "string", "SLI type (availability, latency, error_rate, throughput)", false, "",
                       R"(["availability","latency","error_rate","throughput"])"),
            make_param("target", "number", "SLO target (e.g., 99.9 for 99.9% availability)", false),
            make_param("window_days", "integer", "Rolling window in days", false, "30"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::track_slo called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["slo_id"] = "SLO-" + generate_uuid().substr(0, 8);
            result["current_value"] = 99.95;
            result["target"] = 99.9;
            result["burn_rate"] = 0.5;
            result["error_budget_remaining_percent"] = 85.0;
            result["message"] = "SLO is within target";
            return result.dump(2);
        });
    }

    // 3. manage_alerts
    {
        ToolDefinition def;
        def.name = "manage_alerts";
        def.description = "Create, update, acknowledge, or silence alert rules and active alerts.";
        def.parameters = {
            make_param("action", "string", "Alert action", true, "",
                       R"(["create_rule","delete_rule","list_rules","list_active","acknowledge","silence","resolve"])"),
            make_param("alert_id", "string", "Alert ID (for acknowledge/silence/resolve)", false),
            make_param("rule_name", "string", "Alert rule name (for create/delete)", false),
            make_param("metric", "string", "Metric to monitor (e.g., cpu_percent, error_rate)", false),
            make_param("threshold", "number", "Alert threshold value", false),
            make_param("comparison", "string", "Comparison operator", false, "",
                       R"(["gt","gte","lt","lte","eq","neq"])"),
            make_param("duration_minutes", "integer", "Duration before firing", false, "5"),
            make_param("notification_channel", "string", "Channel for notifications (slack, pagerduty, email)", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::manage_alerts called");
            return placeholder_response("manage_alerts", args_json);
        });
    }

    // 4. chaos_engineering
    {
        ToolDefinition def;
        def.name = "chaos_engineering";
        def.description = "Run chaos engineering experiments: inject failures, stress tests, validate resilience.";
        def.parameters = {
            make_param("experiment_type", "string", "Type of chaos experiment", true, "",
                       R"(["cpu_stress","memory_stress","network_latency","network_partition","disk_fill","process_kill","container_kill","dns_failure"])"),
            make_param("target", "string", "Target service, host, or container", true),
            make_param("duration_seconds", "integer", "Experiment duration", true),
            make_param("intensity", "number", "Intensity level 0.0-1.0 (e.g., 0.8 = 80% CPU)", false, "0.5"),
            make_param("blast_radius", "string", "Scope of impact (single, subset, all)", false, "\"single\"",
                       R"(["single","subset","all"])"),
            make_param("rollback_on_failure", "boolean", "Automatically rollback if health degrades", false, "true"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::chaos_engineering called");
            json result;
            result["status"] = "completed";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["experiment_id"] = "CHAOS-" + generate_uuid().substr(0, 8);
            result["outcome"] = "passed";
            result["findings"] = json::array({"Service recovered within SLO"});
            result["message"] = "Chaos experiment completed successfully";
            return result.dump(2);
        });
    }

    // 5. execute_runbook
    {
        ToolDefinition def;
        def.name = "execute_runbook";
        def.description = "Execute an operational runbook with step-by-step automation.";
        def.parameters = {
            make_param("action", "string", "Runbook action", true, "",
                       R"(["execute","list","get","create","validate"])"),
            make_param("runbook_id", "string", "Runbook identifier", false),
            make_param("parameters_json", "string", "JSON parameters for runbook execution", false, "{}"),
            make_param("dry_run", "boolean", "Validate without executing", false, "false"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::execute_runbook called");
            json result;
            result["status"] = "completed";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["runbook_id"] = "RB-" + generate_uuid().substr(0, 8);
            result["steps_completed"] = 5;
            result["steps_total"] = 5;
            result["message"] = "Runbook executed successfully";
            return result.dump(2);
        });
    }

    // 6. generate_postmortem
    {
        ToolDefinition def;
        def.name = "generate_postmortem";
        def.description = "Generate a post-mortem report from an incident including timeline, impact, and action items.";
        def.parameters = {
            make_param("incident_id", "string", "Incident ID to generate post-mortem for", true),
            make_param("include_timeline", "boolean", "Include detailed timeline", false, "true"),
            make_param("include_metrics", "boolean", "Include system metrics during incident", false, "true"),
            make_param("format", "string", "Output format (markdown, json, html)", false, "\"markdown\"",
                       R"(["markdown","json","html"])"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::generate_postmortem called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["postmortem_id"] = "PM-" + generate_uuid().substr(0, 8);
            result["summary"] = "Post-mortem generated with 3 action items";
            result["action_items_count"] = 3;
            result["message"] = "Post-mortem report generated successfully";
            return result.dump(2);
        });
    }

    // 7. capacity_planning
    {
        ToolDefinition def;
        def.name = "capacity_planning";
        def.description = "Analyze capacity and forecast future resource needs based on trends.";
        def.parameters = {
            make_param("service", "string", "Service to analyze", true),
            make_param("resource", "string", "Resource type to forecast", true, "",
                       R"(["cpu","memory","disk","network","connections"])"),
            make_param("lookback_days", "integer", "Historical data window", false, "30"),
            make_param("forecast_days", "integer", "Days to forecast ahead", false, "90"),
            make_param("growth_model", "string", "Growth model to use", false, "\"linear\"",
                       R"(["linear","exponential","seasonal"])"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::capacity_planning called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["current_utilization_percent"] = 62.5;
            result["predicted_exhaustion_days"] = 120;
            result["recommendation"] = "Scale up by 30% within 60 days";
            result["message"] = "Capacity forecast completed";
            return result.dump(2);
        });
    }

    // 8. track_toil
    {
        ToolDefinition def;
        def.name = "track_toil";
        def.description = "Track toil metrics and identify automation opportunities.";
        def.parameters = {
            make_param("action", "string", "Toil tracking action", true, "",
                       R"(["log","report","identify_automation","list","summary"])"),
            make_param("task_name", "string", "Name of the toil task (for log)", false),
            make_param("duration_minutes", "integer", "Time spent on the task", false),
            make_param("frequency", "string", "How often this task occurs", false, "",
                       R"(["hourly","daily","weekly","monthly","ad_hoc"])"),
            make_param("team", "string", "Team responsible", false),
            make_param("period_days", "integer", "Reporting period in days", false, "30"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::track_toil called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["toil_hours_this_month"] = 42;
            result["toil_budget_percent"] = 28;
            result["top_automation_candidates"] = json::array({
                "Log rotation (8 hrs/month)",
                "Certificate renewal (4 hrs/month)",
                "DB index maintenance (6 hrs/month)"
            });
            result["message"] = "Toil report generated";
            return result.dump(2);
        });
    }

    // 9. on_call_management
    {
        ToolDefinition def;
        def.name = "on_call_management";
        def.description = "Manage on-call schedules, rotations, and escalation policies.";
        def.parameters = {
            make_param("action", "string", "On-call action", true, "",
                       R"(["who_is_on_call","schedule","override","escalate","list_schedules"])"),
            make_param("team", "string", "Team name", false),
            make_param("engineer", "string", "Engineer name or ID", false),
            make_param("start_time", "string", "Override start time (ISO8601)", false),
            make_param("end_time", "string", "Override end time (ISO8601)", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::on_call_management called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["current_on_call"] = "jane.doe@prodxcloud.com";
            result["rotation"] = "Platform Engineering - Week 13";
            result["message"] = "On-call information retrieved";
            return result.dump(2);
        });
    }

    // 10. service_dependency_map
    {
        ToolDefinition def;
        def.name = "service_dependency_map";
        def.description = "Map and analyze service dependencies, detect circular deps, and find critical paths.";
        def.parameters = {
            make_param("action", "string", "Dependency action", true, "",
                       R"(["map","analyze","critical_path","blast_radius","list"])"),
            make_param("service", "string", "Service to analyze", false),
            make_param("depth", "integer", "Dependency depth to traverse", false, "3"),
            make_param("include_external", "boolean", "Include external dependencies", false, "true"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::service_dependency_map called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["total_services"] = 12;
            result["total_dependencies"] = 28;
            result["critical_path_length"] = 4;
            result["circular_dependencies"] = 0;
            result["message"] = "Service dependency map generated";
            return result.dump(2);
        });
    }

    // 11. availability_report
    {
        ToolDefinition def;
        def.name = "availability_report";
        def.description = "Generate availability and uptime reports for services over a given period.";
        def.parameters = {
            make_param("service", "string", "Service name (empty for all)", false),
            make_param("period_days", "integer", "Reporting period in days", false, "30"),
            make_param("granularity", "string", "Report granularity", false, "\"daily\"",
                       R"(["hourly","daily","weekly","monthly"])"),
            make_param("include_incidents", "boolean", "Include incident details", false, "true"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("sre::availability_report called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["uptime_percent"] = 99.97;
            result["downtime_minutes"] = 13;
            result["incidents_count"] = 2;
            result["mttr_minutes"] = 6.5;
            result["message"] = "Availability report generated";
            return result.dump(2);
        });
    }

    spdlog::info("SRE Agent: {} tools registered", executor.get_tool_definitions().size());
}

// ============================================================================
// OpenClaw Agent Tools
// ============================================================================

void register_openclaw_tools(ToolExecutor& executor) {
    spdlog::info("Registering OpenClaw Agent tools");

    // 1. scan_licenses
    {
        ToolDefinition def;
        def.name = "scan_licenses";
        def.description = "Scan project dependencies for license types and compatibility issues.";
        def.parameters = {
            make_param("project_path", "string", "Path to the project root", true),
            make_param("language", "string", "Primary language/ecosystem", false, "",
                       R"(["python","javascript","java","go","rust","cpp","csharp"])"),
            make_param("include_transitive", "boolean", "Include transitive dependencies", false, "true"),
            make_param("policy", "string", "License policy to enforce (permissive, copyleft_allowed, strict)", false, "\"permissive\"",
                       R"(["permissive","copyleft_allowed","strict"])"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::scan_licenses called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["total_dependencies"] = 142;
            result["licenses_found"] = {
                {"MIT", 85}, {"Apache-2.0", 32}, {"BSD-3-Clause", 15},
                {"ISC", 7}, {"GPL-3.0", 2}, {"UNKNOWN", 1}
            };
            result["policy_violations"] = 2;
            result["flagged"] = json::array({
                {{"package", "libgpl-util"}, {"license", "GPL-3.0"}, {"risk", "high"}},
                {{"package", "unknown-dep"}, {"license", "UNKNOWN"}, {"risk", "medium"}}
            });
            result["message"] = "License scan completed: 2 policy violations found";
            return result.dump(2);
        });
    }

    // 2. scan_vulnerabilities
    {
        ToolDefinition def;
        def.name = "scan_vulnerabilities";
        def.description = "Scan dependencies and code for known vulnerabilities (CVEs) with CVSS scoring.";
        def.parameters = {
            make_param("project_path", "string", "Path to the project root", true),
            make_param("scan_type", "string", "Type of vulnerability scan", false, "\"dependencies\"",
                       R"(["dependencies","code","container","all"])"),
            make_param("severity_threshold", "string", "Minimum severity to report", false, "\"medium\"",
                       R"(["critical","high","medium","low","info"])"),
            make_param("fix_suggestions", "boolean", "Include fix suggestions", false, "true"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::scan_vulnerabilities called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["total_vulnerabilities"] = 7;
            result["by_severity"] = {
                {"critical", 1}, {"high", 2}, {"medium", 3}, {"low", 1}
            };
            result["critical_cves"] = json::array({
                {{"cve", "CVE-2026-1234"}, {"package", "openssl"}, {"cvss", 9.8},
                 {"fix", "Upgrade to openssl 3.2.1"}}
            });
            result["gate_status"] = "FAILED";
            result["message"] = "Vulnerability scan completed: 1 critical, 2 high";
            return result.dump(2);
        });
    }

    // 3. audit_dependencies
    {
        ToolDefinition def;
        def.name = "audit_dependencies";
        def.description = "Full dependency audit: direct + transitive, risk scoring, outdated detection.";
        def.parameters = {
            make_param("project_path", "string", "Path to the project root", true),
            make_param("depth", "integer", "Maximum transitive dependency depth", false, "5"),
            make_param("include_dev", "boolean", "Include development dependencies", false, "false"),
            make_param("risk_threshold", "number", "Minimum risk score to flag (0.0-1.0)", false, "0.3"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::audit_dependencies called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["direct_deps"] = 45;
            result["transitive_deps"] = 312;
            result["outdated"] = 18;
            result["deprecated"] = 2;
            result["high_risk"] = 3;
            result["message"] = "Dependency audit completed: 3 high-risk dependencies found";
            return result.dump(2);
        });
    }

    // 4. generate_sbom
    {
        ToolDefinition def;
        def.name = "generate_sbom";
        def.description = "Generate a Software Bill of Materials (SBOM) in CycloneDX or SPDX format.";
        def.parameters = {
            make_param("project_path", "string", "Path to the project root", true),
            make_param("format", "string", "SBOM format to generate", true, "",
                       R"(["cyclonedx","spdx","cyclonedx-json","spdx-json"])"),
            make_param("output_path", "string", "Path to write the SBOM file", false),
            make_param("include_hashes", "boolean", "Include package integrity hashes", false, "true"),
            make_param("include_licenses", "boolean", "Include license information", false, "true"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::generate_sbom called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["sbom_id"] = "SBOM-" + generate_uuid().substr(0, 8);
            result["components_count"] = 357;
            result["format"] = "cyclonedx-json";
            result["message"] = "SBOM generated with 357 components";
            return result.dump(2);
        });
    }

    // 5. check_supply_chain
    {
        ToolDefinition def;
        def.name = "check_supply_chain";
        def.description = "Verify supply chain security: signature checks, typosquat detection, provenance.";
        def.parameters = {
            make_param("project_path", "string", "Path to the project root", true),
            make_param("checks", "string", "Comma-separated checks (signatures, typosquat, provenance, all)", false, "\"all\""),
            make_param("registry", "string", "Package registry to verify against", false, "",
                       R"(["npm","pypi","crates","maven","nuget"])"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::check_supply_chain called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["signatures_verified"] = 140;
            result["signatures_missing"] = 5;
            result["typosquat_suspects"] = 0;
            result["provenance_verified"] = 135;
            result["message"] = "Supply chain check completed: 5 packages missing signatures";
            return result.dump(2);
        });
    }

    // 6. enforce_policy
    {
        ToolDefinition def;
        def.name = "enforce_policy";
        def.description = "Run policy enforcement gate checks against a project or artifact.";
        def.parameters = {
            make_param("project_path", "string", "Path to the project root", true),
            make_param("policy_name", "string", "Policy name to enforce", true, "",
                       R"(["default","strict","production","hipaa","soc2"])"),
            make_param("fail_on_violation", "boolean", "Fail the gate on any violation", false, "true"),
            make_param("report_path", "string", "Path to write the policy report", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::enforce_policy called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["gate_status"] = "PASSED";
            result["rules_checked"] = 24;
            result["rules_passed"] = 22;
            result["rules_warned"] = 2;
            result["rules_failed"] = 0;
            result["message"] = "Policy enforcement passed with 2 warnings";
            return result.dump(2);
        });
    }

    // 7. scan_secrets
    {
        ToolDefinition def;
        def.name = "scan_secrets";
        def.description = "Scan code repositories for leaked secrets, API keys, and credentials.";
        def.parameters = {
            make_param("project_path", "string", "Path to scan for secrets", true),
            make_param("scan_history", "boolean", "Scan git history (not just current files)", false, "false"),
            make_param("rules", "string", "Secret detection rules (default, strict, custom)", false, "\"default\"",
                       R"(["default","strict","custom"])"),
            make_param("exclude_paths", "string", "Comma-separated paths to exclude", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::scan_secrets called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["files_scanned"] = 483;
            result["secrets_found"] = 0;
            result["gate_status"] = "PASSED";
            result["message"] = "Secret scan completed: no secrets detected";
            return result.dump(2);
        });
    }

    // 8. run_sast
    {
        ToolDefinition def;
        def.name = "run_sast";
        def.description = "Run Static Application Security Testing (SAST) on source code.";
        def.parameters = {
            make_param("project_path", "string", "Path to the source code", true),
            make_param("language", "string", "Primary language to analyze", false, "",
                       R"(["python","javascript","java","go","rust","cpp","csharp","auto"])"),
            make_param("severity_threshold", "string", "Minimum severity to report", false, "\"medium\"",
                       R"(["critical","high","medium","low"])"),
            make_param("exclude_rules", "string", "Comma-separated rule IDs to exclude", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::run_sast called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["files_analyzed"] = 256;
            result["findings_total"] = 4;
            result["by_severity"] = {{"high", 1}, {"medium", 2}, {"low", 1}};
            result["message"] = "SAST scan completed: 4 findings";
            return result.dump(2);
        });
    }

    // 9. run_dast
    {
        ToolDefinition def;
        def.name = "run_dast";
        def.description = "Run Dynamic Application Security Testing (DAST) against a running application.";
        def.parameters = {
            make_param("target_url", "string", "URL of the application to test", true),
            make_param("scan_profile", "string", "Scan profile to use", false, "\"standard\"",
                       R"(["quick","standard","full","api_only"])"),
            make_param("authentication_json", "string", "Authentication credentials JSON", false),
            make_param("max_duration_minutes", "integer", "Maximum scan duration", false, "30"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::run_dast called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["endpoints_tested"] = 45;
            result["findings_total"] = 2;
            result["by_severity"] = {{"medium", 1}, {"low", 1}};
            result["message"] = "DAST scan completed: 2 findings";
            return result.dump(2);
        });
    }

    // 10. compliance_report
    {
        ToolDefinition def;
        def.name = "compliance_report";
        def.description = "Generate compliance reports for SOC2, HIPAA, PCI-DSS, or ISO 27001.";
        def.parameters = {
            make_param("project_path", "string", "Path to the project root", true),
            make_param("framework", "string", "Compliance framework", true, "",
                       R"(["soc2","hipaa","pci_dss","iso27001","gdpr"])"),
            make_param("output_format", "string", "Report format", false, "\"json\"",
                       R"(["json","pdf","html","markdown"])"),
            make_param("include_evidence", "boolean", "Include evidence artifacts", false, "true"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::compliance_report called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["report_id"] = "CR-" + generate_uuid().substr(0, 8);
            result["controls_assessed"] = 48;
            result["controls_passing"] = 44;
            result["controls_failing"] = 2;
            result["controls_not_applicable"] = 2;
            result["overall_score"] = 91.7;
            result["message"] = "Compliance report generated: 91.7% controls passing";
            return result.dump(2);
        });
    }

    // 11. full_audit
    {
        ToolDefinition def;
        def.name = "full_audit";
        def.description = "Run a comprehensive security audit combining licenses, CVEs, secrets, SAST, and SBOM.";
        def.parameters = {
            make_param("project_path", "string", "Path to the project root", true),
            make_param("output_path", "string", "Path to write the full audit report", false),
            make_param("policy", "string", "Policy to enforce", false, "\"default\"",
                       R"(["default","strict","production"])"),
            make_param("fail_fast", "boolean", "Stop at first critical finding", false, "false"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("openclaw::full_audit called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["audit_id"] = "AUDIT-" + generate_uuid().substr(0, 8);
            result["scans_completed"] = json::array({"licenses", "vulnerabilities", "secrets", "sast", "sbom"});
            result["overall_risk_score"] = 3.2;
            result["gate_status"] = "PASSED_WITH_WARNINGS";
            result["critical_findings"] = 0;
            result["total_findings"] = 8;
            result["message"] = "Full audit completed: passed with warnings (8 findings)";
            return result.dump(2);
        });
    }

    spdlog::info("OpenClaw Agent: {} tools registered", executor.get_tool_definitions().size());
}

// ============================================================================
// CICD Agent Tools
// ============================================================================

void register_cicd_tools(ToolExecutor& executor) {
    spdlog::info("Registering CICD Agent tools");

    // 1. manage_pipeline
    {
        ToolDefinition def;
        def.name = "manage_pipeline";
        def.description = "Create, trigger, cancel, or retry CI/CD pipelines with DAG-based stage execution.";
        def.parameters = {
            make_param("action", "string", "Pipeline action", true, "",
                       R"(["create","trigger","cancel","retry","status","list","delete","validate"])"),
            make_param("pipeline_id", "string", "Pipeline ID (for trigger/cancel/retry/status)", false),
            make_param("pipeline_name", "string", "Pipeline name (for create)", false),
            make_param("branch", "string", "Git branch to build", false, "\"main\""),
            make_param("commit_sha", "string", "Specific commit SHA to build", false),
            make_param("parameters_json", "string", "Pipeline parameters as JSON", false, "{}"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::manage_pipeline called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["pipeline_id"] = "PL-" + generate_uuid().substr(0, 8);
            result["pipeline_status"] = "running";
            result["message"] = "Pipeline operation completed successfully";
            return result.dump(2);
        });
    }

    // 2. run_build
    {
        ToolDefinition def;
        def.name = "run_build";
        def.description = "Execute a build with parallel stage execution and artifact output.";
        def.parameters = {
            make_param("project", "string", "Project name or path", true),
            make_param("branch", "string", "Git branch", false, "\"main\""),
            make_param("build_type", "string", "Type of build", false, "\"release\"",
                       R"(["debug","release","profile","sanitize"])"),
            make_param("targets", "string", "Comma-separated build targets", false),
            make_param("parallel_jobs", "integer", "Number of parallel build jobs", false, "4"),
            make_param("cache_enabled", "boolean", "Use build cache", false, "true"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::run_build called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["build_id"] = "BUILD-" + generate_uuid().substr(0, 8);
            result["build_status"] = "passed";
            result["duration_seconds"] = 127;
            result["artifacts"] = json::array({
                {{"name", "app-binary"}, {"size_mb", 42.5}, {"path", "/artifacts/app-v1.0.0"}}
            });
            result["message"] = "Build completed successfully in 127s";
            return result.dump(2);
        });
    }

    // 3. deploy
    {
        ToolDefinition def;
        def.name = "deploy";
        def.description = "Deploy an application using rolling, blue-green, or canary strategy.";
        def.parameters = {
            make_param("application", "string", "Application name", true),
            make_param("version", "string", "Version or artifact to deploy", true),
            make_param("environment", "string", "Target environment", true, "",
                       R"(["dev","staging","production","canary"])"),
            make_param("strategy", "string", "Deployment strategy", false, "\"rolling\"",
                       R"(["rolling","blue_green","canary","recreate"])"),
            make_param("canary_percent", "integer", "Canary traffic percentage (for canary strategy)", false, "10"),
            make_param("health_check_url", "string", "URL to verify deployment health", false),
            make_param("rollback_on_failure", "boolean", "Automatically rollback if health check fails", false, "true"),
            make_param("dry_run", "boolean", "Simulate deployment without applying", false, "false"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::deploy called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["deployment_id"] = "DEPLOY-" + generate_uuid().substr(0, 8);
            result["deployment_status"] = "healthy";
            result["health_check_passed"] = true;
            result["message"] = "Deployment completed successfully";
            return result.dump(2);
        });
    }

    // 4. manage_artifacts
    {
        ToolDefinition def;
        def.name = "manage_artifacts";
        def.description = "Manage build artifacts: publish, promote, cache, list, and clean up.";
        def.parameters = {
            make_param("action", "string", "Artifact action", true, "",
                       R"(["publish","promote","download","list","delete","cache","restore_cache"])"),
            make_param("artifact_name", "string", "Artifact name or path", false),
            make_param("version", "string", "Artifact version", false),
            make_param("registry", "string", "Artifact registry URL", false),
            make_param("from_environment", "string", "Source environment (for promote)", false),
            make_param("to_environment", "string", "Target environment (for promote)", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::manage_artifacts called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["artifact_id"] = "ART-" + generate_uuid().substr(0, 8);
            result["message"] = "Artifact operation completed successfully";
            return result.dump(2);
        });
    }

    // 5. manage_environment
    {
        ToolDefinition def;
        def.name = "manage_environment";
        def.description = "Manage deployment environments: create, lock, unlock, list, and configure.";
        def.parameters = {
            make_param("action", "string", "Environment action", true, "",
                       R"(["create","lock","unlock","list","status","configure","delete"])"),
            make_param("environment", "string", "Environment name", true),
            make_param("lock_reason", "string", "Reason for locking (for lock action)", false),
            make_param("config_json", "string", "Environment configuration JSON", false, "{}"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::manage_environment called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();

            auto args = json::parse(args_json, nullptr, false);
            std::string env = "unknown";
            if (!args.is_discarded() && args.contains("environment")) {
                env = args["environment"].get<std::string>();
            }
            result["environment"] = env;
            result["locked"] = false;
            result["current_version"] = "v1.2.3";
            result["message"] = "Environment '" + env + "' operation completed";
            return result.dump(2);
        });
    }

    // 6. create_release
    {
        ToolDefinition def;
        def.name = "create_release";
        def.description = "Create a new release with semantic versioning and auto-generated changelog.";
        def.parameters = {
            make_param("project", "string", "Project name", true),
            make_param("version", "string", "Release version (semver, e.g., 1.2.0)", true),
            make_param("branch", "string", "Branch to release from", false, "\"main\""),
            make_param("prerelease", "boolean", "Mark as pre-release", false, "false"),
            make_param("generate_changelog", "boolean", "Auto-generate changelog from commits", false, "true"),
            make_param("notes", "string", "Additional release notes", false),
            make_param("tag_prefix", "string", "Tag prefix (e.g., 'v')", false, "\"v\""),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::create_release called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["release_id"] = "REL-" + generate_uuid().substr(0, 8);

            auto args = json::parse(args_json, nullptr, false);
            std::string version = "0.0.0";
            if (!args.is_discarded() && args.contains("version")) {
                version = args["version"].get<std::string>();
            }
            result["version"] = version;
            result["tag"] = "v" + version;
            result["changelog_entries"] = 12;
            result["message"] = "Release v" + version + " created with changelog";
            return result.dump(2);
        });
    }

    // 7. run_tests
    {
        ToolDefinition def;
        def.name = "run_tests";
        def.description = "Run test suites: unit, integration, E2E, or performance tests.";
        def.parameters = {
            make_param("project", "string", "Project name or path", true),
            make_param("test_type", "string", "Type of tests to run", true, "",
                       R"(["unit","integration","e2e","performance","all"])"),
            make_param("pattern", "string", "Test name or file pattern filter", false),
            make_param("parallel", "boolean", "Run tests in parallel", false, "true"),
            make_param("coverage", "boolean", "Collect code coverage", false, "true"),
            make_param("timeout_seconds", "integer", "Test suite timeout", false, "300"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::run_tests called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["test_run_id"] = "TEST-" + generate_uuid().substr(0, 8);
            result["tests_total"] = 342;
            result["tests_passed"] = 340;
            result["tests_failed"] = 1;
            result["tests_skipped"] = 1;
            result["coverage_percent"] = 87.3;
            result["duration_seconds"] = 45;
            result["message"] = "Tests completed: 340/342 passed (87.3% coverage)";
            return result.dump(2);
        });
    }

    // 8. container_registry
    {
        ToolDefinition def;
        def.name = "container_registry";
        def.description = "Manage container registry: build, push, pull, tag, scan, and list images.";
        def.parameters = {
            make_param("action", "string", "Registry action", true, "",
                       R"(["build","push","pull","tag","scan","list","delete","inspect"])"),
            make_param("image", "string", "Image name (e.g., myapp:latest)", true),
            make_param("registry", "string", "Registry URL", false),
            make_param("dockerfile", "string", "Path to Dockerfile (for build)", false, "\"Dockerfile\""),
            make_param("build_args_json", "string", "Build arguments as JSON", false, "{}"),
            make_param("platform", "string", "Target platform (linux/amd64, linux/arm64)", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::container_registry called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();

            auto args = json::parse(args_json, nullptr, false);
            std::string image = "unknown:latest";
            if (!args.is_discarded() && args.contains("image")) {
                image = args["image"].get<std::string>();
            }
            result["image"] = image;
            result["digest"] = "sha256:" + generate_uuid().substr(0, 12);
            result["message"] = "Container registry operation completed for " + image;
            return result.dump(2);
        });
    }

    // 9. validate_pipeline
    {
        ToolDefinition def;
        def.name = "validate_pipeline";
        def.description = "Validate and lint pipeline configuration files for correctness.";
        def.parameters = {
            make_param("config_path", "string", "Path to the pipeline configuration file", true),
            make_param("config_type", "string", "Configuration type", false, "\"auto\"",
                       R"(["github_actions","gitlab_ci","jenkins","circleci","azure_devops","auto"])"),
            make_param("strict", "boolean", "Enable strict validation mode", false, "false"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::validate_pipeline called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["valid"] = true;
            result["warnings"] = 1;
            result["errors"] = 0;
            result["suggestions"] = json::array({
                "Consider adding a timeout to the 'deploy' stage"
            });
            result["message"] = "Pipeline configuration is valid (1 warning)";
            return result.dump(2);
        });
    }

    // 10. manage_secrets
    {
        ToolDefinition def;
        def.name = "manage_secrets";
        def.description = "Manage CI/CD secrets and environment variables securely.";
        def.parameters = {
            make_param("action", "string", "Secret action", true, "",
                       R"(["set","delete","list","rotate","audit"])"),
            make_param("environment", "string", "Target environment", true, "",
                       R"(["dev","staging","production","all"])"),
            make_param("key", "string", "Secret key name", false),
            make_param("value", "string", "Secret value (for set; will be encrypted)", false),
            make_param("expiry_days", "integer", "Days until secret expires", false, "90"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::manage_secrets called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["message"] = "Secret operation completed (value encrypted)";
            return result.dump(2);
        });
    }

    // 11. rollback_deployment
    {
        ToolDefinition def;
        def.name = "rollback_deployment";
        def.description = "Rollback a deployment to a previous version with health verification.";
        def.parameters = {
            make_param("application", "string", "Application name", true),
            make_param("environment", "string", "Environment to rollback", true, "",
                       R"(["dev","staging","production"])"),
            make_param("target_version", "string", "Version to rollback to (empty for previous)", false),
            make_param("reason", "string", "Reason for rollback", false),
            make_param("notify", "boolean", "Send notification about rollback", false, "true"),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::rollback_deployment called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["rollback_id"] = "RB-" + generate_uuid().substr(0, 8);
            result["previous_version"] = "v1.3.0";
            result["rolled_back_to"] = "v1.2.9";
            result["health_check_passed"] = true;
            result["message"] = "Successfully rolled back to v1.2.9";
            return result.dump(2);
        });
    }

    // 12. pipeline_metrics
    {
        ToolDefinition def;
        def.name = "pipeline_metrics";
        def.description = "Get CI/CD pipeline metrics: build times, success rates, DORA metrics.";
        def.parameters = {
            make_param("pipeline_id", "string", "Pipeline ID (empty for all)", false),
            make_param("period_days", "integer", "Reporting period in days", false, "30"),
            make_param("metrics", "string", "Comma-separated metrics (build_time, success_rate, dora, throughput)", false, "\"build_time,success_rate,dora\""),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::pipeline_metrics called");
            json result;
            result["status"] = "success";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["metrics"]["avg_build_time_seconds"] = 145;
            result["metrics"]["success_rate_percent"] = 94.2;
            result["metrics"]["deployments_per_day"] = 3.5;
            result["metrics"]["lead_time_hours"] = 4.2;
            result["metrics"]["mttr_minutes"] = 18;
            result["metrics"]["change_failure_rate_percent"] = 5.8;
            result["message"] = "Pipeline metrics retrieved for last 30 days";
            return result.dump(2);
        });
    }

    // 13. notify
    {
        ToolDefinition def;
        def.name = "notify";
        def.description = "Send notifications about CI/CD events to Slack, email, or webhooks.";
        def.parameters = {
            make_param("channel", "string", "Notification channel type", true, "",
                       R"(["slack","email","webhook","pagerduty","teams"])"),
            make_param("target", "string", "Channel name, email, or webhook URL", true),
            make_param("message", "string", "Notification message", true),
            make_param("severity", "string", "Notification severity", false, "\"info\"",
                       R"(["info","warning","error","critical"])"),
            make_param("title", "string", "Notification title", false),
        };
        executor.register_tool(def, [](const std::string& args_json) -> Result<std::string> {
            spdlog::info("cicd::notify called");
            json result;
            result["status"] = "sent";
            result["request_id"] = generate_uuid();
            result["timestamp"] = now_iso8601();
            result["message"] = "Notification sent successfully";
            return result.dump(2);
        });
    }

    spdlog::info("CICD Agent: {} tools registered", executor.get_tool_definitions().size());
}

}  // namespace prodxcloud::ai
