#include "inference/batch_scheduler.hpp"
#include "inference/engine.hpp"
#include <spdlog/spdlog.h>

namespace prodxcloud::inference {

DynamicBatchScheduler::DynamicBatchScheduler(InferenceEngine& engine, BatchConfig config)
    : engine_(engine), config_(config) {
    scheduler_thread_ = std::thread(&DynamicBatchScheduler::scheduler_loop, this);
    spdlog::info("BatchScheduler: max_batch={}, max_wait={}ms", config_.max_batch_size, config_.max_wait_ms);
}

DynamicBatchScheduler::~DynamicBatchScheduler() { stop(); }

std::future<Result<InferenceResult>> DynamicBatchScheduler::submit(InferenceRequest request) {
    std::promise<Result<InferenceResult>> promise;
    auto future = promise.get_future();
    { std::lock_guard lock(queue_mutex_); pending_queue_.push({std::move(request), std::move(promise)}); }
    queue_cv_.notify_one();
    return future;
}

void DynamicBatchScheduler::stop() {
    if (running_.exchange(false)) {
        queue_cv_.notify_all();
        if (scheduler_thread_.joinable()) scheduler_thread_.join();
    }
}

size_t DynamicBatchScheduler::pending_count() const { return pending_queue_.size(); }

void DynamicBatchScheduler::scheduler_loop() {
    while (running_.load()) {
        std::vector<PendingRequest> batch;
        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !pending_queue_.empty() || !running_.load(); });
            if (!running_.load() && pending_queue_.empty()) break;
            auto deadline = Clock::now() + Millis(config_.max_wait_ms);
            while (batch.size() < static_cast<size_t>(config_.max_batch_size)) {
                if (!pending_queue_.empty()) {
                    batch.push_back(std::move(pending_queue_.front()));
                    pending_queue_.pop();
                } else {
                    if (Clock::now() >= deadline) break;
                    queue_cv_.wait_until(lock, deadline, [this] { return !pending_queue_.empty() || !running_.load(); });
                    if (!running_.load() || pending_queue_.empty()) break;
                }
            }
        }
        if (!batch.empty()) dispatch_batch(batch);
    }
}

void DynamicBatchScheduler::dispatch_batch(std::vector<PendingRequest>& batch) {
    for (auto& p : batch) p.promise.set_value(engine_.infer(p.request));
}

}  // namespace prodxcloud::inference
