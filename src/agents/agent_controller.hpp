#pragma once

/// @file agent_controller.hpp
/// @brief Manages agent lifecycle: spawn, terminate, query, quota enforcement.

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "agents/agent_base.hpp"
#include "agents/agent_registry.hpp"
#include "common/types.hpp"

namespace prodxcloud::agents {

struct AgentHandle {
    std::string agent_id;
    std::string tenant_id;
    std::shared_ptr<AgentBase> agent;
};

struct ControllerConfig {
    size_t thread_pool_size      = 16;
    size_t max_agents_per_tenant = 50;
};

class AgentController {
public:
    explicit AgentController(ControllerConfig config);
    ~AgentController();

    AgentController(const AgentController&)            = delete;
    AgentController& operator=(const AgentController&) = delete;

    Result<AgentHandle> spawn_agent(AgentConfig config, std::shared_ptr<AgentBase> agent);
    Result<void> terminate_agent(const std::string& agent_id);
    Result<AgentState> get_agent_state(const std::string& agent_id) const;
    std::vector<AgentMetadata> list_agents(const std::string& tenant_id) const;
    size_t agent_count(const std::string& tenant_id) const;

private:
    ControllerConfig config_;
    AgentRegistry registry_;
    mutable std::shared_mutex quota_mutex_;
    std::unordered_map<std::string, size_t> tenant_agent_counts_;

    void increment_quota(const std::string& tenant_id);
    void decrement_quota(const std::string& tenant_id);
};

}  // namespace prodxcloud::agents
