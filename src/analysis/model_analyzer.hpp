#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "common/types.hpp"

namespace prodxcloud::analysis {

struct LayerInfo {
    std::string name, type;
    int64_t param_count, flops;
    size_t memory_bytes;
};

struct ModelReport {
    std::string model_id;
    int layer_count = 0;
    int64_t param_count = 0, total_flops = 0;
    size_t memory_bytes = 0;
    std::vector<LayerInfo> layers;
    std::string summary;
};

class ModelAnalyzer {
public:
    Result<ModelReport> analyze(const ModelConfig& config) const;
    static int64_t estimate_linear_flops(int in, int out, int batch = 1);
    static int64_t estimate_attention_flops(int hidden, int heads, int seq);
    static std::string format_report(const ModelReport& r);
};

}  // namespace prodxcloud::analysis
