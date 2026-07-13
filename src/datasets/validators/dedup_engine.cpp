#include <algorithm>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <spdlog/spdlog.h>
#include "common/types.hpp"

namespace prodxcloud::datasets {

struct DedupResult {
    size_t original_count, unique_count, duplicate_count;
    std::unordered_map<size_t, size_t> duplicate_map;
};

class MinHashDeduplicator {
public:
    explicit MinHashDeduplicator(int num_hashes = 128, int num_bands = 16, double threshold = 0.8)
        : num_hashes_(num_hashes), num_bands_(num_bands), rows_per_band_(num_hashes/num_bands),
          threshold_(threshold) {
        std::mt19937_64 rng(42);
        std::uniform_int_distribution<uint64_t> d(1, (1ULL<<61)-1);
        for (int i = 0; i < num_hashes_; ++i) hash_coeffs_.emplace_back(d(rng), d(rng));
    }

    DedupResult deduplicate(Dataset& ds) const {
        DedupResult res{.original_count=ds.rows.size(), .unique_count=0, .duplicate_count=0};
        if (ds.rows.empty()) return res;
        std::vector<std::vector<uint64_t>> sigs;
        for (const auto& row : ds.rows) {
            std::string text;
            for (const auto& [n, v] : row.fields) text += v + " ";
            sigs.push_back(compute_sig(shingle(text)));
        }
        std::vector<bool> is_dup(ds.rows.size(), false);
        std::unordered_map<size_t, std::vector<size_t>> buckets;
        for (int band = 0; band < num_bands_; ++band) {
            buckets.clear(); int start = band * rows_per_band_;
            for (size_t doc = 0; doc < sigs.size(); ++doc) {
                size_t h = 0;
                for (int r = 0; r < rows_per_band_ && start+r < num_hashes_; ++r)
                    h ^= std::hash<uint64_t>{}(sigs[doc][start+r]) + 0x9e3779b9 + (h<<6) + (h>>2);
                buckets[h].push_back(doc);
            }
            for (const auto& [_, docs] : buckets)
                for (size_t i = 0; i < docs.size(); ++i) { if (is_dup[docs[i]]) continue;
                    for (size_t j = i+1; j < docs.size(); ++j) { if (is_dup[docs[j]]) continue;
                        if (jaccard(sigs[docs[i]], sigs[docs[j]]) >= threshold_) {
                            is_dup[docs[j]] = true; res.duplicate_map[docs[j]] = docs[i]; }}}
        }
        size_t w = 0;
        for (size_t i = 0; i < ds.rows.size(); ++i)
            if (!is_dup[i]) { if (w != i) ds.rows[w] = std::move(ds.rows[i]); ++w; }
        ds.rows.resize(w);
        res.unique_count = ds.rows.size(); res.duplicate_count = res.original_count - res.unique_count;
        spdlog::info("Dedup: {} -> {} ({} removed)", res.original_count, res.unique_count, res.duplicate_count);
        return res;
    }

private:
    int num_hashes_, num_bands_, rows_per_band_; double threshold_;
    std::vector<std::pair<uint64_t, uint64_t>> hash_coeffs_;

    std::vector<std::string> shingle(const std::string& t, int n=3) const {
        std::vector<std::string> s;
        if ((int)t.size() < n) { s.push_back(t); return s; }
        for (size_t i = 0; i+n <= t.size(); ++i) s.push_back(t.substr(i, n));
        return s;
    }

    std::vector<uint64_t> compute_sig(const std::vector<std::string>& shingles) const {
        constexpr uint64_t P = (1ULL<<61)-1;
        std::vector<uint64_t> sig(num_hashes_, UINT64_MAX);
        for (const auto& s : shingles) {
            uint64_t hv = std::hash<std::string>{}(s);
            for (int i = 0; i < num_hashes_; ++i) {
                auto [a, b] = hash_coeffs_[i];
                sig[i] = std::min(sig[i], (a*hv+b)%P);
            }
        }
        return sig;
    }

    static double jaccard(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
        int m = 0; for (size_t i = 0; i < a.size() && i < b.size(); ++i) if (a[i]==b[i]) ++m;
        return (double)m / a.size();
    }
};

}  // namespace prodxcloud::datasets
