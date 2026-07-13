#include "api/middleware/rate_limiter.hpp"
#include <algorithm>

namespace prodxcloud::api::middleware {

HttpRateLimiter::HttpRateLimiter(const RateLimitConfig& config) : config_(config) {}

void HttpRateLimiter::refill(Bucket& bucket) const {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - bucket.last_refill).count();
    bucket.tokens = std::min(
        static_cast<double>(config_.burst_size),
        bucket.tokens + elapsed * config_.requests_per_second
    );
    bucket.last_refill = now;
}

bool HttpRateLimiter::allow(const std::string& key) {
    std::lock_guard lock(mu_);

    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        if (static_cast<int>(buckets_.size()) >= config_.max_tenants) {
            // Evict oldest bucket
            auto oldest = buckets_.begin();
            for (auto jt = buckets_.begin(); jt != buckets_.end(); ++jt) {
                if (jt->second.last_refill < oldest->second.last_refill)
                    oldest = jt;
            }
            buckets_.erase(oldest);
        }
        buckets_[key] = Bucket{
            static_cast<double>(config_.burst_size - 1),
            std::chrono::steady_clock::now()
        };
        return true;
    }

    refill(it->second);
    if (it->second.tokens >= 1.0) {
        it->second.tokens -= 1.0;
        return true;
    }
    return false;
}

int HttpRateLimiter::remaining(const std::string& key) const {
    std::lock_guard lock(mu_);
    auto it = buckets_.find(key);
    if (it == buckets_.end()) return config_.burst_size;
    return static_cast<int>(it->second.tokens);
}

void HttpRateLimiter::reset(const std::string& key) {
    std::lock_guard lock(mu_);
    buckets_.erase(key);
}

void HttpRateLimiter::reset_all() {
    std::lock_guard lock(mu_);
    buckets_.clear();
}

}  // namespace prodxcloud::api::middleware
