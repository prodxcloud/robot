#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include "common/types.hpp"

namespace prodxcloud::datasets {

struct OutlierReport {
    std::string column_name; double mean, std_dev; size_t outlier_count;
    std::vector<size_t> outlier_row_indices;
};

class StatisticalOutlierDetector {
public:
    explicit StatisticalOutlierDetector(double threshold = 3.0) : threshold_(threshold) {}

    std::vector<OutlierReport> detect(const Dataset& ds, const std::vector<std::string>& cols = {}) const {
        auto check_cols = cols.empty() ? ds.column_names : cols;
        std::vector<OutlierReport> reports;
        for (const auto& col : check_cols) {
            std::vector<std::pair<size_t, double>> vals;
            for (size_t i = 0; i < ds.rows.size(); ++i) {
                auto it = ds.rows[i].fields.find(col);
                if (it != ds.rows[i].fields.end()) {
                    try { vals.emplace_back(i, std::stod(it->second)); } catch (...) {}
                }
            }
            if (vals.size() < 3) continue;
            double sum = 0; for (auto& [_, v] : vals) sum += v;
            double mean = sum / vals.size();
            double sq = 0; for (auto& [_, v] : vals) { double d = v - mean; sq += d * d; }
            double sd = std::sqrt(sq / vals.size());
            if (sd < 1e-10) continue;
            OutlierReport r{.column_name = col, .mean = mean, .std_dev = sd, .outlier_count = 0};
            for (auto& [idx, v] : vals) if (std::abs(v - mean) / sd > threshold_) r.outlier_row_indices.push_back(idx);
            r.outlier_count = r.outlier_row_indices.size();
            if (r.outlier_count > 0) spdlog::info("Col '{}': {} outliers", col, r.outlier_count);
            reports.push_back(std::move(r));
        }
        return reports;
    }

    size_t remove_outliers(Dataset& ds) const {
        auto reports = detect(ds);
        std::vector<bool> is_out(ds.rows.size(), false);
        for (const auto& r : reports) for (size_t i : r.outlier_row_indices) is_out[i] = true;
        auto orig = ds.rows.size(); size_t w = 0;
        for (size_t i = 0; i < ds.rows.size(); ++i)
            if (!is_out[i]) { if (w != i) ds.rows[w] = std::move(ds.rows[i]); ++w; }
        ds.rows.resize(w);
        return orig - ds.rows.size();
    }
private:
    double threshold_;
};

}  // namespace prodxcloud::datasets
