#pragma once
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/types.hpp"

namespace prodxcloud::storage {

struct VectorEntry {
    std::string id;
    std::string tenant_id;
    std::string namespace_id;
    std::vector<float> embedding;
    std::string metadata_json;
};

struct SearchResult {
    std::string id;
    float score;
    std::string metadata_json;
};

struct VectorStoreConfig {
    int dimension = 768;
    int nlist = 100;         // IVF clusters
    int nprobe = 10;         // Search probes
    size_t max_vectors = 1'000'000;
    std::string storage_path;
};

class VectorStore {
public:
    explicit VectorStore(const VectorStoreConfig& config);
    ~VectorStore();

    VectorStore(const VectorStore&) = delete;
    VectorStore& operator=(const VectorStore&) = delete;

    Result<void> upsert(const VectorEntry& entry);
    Result<void> upsert_batch(const std::vector<VectorEntry>& entries);
    Result<std::vector<SearchResult>> search(const std::string& tenant_id,
                                              const std::string& namespace_id,
                                              const std::vector<float>& query,
                                              int top_k = 10) const;
    Result<void> remove(const std::string& tenant_id, const std::string& id);
    Result<void> remove_namespace(const std::string& tenant_id, const std::string& namespace_id);
    size_t count(const std::string& tenant_id) const;
    size_t total_count() const;

private:
    struct TenantIndex {
        std::vector<std::string> ids;
        std::vector<std::string> namespaces;
        std::vector<std::vector<float>> vectors;
        std::vector<std::string> metadata;
    };

    VectorStoreConfig config_;
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, TenantIndex> indices_;

    float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) const;
};

}  // namespace prodxcloud::storage
