#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include "common/types.hpp"

namespace prodxcloud::api::middleware {

struct RateLimitConfig {
    double requests_per_second = 100.0;
    int burst_size = 200;
    int max_tenants = 10000;
};

class HttpRateLimiter {
public:
    explicit HttpRateLimiter(const RateLimitConfig& config = {});

    // Returns true if request is allowed
    bool allow(const std::string& key);

    // Returns remaining tokens for a key
    int remaining(const std::string& key) const;

    void reset(const std::string& key);
    void reset_all();

private:
    struct Bucket {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;
    };

    void refill(Bucket& bucket) const;

    RateLimitConfig config_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, Bucket> buckets_;
};

}  // namespace prodxcloud::api::middleware
