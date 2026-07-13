#pragma once

/// @file agent_executor.hpp
/// @brief Dispatches tasks to agents with timeout, retry, and cancellation support.

#include <future>
#include <memory>

#include "agents/agent_base.hpp"
#include "agents/policies/retry_policy.hpp"
#include "common/types.hpp"

namespace prodxcloud::agents {

class AgentExecutor {
public:
    AgentExecutor()  = default;
    ~AgentExecutor() = default;

    AgentExecutor(const AgentExecutor&)            = delete;
    AgentExecutor& operator=(const AgentExecutor&) = delete;

    std::future<Result<TaskResult>> dispatch(std::shared_ptr<AgentBase> agent, Task task,
                                             Millis timeout = Millis{30000});
    void cancel(std::shared_ptr<AgentBase> agent);
    std::future<Result<TaskResult>> dispatch_with_retry(std::shared_ptr<AgentBase> agent, Task task,
                                                        const policies::RetryPolicy& retry_policy,
                                                        Millis timeout = Millis{30000});
};

}  // namespace prodxcloud::agents
