#pragma once

/// @file agent_base.hpp
/// @brief Abstract base class for all ProdxCloud agents with lifecycle management.

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "common/types.hpp"
#include "common/uuid.hpp"

namespace prodxcloud::agents {

enum class AgentState { IDLE, RUNNING, QUEUED, ERROR, TERMINATED };

constexpr std::string_view agent_state_to_string(AgentState s) {
    switch (s) {
        case AgentState::IDLE:       return "IDLE";
        case AgentState::RUNNING:    return "RUNNING";
        case AgentState::QUEUED:     return "QUEUED";
        case AgentState::ERROR:      return "ERROR";
        case AgentState::TERMINATED: return "TERMINATED";
    }
    return "UNKNOWN";
}

struct AgentMetadata {
    std::string id;
    std::string tenant_id;
    std::string name;
    std::string policy_name;
    std::string created_at;
};

using StateChangeCallback =
    std::function<void(const std::string& agent_id, AgentState from, AgentState to)>;

class AgentBase {
public:
    explicit AgentBase(AgentConfig config);
    virtual ~AgentBase() = default;

    AgentBase(const AgentBase&)            = delete;
    AgentBase& operator=(const AgentBase&) = delete;
    AgentBase(AgentBase&&)                 = default;
    AgentBase& operator=(AgentBase&&)      = default;

    virtual Result<TaskResult> execute(Task& task) = 0;
    virtual void cancel()                          = 0;
    virtual Result<bool> health_check()            = 0;

    [[nodiscard]] AgentState state() const;
    [[nodiscard]] const AgentMetadata& metadata() const { return metadata_; }
    [[nodiscard]] const std::string& id() const { return metadata_.id; }
    [[nodiscard]] const std::string& name() const { return metadata_.name; }
    [[nodiscard]] const std::string& tenant_id() const { return metadata_.tenant_id; }

    void on_state_change(StateChangeCallback cb);

protected:
    bool transition_to(AgentState new_state);
    CancellationToken cancellation_token_;

private:
    AgentMetadata metadata_;
    std::atomic<AgentState> state_{AgentState::IDLE};
    mutable std::mutex callback_mutex_;
    std::vector<StateChangeCallback> state_callbacks_;
};

}  // namespace prodxcloud::agents
