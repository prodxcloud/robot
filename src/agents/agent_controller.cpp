#include "agents/agent_controller.hpp"
#include <spdlog/spdlog.h>

namespace prodxcloud::agents {

AgentController::AgentController(ControllerConfig config) : config_(std::move(config)) {
    spdlog::info("AgentController initialized: pool={}, max_per_tenant={}",
                 config_.thread_pool_size, config_.max_agents_per_tenant);
}
AgentController::~AgentController() { spdlog::info("AgentController shutting down"); }

Result<AgentHandle> AgentController::spawn_agent(AgentConfig config,
                                                  std::shared_ptr<AgentBase> agent) {
    const auto& tid = config.tenant_id;
    {
        std::shared_lock lock(quota_mutex_);
        auto it = tenant_agent_counts_.find(tid);
        if (it != tenant_agent_counts_.end() && it->second >= config_.max_agents_per_tenant)
            return std::unexpected(Error::rate_limited("Tenant " + tid + " exceeded agent quota"));
    }
    auto aid = agent->id();
    registry_.register_agent(tid, agent);
    increment_quota(tid);
    agent->on_state_change([](const std::string& id, AgentState from, AgentState to) {
        spdlog::info("Agent {} state: {} -> {}", id,
                     agent_state_to_string(from), agent_state_to_string(to));
    });
    spdlog::info("Spawned agent {} for tenant {}", aid, tid);
    return AgentHandle{.agent_id = aid, .tenant_id = tid, .agent = agent};
}

Result<void> AgentController::terminate_agent(const std::string& agent_id) {
    auto agent = registry_.lookup_agent(agent_id);
    if (!agent) return std::unexpected(Error::not_found("Agent not found: " + agent_id));
    agent->cancel();
    auto tid = agent->tenant_id();
    registry_.unregister_agent(agent_id);
    decrement_quota(tid);
    spdlog::info("Terminated agent {}", agent_id);
    return {};
}

Result<AgentState> AgentController::get_agent_state(const std::string& agent_id) const {
    auto agent = registry_.lookup_agent(agent_id);
    if (!agent) return std::unexpected(Error::not_found("Agent not found: " + agent_id));
    return agent->state();
}

std::vector<AgentMetadata> AgentController::list_agents(const std::string& tid) const {
    return registry_.list_agents(tid);
}

size_t AgentController::agent_count(const std::string& tid) const {
    std::shared_lock lock(quota_mutex_);
    auto it = tenant_agent_counts_.find(tid);
    return it != tenant_agent_counts_.end() ? it->second : 0;
}

void AgentController::increment_quota(const std::string& tid) {
    std::unique_lock lock(quota_mutex_);
    tenant_agent_counts_[tid]++;
}
void AgentController::decrement_quota(const std::string& tid) {
    std::unique_lock lock(quota_mutex_);
    auto it = tenant_agent_counts_.find(tid);
    if (it != tenant_agent_counts_.end() && it->second > 0) it->second--;
}

}  // namespace prodxcloud::agents
