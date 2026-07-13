#include "analysis/benchmark.hpp"
#include <spdlog/spdlog.h>

namespace prodxcloud::analysis {

Result<BenchmarkReport> InferenceBenchmark::run(const BenchmarkConfig& config) const {
    spdlog::warn("[benchmark] run() called for '{}' — delegate to SLM-Models service",
                 config.model_id);
    return std::unexpected(Error::bad_request(
        "Inference benchmarking requires model execution — use the SLM-Models service"));
}

double InferenceBenchmark::percentile(const std::vector<double>& s, double p) {
    if (s.empty()) return 0;
    double idx = p * (s.size() - 1);
    size_t lo = (size_t)idx, hi = std::min(lo + 1, s.size() - 1);
    return s[lo] * (1.0 - (idx - lo)) + s[hi] * (idx - lo);
}

std::string InferenceBenchmark::format_report(const BenchmarkReport& r) {
    return "Benchmark for " + r.model_id + ": delegated to SLM-Models service";
}

}  // namespace prodxcloud::analysis
