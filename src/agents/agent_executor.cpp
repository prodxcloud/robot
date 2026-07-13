#include "agents/agent_executor.hpp"
#include <spdlog/spdlog.h>
#include <thread>

namespace prodxcloud::agents {

std::future<Result<TaskResult>> AgentExecutor::dispatch(std::shared_ptr<AgentBase> agent,
                                                        Task task, Millis timeout) {
    return std::async(std::launch::async,
                      [agent, task = std::move(task), timeout]() mutable -> Result<TaskResult> {
        auto start  = Clock::now();
        auto future = std::async(std::launch::async,
                                 [&agent, &task]() -> Result<TaskResult> { return agent->execute(task); });
        if (future.wait_for(timeout) == std::future_status::timeout) {
            agent->cancel();
            return std::unexpected(Error::timeout("Task timed out after " +
                                                  std::to_string(timeout.count()) + "ms"));
        }
        auto result  = future.get();
        auto elapsed = std::chrono::duration_cast<Millis>(Clock::now() - start).count();
        if (result.has_value()) result->duration_ms = static_cast<double>(elapsed);
        return result;
    });
}

void AgentExecutor::cancel(std::shared_ptr<AgentBase> agent) {
    if (agent) { agent->cancel(); spdlog::info("Cancel requested for agent {}", agent->id()); }
}

std::future<Result<TaskResult>> AgentExecutor::dispatch_with_retry(
    std::shared_ptr<AgentBase> agent, Task task, const policies::RetryPolicy& retry,
    Millis timeout) {
    return std::async(std::launch::async,
                      [this, agent, task = std::move(task), retry, timeout]() mutable -> Result<TaskResult> {
        for (int attempt = 0; attempt <= retry.max_attempts(); ++attempt) {
            if (attempt > 0) {
                auto delay = retry.compute_delay(attempt);
                spdlog::info("Retry {} for agent {} task {}, waiting {}ms",
                             attempt, agent->id(), task.id, delay.count());
                std::this_thread::sleep_for(delay);
            }
            auto f = dispatch(agent, task, timeout);
            auto r = f.get();
            if (r.has_value()) return r;
            if (r.error().code != 500 && r.error().code != 408) return r;
        }
        return std::unexpected(Error::internal("All retry attempts exhausted for " + task.id));
    });
}

}  // namespace prodxcloud::agents
