#include "agents/agent_registry.hpp"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace prodxcloud::agents {

void AgentRegistry::register_agent(const std::string& tenant_id,
                                    std::shared_ptr<AgentBase> agent) {
    std::unique_lock lock(mutex_);
    auto aid = agent->id();
    agents_[aid] = agent;
    tenant_agents_[tenant_id].push_back(aid);
    spdlog::debug("Registered agent {} for tenant {}", aid, tenant_id);
}

bool AgentRegistry::unregister_agent(const std::string& agent_id) {
    std::unique_lock lock(mutex_);
    auto it = agents_.find(agent_id);
    if (it == agents_.end()) return false;
    auto tid = it->second->tenant_id();
    agents_.erase(it);
    auto& tl = tenant_agents_[tid];
    tl.erase(std::remove(tl.begin(), tl.end(), agent_id), tl.end());
    if (tl.empty()) tenant_agents_.erase(tid);
    return true;
}

std::shared_ptr<AgentBase> AgentRegistry::lookup_agent(const std::string& agent_id) const {
    std::shared_lock lock(mutex_);
    auto it = agents_.find(agent_id);
    return it != agents_.end() ? it->second : nullptr;
}

std::vector<AgentMetadata> AgentRegistry::list_agents(const std::string& tenant_id) const {
    std::shared_lock lock(mutex_);
    std::vector<AgentMetadata> result;
    auto it = tenant_agents_.find(tenant_id);
    if (it == tenant_agents_.end()) return result;
    for (const auto& aid : it->second) {
        auto ait = agents_.find(aid);
        if (ait != agents_.end()) result.push_back(ait->second->metadata());
    }
    return result;
}

size_t AgentRegistry::size() const {
    std::shared_lock lock(mutex_);
    return agents_.size();
}

}  // namespace prodxcloud::agents
