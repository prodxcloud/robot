#pragma once

/// @file retry_policy.hpp
/// @brief Retry policy with exponential backoff and jitter.

#include <chrono>

namespace prodxcloud::agents::policies {

class RetryPolicy {
public:
    explicit RetryPolicy(int max_attempts = 3, int base_delay_ms = 100, int max_delay_ms = 5000);
    [[nodiscard]] int max_attempts() const { return max_attempts_; }
    [[nodiscard]] int base_delay_ms() const { return base_delay_ms_; }
    [[nodiscard]] int max_delay_ms() const { return max_delay_ms_; }
    [[nodiscard]] std::chrono::milliseconds compute_delay(int attempt) const;

private:
    int max_attempts_;
    int base_delay_ms_;
    int max_delay_ms_;
};

}  // namespace prodxcloud::agents::policies
