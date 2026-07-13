#include "agents/agent_base.hpp"
#include <spdlog/spdlog.h>

namespace prodxcloud::agents {

AgentBase::AgentBase(AgentConfig config) {
    metadata_.id          = generate_uuid();
    metadata_.tenant_id   = std::move(config.tenant_id);
    metadata_.name        = std::move(config.name);
    metadata_.policy_name = std::move(config.policy_name);
    metadata_.created_at  = now_iso8601();
}

AgentState AgentBase::state() const { return state_.load(std::memory_order_acquire); }

void AgentBase::on_state_change(StateChangeCallback cb) {
    std::lock_guard lock(callback_mutex_);
    state_callbacks_.push_back(std::move(cb));
}

bool AgentBase::transition_to(AgentState new_state) {
    AgentState old = state_.load(std::memory_order_acquire);
    bool valid     = false;
    switch (old) {
        case AgentState::IDLE:
            valid = (new_state == AgentState::QUEUED || new_state == AgentState::RUNNING ||
                     new_state == AgentState::TERMINATED);
            break;
        case AgentState::QUEUED:
            valid = (new_state == AgentState::RUNNING || new_state == AgentState::TERMINATED);
            break;
        case AgentState::RUNNING:
            valid = (new_state == AgentState::IDLE || new_state == AgentState::ERROR ||
                     new_state == AgentState::TERMINATED);
            break;
        case AgentState::ERROR:
            valid = (new_state == AgentState::IDLE || new_state == AgentState::TERMINATED);
            break;
        case AgentState::TERMINATED:
            valid = false;
            break;
    }
    if (!valid) {
        spdlog::warn("Invalid state transition: {} -> {} for agent {}",
                     agent_state_to_string(old), agent_state_to_string(new_state), metadata_.id);
        return false;
    }
    state_.store(new_state, std::memory_order_release);

    std::lock_guard lock(callback_mutex_);
    for (auto& cb : state_callbacks_) {
        try { cb(metadata_.id, old, new_state); }
        catch (const std::exception& e) { spdlog::error("State callback threw: {}", e.what()); }
    }
    spdlog::info("Agent {} transitioned: {} -> {}", metadata_.id,
                 agent_state_to_string(old), agent_state_to_string(new_state));
    return true;
}

}  // namespace prodxcloud::agents
