#pragma once
/// @brief Benchmark — model-dependent benchmarking is delegated to SLM-Models.
///        Drift detection (pure math) runs locally.
#include <string>
#include <vector>
#include "common/types.hpp"

namespace prodxcloud::analysis {

struct BenchmarkConfig {
    std::string model_id;
    std::string prompt = "Hello, how are you?";
    int num_iterations = 100, warmup_iterations = 10, max_tokens = 128;
    float temperature = 0.0f;
};

struct BenchmarkReport {
    std::string model_id; int num_iterations = 0;
    double p50_latency_ms=0, p95_latency_ms=0, p99_latency_ms=0;
    double mean_latency_ms=0, min_latency_ms=0, max_latency_ms=0;
    double throughput_tokens_per_sec=0, total_duration_ms=0;
    size_t peak_memory_bytes = 0;
};

class InferenceBenchmark {
public:
    /// Always returns an error — benchmarking requires SLM-Models service.
    Result<BenchmarkReport> run(const BenchmarkConfig& config) const;
    static std::string format_report(const BenchmarkReport& r);
private:
    static double percentile(const std::vector<double>& sorted, double p);
};

}  // namespace prodxcloud::analysis
