#include "agents/policies/rate_limit_policy.hpp"
#include <algorithm>

namespace prodxcloud::agents::policies {

TokenBucketRateLimiter::TokenBucketRateLimiter(double capacity, double refill_rate_per_sec)
    : capacity_(capacity), refill_rate_(refill_rate_per_sec), tokens_(capacity),
      last_refill_(std::chrono::steady_clock::now()) {}

void TokenBucketRateLimiter::refill() {
    auto now     = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - last_refill_).count();
    tokens_      = std::min(capacity_, tokens_ + elapsed * refill_rate_);
    last_refill_ = now;
}

bool TokenBucketRateLimiter::try_acquire(double tokens) {
    std::lock_guard lock(mutex_);
    refill();
    if (tokens_ >= tokens) { tokens_ -= tokens; return true; }
    return false;
}

double TokenBucketRateLimiter::available_tokens() const { return tokens_; }

RateLimitPolicy::RateLimitPolicy(double per_agent_rps, double per_tenant_rps)
    : per_agent_rps_(per_agent_rps), per_tenant_rps_(per_tenant_rps) {}

bool RateLimitPolicy::allow(const std::string& agent_id, const std::string& tenant_id) {
    std::lock_guard lock(mutex_);
    auto [ait, _a] = agent_limiters_.try_emplace(agent_id, per_agent_rps_, per_agent_rps_);
    if (!ait->second.try_acquire()) return false;
    auto [tit, _t] = tenant_limiters_.try_emplace(tenant_id, per_tenant_rps_, per_tenant_rps_);
    return tit->second.try_acquire();
}

}  // namespace prodxcloud::agents::policies
