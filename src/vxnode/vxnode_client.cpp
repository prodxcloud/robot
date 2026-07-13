#include "vxnode/vxnode_client.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "vxnode/http_client.hpp"

using json = nlohmann::json;

namespace prodxcloud::vxnode {

namespace {

std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

bool env_flag(const char* key) {
    const std::string v = env_or(key, "");
    return v == "1" || v == "true" || v == "yes";
}

/// Strip a trailing '/' so base_url + path never yields a double slash.
std::string normalise_base(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

}  // namespace

NodeConfig NodeConfig::from_env() {
    NodeConfig c;
    c.base_url  = normalise_base(env_or("VXNODE_URL", "http://127.0.0.1:8744"));
    c.api_key   = env_or("VXNODE_API_KEY", "");
    c.node_id   = env_or("VXNODE_ID", "default");
    c.tenant_id = env_or("VXNODE_TENANT", "");
    c.dry_run   = env_flag("VXNODE_DRY_RUN");

    if (const std::string t = env_or("VXNODE_TIMEOUT_MS", ""); !t.empty()) {
        try {
            c.timeout = std::chrono::milliseconds(std::stoi(t));
        } catch (const std::exception&) {
            spdlog::warn("VXNODE_TIMEOUT_MS is not a number, keeping the default");
        }
    }
    return c;
}

// ─── Payloads ───────────────────────────────────────────────────────────────

std::string ProvisionRequest::to_json() const {
    json j;
    j["provider"]      = provider;
    j["region"]        = region;
    j["instance_type"] = instance_type;
    j["image"]         = image;
    j["name"]          = name;
    j["count"]         = count;
    j["dry_run"]       = dry_run;
    if (!ssh_key_name.empty()) j["ssh_key_name"] = ssh_key_name;
    if (!purpose.empty()) j["purpose"] = purpose;
    // The node tags everything it creates with its requester, so a runaway robot
    // is traceable to the exact plan that asked for the instance.
    j["requested_by"] = "prodxcloud-robot";
    return j.dump();
}

std::string DeployRequest::to_json() const {
    json j;
    j["host"]     = host;
    j["repo_url"] = repo_url;
    j["branch"]   = branch;
    if (!domain.empty()) j["domain"] = domain;
    if (port > 0) j["port"] = port;

    auto env = json::parse(env_json, nullptr, false);
    j["env"] = env.is_discarded() ? json::object() : env;

    j["requested_by"] = "prodxcloud-robot";
    return j.dump();
}

std::string VmAction::to_json() const {
    json j;
    j["instance_id"] = instance_id;
    j["action"]      = action;
    return j.dump();
}

// ─── Client ─────────────────────────────────────────────────────────────────

VxNodeClient::VxNodeClient(NodeConfig config) : config_(std::move(config)) {
    config_.base_url = normalise_base(config_.base_url);
}

const std::vector<std::string>& VxNodeClient::supported_stacks() {
    // Mirrors the /api/v2/infrastructure/services/<stack>/deploy routes vxnode exposes.
    static const std::vector<std::string> kStacks = {
        "nodejs",  "python",  "fastapi", "django",  "flask",   "golang",
        "rust",    "java",    "springboot", "php",  "laravel", "nextjs",
        "reactjs", "angular", "streamlit",  "staticwebsite",   "cpp",
        "expo",
    };
    return kStacks;
}

Result<std::string> VxNodeClient::post(const std::string& path, const std::string& body) const {
    const std::string url = config_.base_url + path;

    if (config_.dry_run) {
        // Dry-run returns the exact request that *would* have gone to the node.
        // A plan you can read before it touches infrastructure is worth more than
        // one you can only audit afterwards.
        json j;
        j["dry_run"]  = true;
        j["method"]   = "POST";
        j["url"]      = url;
        j["node_id"]  = config_.node_id;
        j["payload"]  = json::parse(body, nullptr, false);
        spdlog::info("vxnode dry-run: POST {}", url);
        return j.dump(2);
    }

    HttpRequest req;
    req.method  = "POST";
    req.url     = url;
    req.body    = body;
    req.timeout = config_.timeout;
    req.headers["Content-Type"] = "application/json";
    if (!config_.api_key.empty()) req.headers["X-API-Key"] = config_.api_key;
    if (!config_.tenant_id.empty()) req.headers["X-Tenant-ID"] = config_.tenant_id;

    Error last{503, "vxnode unreachable", ""};

    for (int attempt = 1; attempt <= std::max(1, config_.max_retries); ++attempt) {
        const auto res = http_request(req);

        if (res) {
            if (res->ok()) return res->body;

            // 4xx is the node telling us the request is wrong. Retrying an
            // argument error just makes the same mistake more times.
            if (res->status >= 400 && res->status < 500) {
                return std::unexpected(Error{res->status,
                                             "vxnode rejected the request (HTTP " +
                                                 std::to_string(res->status) + ")",
                                             res->body});
            }
            last = Error{res->status,
                         "vxnode returned HTTP " + std::to_string(res->status),
                         res->body};
        } else {
            last = res.error();
        }

        if (attempt < config_.max_retries) {
            const auto backoff = std::chrono::milliseconds(200 * (1 << (attempt - 1)));
            spdlog::warn("vxnode POST {} failed ({}), retry {}/{} in {} ms",
                         path, last.message, attempt, config_.max_retries, backoff.count());
            std::this_thread::sleep_for(backoff);
        }
    }

    return std::unexpected(last);
}

Result<NodeHealth> VxNodeClient::health() const {
    HttpRequest req;
    req.method  = "GET";
    req.url     = config_.base_url + "/api/v2/health";
    req.timeout = config_.timeout;
    if (!config_.api_key.empty()) req.headers["X-API-Key"] = config_.api_key;

    const auto start = std::chrono::steady_clock::now();
    const auto res   = http_request(req);
    const auto ms    = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count();

    if (!res) return std::unexpected(res.error());

    NodeHealth h;
    h.reachable    = res->ok();
    h.raw_response = res->body;
    h.latency_ms   = ms;

    const auto j = json::parse(res->body, nullptr, false);
    if (!j.is_discarded() && j.is_object()) {
        h.status  = j.value("status", res->ok() ? "ok" : "error");
        h.version = j.value("version", "");
    } else {
        h.status = res->ok() ? "ok" : "error";
    }
    return h;
}

Result<ProvisionResult> VxNodeClient::provision_vm(const ProvisionRequest& req) const {
    if (req.provider.empty()) {
        return std::unexpected(Error::bad_request("provision: provider is required"));
    }
    if (req.count < 1 || req.count > 50) {
        return std::unexpected(Error::validation("provision: count must be in [1, 50]"));
    }

    spdlog::info("delegating provisioning to vxnode '{}': {} x {} on {}",
                 config_.node_id, req.count, req.instance_type, req.provider);

    ProvisionRequest r = req;
    r.dry_run = r.dry_run || config_.dry_run;

    const auto body = post("/api/v2/provision/vm", r.to_json());
    if (!body) return std::unexpected(body.error());

    ProvisionResult out;
    out.raw_response = *body;
    out.node_id      = config_.node_id;
    out.accepted     = true;

    const auto j = json::parse(*body, nullptr, false);
    if (!j.is_discarded() && j.is_object()) {
        out.request_id = j.value("request_id", j.value("id", ""));
        out.status     = j.value("status", "accepted");
        out.message    = j.value("message", "");

        // The node has returned instance ids under a few different keys across
        // versions; accept any of them rather than silently reporting none.
        for (const char* key : {"instance_ids", "instances", "ids"}) {
            if (!j.contains(key)) continue;
            const auto& node = j[key];
            if (!node.is_array()) continue;

            for (const auto& item : node) {
                if (item.is_string()) {
                    out.instance_ids.push_back(item.get<std::string>());
                } else if (item.is_object()) {
                    if (item.contains("id")) out.instance_ids.push_back(item["id"].get<std::string>());
                    else if (item.contains("instance_id"))
                        out.instance_ids.push_back(item["instance_id"].get<std::string>());
                }
            }
            break;
        }
    } else {
        out.status = "accepted";
    }

    return out;
}

Result<std::string> VxNodeClient::vm_status(const std::string& instance_id) const {
    if (instance_id.empty()) {
        return std::unexpected(Error::bad_request("vm_status: instance_id is required"));
    }
    json j;
    j["instance_id"] = instance_id;
    return post("/api/v2/provision/vm/status", j.dump());
}

Result<std::string> VxNodeClient::vm_action(const VmAction& action) const {
    static const std::vector<std::string> kActions = {"start", "stop", "restart", "status",
                                                      "terminate"};
    if (std::find(kActions.begin(), kActions.end(), action.action) == kActions.end()) {
        return std::unexpected(Error::validation(
            "vm_action: '" + action.action +
            "' is not one of start|stop|restart|status|terminate"));
    }
    if (action.instance_id.empty()) {
        return std::unexpected(Error::bad_request("vm_action: instance_id is required"));
    }

    spdlog::info("vxnode action '{}' on instance {}", action.action, action.instance_id);
    return post("/api/v2/provision/vm/action", action.to_json());
}

Result<std::string> VxNodeClient::deploy(const DeployRequest& req) const {
    const auto& stacks = supported_stacks();
    if (std::find(stacks.begin(), stacks.end(), req.stack) == stacks.end()) {
        return std::unexpected(Error::validation(
            "deploy: vxnode has no '" + req.stack + "' stack endpoint"));
    }
    if (req.host.empty()) {
        return std::unexpected(Error::bad_request("deploy: host is required"));
    }

    spdlog::info("delegating '{}' deploy to vxnode '{}' -> {}", req.stack, config_.node_id, req.host);
    return post("/api/v2/infrastructure/services/" + req.stack + "/deploy", req.to_json());
}

}  // namespace prodxcloud::vxnode
