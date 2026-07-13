#include "storage/vector_store.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace prodxcloud::storage {

VectorStore::VectorStore(const VectorStoreConfig& config) : config_(config) {
    spdlog::info("VectorStore initialized: dim={} max_vectors={}", config_.dimension, config_.max_vectors);
}

VectorStore::~VectorStore() = default;

Result<void> VectorStore::upsert(const VectorEntry& entry) {
    if (static_cast<int>(entry.embedding.size()) != config_.dimension)
        return std::unexpected(Error::validation(
            "Dimension mismatch: expected " + std::to_string(config_.dimension) +
            " got " + std::to_string(entry.embedding.size())));

    std::unique_lock lock(mu_);
    auto& idx = indices_[entry.tenant_id];

    // Check for existing ID and update
    for (size_t i = 0; i < idx.ids.size(); ++i) {
        if (idx.ids[i] == entry.id && idx.namespaces[i] == entry.namespace_id) {
            idx.vectors[i] = entry.embedding;
            idx.metadata[i] = entry.metadata_json;
            return {};
        }
    }

    if (total_count() >= config_.max_vectors)
        return std::unexpected(Error::internal("Vector store capacity exceeded"));

    idx.ids.push_back(entry.id);
    idx.namespaces.push_back(entry.namespace_id);
    idx.vectors.push_back(entry.embedding);
    idx.metadata.push_back(entry.metadata_json);
    return {};
}

Result<void> VectorStore::upsert_batch(const std::vector<VectorEntry>& entries) {
    for (const auto& e : entries) {
        auto r = upsert(e);
        if (!r) return r;
    }
    return {};
}

Result<std::vector<SearchResult>> VectorStore::search(const std::string& tenant_id,
                                                       const std::string& namespace_id,
                                                       const std::vector<float>& query,
                                                       int top_k) const {
    if (static_cast<int>(query.size()) != config_.dimension)
        return std::unexpected(Error::validation("Query dimension mismatch"));

    std::shared_lock lock(mu_);
    auto it = indices_.find(tenant_id);
    if (it == indices_.end()) return std::vector<SearchResult>{};

    const auto& idx = it->second;
    std::vector<std::pair<float, size_t>> scores;

    for (size_t i = 0; i < idx.ids.size(); ++i) {
        if (!namespace_id.empty() && idx.namespaces[i] != namespace_id) continue;
        float sim = cosine_similarity(query, idx.vectors[i]);
        scores.emplace_back(sim, i);
    }

    std::partial_sort(scores.begin(),
                      scores.begin() + std::min(static_cast<int>(scores.size()), top_k),
                      scores.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<SearchResult> results;
    for (int i = 0; i < std::min(static_cast<int>(scores.size()), top_k); ++i)
        results.push_back({idx.ids[scores[i].second], scores[i].first,
                           idx.metadata[scores[i].second]});
    return results;
}

Result<void> VectorStore::remove(const std::string& tenant_id, const std::string& id) {
    std::unique_lock lock(mu_);
    auto it = indices_.find(tenant_id);
    if (it == indices_.end()) return {};

    auto& idx = it->second;
    for (size_t i = 0; i < idx.ids.size(); ++i) {
        if (idx.ids[i] == id) {
            idx.ids.erase(idx.ids.begin() + i);
            idx.namespaces.erase(idx.namespaces.begin() + i);
            idx.vectors.erase(idx.vectors.begin() + i);
            idx.metadata.erase(idx.metadata.begin() + i);
            return {};
        }
    }
    return {};
}

Result<void> VectorStore::remove_namespace(const std::string& tenant_id,
                                            const std::string& namespace_id) {
    std::unique_lock lock(mu_);
    auto it = indices_.find(tenant_id);
    if (it == indices_.end()) return {};

    auto& idx = it->second;
    for (int i = static_cast<int>(idx.ids.size()) - 1; i >= 0; --i) {
        if (idx.namespaces[i] == namespace_id) {
            idx.ids.erase(idx.ids.begin() + i);
            idx.namespaces.erase(idx.namespaces.begin() + i);
            idx.vectors.erase(idx.vectors.begin() + i);
            idx.metadata.erase(idx.metadata.begin() + i);
        }
    }
    return {};
}

size_t VectorStore::count(const std::string& tenant_id) const {
    std::shared_lock lock(mu_);
    auto it = indices_.find(tenant_id);
    return it != indices_.end() ? it->second.ids.size() : 0;
}

size_t VectorStore::total_count() const {
    size_t total = 0;
    for (const auto& [_, idx] : indices_) total += idx.ids.size();
    return total;
}

float VectorStore::cosine_similarity(const std::vector<float>& a,
                                      const std::vector<float>& b) const {
    float dot = 0, na = 0, nb = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    float denom = std::sqrt(na) * std::sqrt(nb);
    return denom > 0 ? dot / denom : 0.0f;
}

}  // namespace prodxcloud::storage
