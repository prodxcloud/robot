#pragma once

/// @file kv_cache.hpp
/// @brief KV cache management for transformer models with LRU eviction.

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"

namespace prodxcloud::inference {

struct KVCacheSlot {
    std::string sequence_id;
    std::vector<float> key_cache;
    std::vector<float> value_cache;
    size_t num_tokens   = 0;
    size_t memory_bytes = 0;
};

class KVCache {
public:
    explicit KVCache(size_t max_memory_bytes, int num_layers = 32, int hidden_size = 4096);

    KVCache(const KVCache&)            = delete;
    KVCache& operator=(const KVCache&) = delete;

    Result<void> allocate(const std::string& sequence_id, size_t max_tokens);
    void free(const std::string& sequence_id);
    [[nodiscard]] bool has_slot(const std::string& sequence_id) const;
    [[nodiscard]] size_t memory_usage() const;
    [[nodiscard]] size_t active_slots() const;
    [[nodiscard]] size_t max_memory() const { return max_memory_bytes_; }

private:
    void evict_lru();

    size_t max_memory_bytes_;
    int num_layers_, hidden_size_;
    size_t current_memory_ = 0;
    mutable std::mutex mutex_;
    std::list<std::string> lru_order_;

    struct CacheEntry {
        KVCacheSlot slot;
        std::list<std::string>::iterator lru_it;
    };
    std::unordered_map<std::string, CacheEntry> slots_;
};

}  // namespace prodxcloud::inference
