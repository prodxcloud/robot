#include "agents/policies/retry_policy.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace prodxcloud::agents::policies {

RetryPolicy::RetryPolicy(int max_attempts, int base_delay_ms, int max_delay_ms)
    : max_attempts_(max_attempts), base_delay_ms_(base_delay_ms), max_delay_ms_(max_delay_ms) {}

std::chrono::milliseconds RetryPolicy::compute_delay(int attempt) const {
    double exponential = base_delay_ms_ * std::pow(2.0, attempt);
    double capped      = std::min(exponential, static_cast<double>(max_delay_ms_));
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist(0.0, capped);
    return std::chrono::milliseconds(static_cast<int>(dist(rng)));
}

}  // namespace prodxcloud::agents::policies
