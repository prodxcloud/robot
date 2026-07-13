#pragma once

/// @file agent_registry.hpp
/// @brief Thread-safe agent registry scoped per tenant.

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "agents/agent_base.hpp"

namespace prodxcloud::agents {

class AgentRegistry {
public:
    AgentRegistry()  = default;
    ~AgentRegistry() = default;

    AgentRegistry(const AgentRegistry&)            = delete;
    AgentRegistry& operator=(const AgentRegistry&) = delete;

    void register_agent(const std::string& tenant_id, std::shared_ptr<AgentBase> agent);
    bool unregister_agent(const std::string& agent_id);
    std::shared_ptr<AgentBase> lookup_agent(const std::string& agent_id) const;
    std::vector<AgentMetadata> list_agents(const std::string& tenant_id) const;
    size_t size() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<AgentBase>> agents_;
    std::unordered_map<std::string, std::vector<std::string>> tenant_agents_;
};

}  // namespace prodxcloud::agents
