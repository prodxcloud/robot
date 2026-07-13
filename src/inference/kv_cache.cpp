#include "inference/kv_cache.hpp"
#include <spdlog/spdlog.h>

namespace prodxcloud::inference {

KVCache::KVCache(size_t max_mem, int layers, int hidden)
    : max_memory_bytes_(max_mem), num_layers_(layers), hidden_size_(hidden) {
    spdlog::info("KVCache: max={} MB, layers={}, hidden={}", max_mem / (1024*1024), layers, hidden);
}

Result<void> KVCache::allocate(const std::string& seq_id, size_t max_tokens) {
    std::lock_guard lock(mutex_);
    if (slots_.contains(seq_id))
        return std::unexpected(Error::bad_request("Slot exists: " + seq_id));
    size_t mem = 2ULL * num_layers_ * max_tokens * hidden_size_ * sizeof(float);
    while (current_memory_ + mem > max_memory_bytes_ && !lru_order_.empty()) evict_lru();
    if (current_memory_ + mem > max_memory_bytes_)
        return std::unexpected(Error::internal("KV cache OOM"));
    KVCacheSlot slot{.sequence_id = seq_id,
                     .key_cache = std::vector<float>(num_layers_ * max_tokens * hidden_size_, 0.0f),
                     .value_cache = std::vector<float>(num_layers_ * max_tokens * hidden_size_, 0.0f),
                     .num_tokens = 0, .memory_bytes = mem};
    lru_order_.push_front(seq_id);
    slots_[seq_id] = CacheEntry{.slot = std::move(slot), .lru_it = lru_order_.begin()};
    current_memory_ += mem;
    return {};
}

void KVCache::free(const std::string& seq_id) {
    std::lock_guard lock(mutex_);
    auto it = slots_.find(seq_id);
    if (it == slots_.end()) return;
    current_memory_ -= it->second.slot.memory_bytes;
    lru_order_.erase(it->second.lru_it);
    slots_.erase(it);
}

bool KVCache::has_slot(const std::string& seq_id) const {
    std::lock_guard lock(mutex_); return slots_.contains(seq_id);
}
size_t KVCache::memory_usage() const { std::lock_guard lock(mutex_); return current_memory_; }
size_t KVCache::active_slots() const { std::lock_guard lock(mutex_); return slots_.size(); }

void KVCache::evict_lru() {
    if (lru_order_.empty()) return;
    auto vid = lru_order_.back(); lru_order_.pop_back();
    auto it = slots_.find(vid);
    if (it != slots_.end()) { current_memory_ -= it->second.slot.memory_bytes; slots_.erase(it); }
    spdlog::info("Evicted KV cache: seq={}", vid);
}

}  // namespace prodxcloud::inference
