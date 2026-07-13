#pragma once

/// @file agent_factory.hpp
/// @brief Factory for creating specialized agents by type string.

#include <memory>
#include <string>

#include "agents/agent_base.hpp"
#include "agents/specialized/devops_agent.hpp"
#include "agents/specialized/sre_agent.hpp"
#include "agents/specialized/openclaw_agent.hpp"
#include "agents/specialized/cicd_agent.hpp"
#include "common/types.hpp"

namespace prodxcloud::agents::specialized {

class AgentFactory {
public:
    static Result<std::shared_ptr<AgentBase>> create(const std::string& agent_type,
                                                      AgentConfig config) {
        if (agent_type == "devops")
            return std::make_shared<DevOpsAgent>(std::move(config));
        if (agent_type == "sre")
            return std::make_shared<SREAgent>(std::move(config));
        if (agent_type == "openclaw")
            return std::make_shared<OpenClawAgent>(std::move(config));
        if (agent_type == "cicd")
            return std::make_shared<CICDAgent>(std::move(config));
        return std::unexpected(Error::bad_request("Unknown agent type: " + agent_type));
    }

    static std::vector<std::string> available_types() {
        return {"devops", "sre", "openclaw", "cicd"};
    }

    static std::string describe(const std::string& agent_type) {
        if (agent_type == "devops")
            return "DevOps Agent — SSH, Docker, systemd, logs, packages, deployment health";
        if (agent_type == "sre")
            return "SRE Agent — incidents, SLOs, alerting, chaos engineering, runbooks, post-mortems";
        if (agent_type == "openclaw")
            return "OpenClaw Agent — license scanning, vulnerability assessment, SBOM, supply-chain security";
        if (agent_type == "cicd")
            return "CICD Agent — pipelines, builds, artifacts, deployments, releases, testing";
        return "Unknown agent type";
    }
};

}  // namespace prodxcloud::agents::specialized
