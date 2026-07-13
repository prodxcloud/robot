#pragma once

/// @file rate_limit_policy.hpp
/// @brief Token bucket rate limiter for per-agent and per-tenant limits.

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace prodxcloud::agents::policies {

class TokenBucketRateLimiter {
public:
    explicit TokenBucketRateLimiter(double capacity, double refill_rate_per_sec);
    bool try_acquire(double tokens = 1.0);
    [[nodiscard]] double available_tokens() const;

private:
    void refill();
    double capacity_, refill_rate_, tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    mutable std::mutex mutex_;
};

class RateLimitPolicy {
public:
    explicit RateLimitPolicy(double per_agent_rps = 100.0, double per_tenant_rps = 1000.0);
    bool allow(const std::string& agent_id, const std::string& tenant_id);

private:
    double per_agent_rps_, per_tenant_rps_;
    std::mutex mutex_;
    std::unordered_map<std::string, TokenBucketRateLimiter> agent_limiters_;
    std::unordered_map<std::string, TokenBucketRateLimiter> tenant_limiters_;
};

}  // namespace prodxcloud::agents::policies
