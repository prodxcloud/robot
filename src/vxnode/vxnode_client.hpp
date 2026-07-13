#pragma once

/// @file vxnode_client.hpp
/// @brief The robot's only route to infrastructure: the vxnode node API.
///
/// This repository provisions nothing. It has no cloud SDK, no Terraform, no
/// provider credentials and no idea what an availability zone is. When the robot
/// needs compute — a VM to run a perception workload, a deployed service, a node
/// to host a fleet peer — it asks its **vxnode** node, which owns the credentials,
/// the multi-cloud drivers and the blast radius.
///
/// That split is the whole point:
///
///   robot  ──(HTTP, API key)──▶  vxnode  ──▶  AWS / Azure / GCP / Linode / DO
///   plans, controls devices      provisions, deploys, holds the secrets
///
/// The robot therefore ships no secrets and cannot be turned into a cloud bill by
/// a bad plan: everything it can do to infrastructure is exactly what the node's
/// API permits, and every call is one auditable HTTP request.
///
/// Node API (vxnode, default `http://127.0.0.1:8744`):
///   POST /api/v2/provision/vm                              — provision an instance
///   POST /api/v2/provision/vm/status                       — instance status
///   POST /api/v2/provision/vm/action                       — start/stop/restart
///   POST /api/v2/infrastructure/services/<stack>/deploy    — deploy an app stack
///   GET  /api/v2/health                                    — node liveness

#include <chrono>
#include <string>
#include <vector>

#include "common/types.hpp"

namespace prodxcloud::vxnode {

/// How to reach the node. Resolved from the environment so that a robot binary is
/// identical across a laptop, a workcell controller and a container.
struct NodeConfig {
    /// Base URL of the node. vxnode listens on loopback inside the VM; nginx
    /// terminates TLS in front of it for external callers.
    std::string base_url = "http://127.0.0.1:8744";
    std::string api_key;                        ///< sent as X-API-Key
    std::string node_id  = "default";           ///< which node in the fleet
    std::string tenant_id;
    std::chrono::milliseconds timeout{30000};
    int  max_retries   = 3;
    bool dry_run       = false;  ///< plan the call and return it without sending

    /// Read VXNODE_URL, VXNODE_API_KEY, VXNODE_ID, VXNODE_TENANT, VXNODE_DRY_RUN.
    static NodeConfig from_env();
};

/// A request to provision an instance. Mirrors vxnode's `/api/v2/provision/vm`.
struct ProvisionRequest {
    std::string provider   = "aws";       ///< aws | azure | gcp | linode | digitalocean
    std::string region;
    std::string instance_type;
    std::string image;
    std::string name;
    int         count      = 1;
    std::string ssh_key_name;
    std::string purpose;                  ///< free-form: why the robot wants this
    bool        dry_run    = false;

    [[nodiscard]] std::string to_json() const;
};

struct ProvisionResult {
    bool                     accepted = false;
    std::string              request_id;
    std::vector<std::string> instance_ids;
    std::string              status;       ///< as reported by the node
    std::string              node_id;
    std::string              raw_response;
    std::string              message;
};

/// Deploy an application stack onto a node. vxnode exposes one endpoint per
/// stack: /api/v2/infrastructure/services/<stack>/deploy
struct DeployRequest {
    std::string stack;        ///< nodejs | python | fastapi | golang | cpp | ...
    std::string host;         ///< target VM
    std::string repo_url;
    std::string branch = "main";
    std::string domain;
    int         port   = 0;
    std::string env_json = "{}";

    [[nodiscard]] std::string to_json() const;
};

/// Lifecycle action on an existing instance.
struct VmAction {
    std::string instance_id;
    std::string action;  ///< start | stop | restart | status | terminate

    [[nodiscard]] std::string to_json() const;
};

struct NodeHealth {
    bool        reachable = false;
    std::string status;
    std::string version;
    std::string raw_response;
    double      latency_ms = 0.0;
};

/// Thin, typed client over the node API. Stateless and thread-safe.
class VxNodeClient {
public:
    explicit VxNodeClient(NodeConfig config = NodeConfig::from_env());

    [[nodiscard]] const NodeConfig& config() const { return config_; }

    /// GET /api/v2/health — is the node alive?
    [[nodiscard]] Result<NodeHealth> health() const;

    /// POST /api/v2/provision/vm
    [[nodiscard]] Result<ProvisionResult> provision_vm(const ProvisionRequest& req) const;

    /// POST /api/v2/provision/vm/status
    [[nodiscard]] Result<std::string> vm_status(const std::string& instance_id) const;

    /// POST /api/v2/provision/vm/action
    [[nodiscard]] Result<std::string> vm_action(const VmAction& action) const;

    /// POST /api/v2/infrastructure/services/<stack>/deploy
    [[nodiscard]] Result<std::string> deploy(const DeployRequest& req) const;

    /// The set of stacks vxnode can deploy. Used to reject a typo'd stack before
    /// it becomes a 404 from the node.
    [[nodiscard]] static const std::vector<std::string>& supported_stacks();

private:
    /// POST @p path with @p body, retrying idempotent failures with backoff.
    [[nodiscard]] Result<std::string> post(const std::string& path, const std::string& body) const;

    NodeConfig config_;
};

}  // namespace prodxcloud::vxnode
