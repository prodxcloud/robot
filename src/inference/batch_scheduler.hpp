#pragma once

/// @file batch_scheduler.hpp
/// @brief Dynamic batch scheduler for efficient inference.

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "common/types.hpp"

namespace prodxcloud::inference {

class InferenceEngine;

struct BatchConfig {
    int max_batch_size = 32;
    int max_wait_ms    = 10;
};

class DynamicBatchScheduler {
public:
    explicit DynamicBatchScheduler(InferenceEngine& engine, BatchConfig config = {});
    ~DynamicBatchScheduler();

    DynamicBatchScheduler(const DynamicBatchScheduler&)            = delete;
    DynamicBatchScheduler& operator=(const DynamicBatchScheduler&) = delete;

    std::future<Result<InferenceResult>> submit(InferenceRequest request);
    void stop();
    [[nodiscard]] size_t pending_count() const;

private:
    struct PendingRequest {
        InferenceRequest request;
        std::promise<Result<InferenceResult>> promise;
    };

    void scheduler_loop();
    void dispatch_batch(std::vector<PendingRequest>& batch);

    InferenceEngine& engine_;
    BatchConfig config_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<PendingRequest> pending_queue_;
    std::atomic<bool> running_{true};
    std::thread scheduler_thread_;
};

}  // namespace prodxcloud::inference
