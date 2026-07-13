#pragma once

/// @file timeout_policy.hpp
/// @brief Wraps any callable with a deadline for timeout enforcement.

#include <chrono>
#include <future>
#include <type_traits>

#include "common/types.hpp"

namespace prodxcloud::agents::policies {

class TimeoutPolicy {
public:
    explicit TimeoutPolicy(Millis timeout) : timeout_(timeout) {}

    template <typename F>
    auto execute(F&& func) -> std::invoke_result_t<F> {
        using R = std::invoke_result_t<F>;
        auto future = std::async(std::launch::async, std::forward<F>(func));
        if (future.wait_for(timeout_) == std::future_status::timeout) {
            if constexpr (requires { R(std::unexpected(Error::timeout(""))); })
                return std::unexpected(Error::timeout("Timed out after " +
                                                      std::to_string(timeout_.count()) + "ms"));
            else
                throw std::runtime_error("Timed out after " + std::to_string(timeout_.count()) + "ms");
        }
        return future.get();
    }

    [[nodiscard]] Millis timeout() const { return timeout_; }

private:
    Millis timeout_;
};

}  // namespace prodxcloud::agents::policies
