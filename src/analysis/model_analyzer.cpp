#include "analysis/model_analyzer.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <spdlog/spdlog.h>

namespace prodxcloud::analysis {

Result<ModelReport> ModelAnalyzer::analyze(const ModelConfig& config) const {
    std::ifstream f(config.path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return std::unexpected(Error::not_found("Model not found: " + config.path));
    size_t fsz = static_cast<size_t>(f.tellg()); f.close();

    ModelReport r; r.model_id = config.id;
    int hidden = 4096, heads = 32, layers = 32, vocab = 32000, inter = hidden * 4;

    r.layers.push_back({"token_embedding", "embedding", int64_t(vocab)*hidden, 0, size_t(vocab*hidden*4)});
    for (int i = 0; i < layers; ++i) {
        int64_t ap = 4LL*hidden*hidden, af = estimate_attention_flops(hidden, heads, config.context_length);
        r.layers.push_back({"layer_" + std::to_string(i) + ".attn", "attention", ap, af, size_t(ap*4)});
        int64_t fp = 2LL*hidden*inter+hidden, ff = estimate_linear_flops(hidden, inter)+estimate_linear_flops(inter, hidden);
        r.layers.push_back({"layer_" + std::to_string(i) + ".ffn", "linear", fp, ff, size_t(fp*4)});
        r.layers.push_back({"layer_" + std::to_string(i) + ".norm", "norm", hidden*2, int64_t(hidden*5), size_t(hidden*2*4)});
    }
    r.layers.push_back({"lm_head", "linear", int64_t(hidden)*vocab, estimate_linear_flops(hidden, vocab), size_t(hidden*vocab*4)});

    r.layer_count = (int)r.layers.size();
    for (const auto& l : r.layers) { r.param_count += l.param_count; r.total_flops += l.flops; r.memory_bytes += l.memory_bytes; }
    r.summary = format_report(r);
    spdlog::info("Model {}: {} layers, {}B params, {} GFLOPs", config.id, r.layer_count, r.param_count/1e9, r.total_flops/1e9);
    return r;
}

int64_t ModelAnalyzer::estimate_linear_flops(int in, int out, int batch) { return 2LL*batch*in*out; }
int64_t ModelAnalyzer::estimate_attention_flops(int h, int heads, int seq) {
    int hd = h/heads;
    return 2LL*seq*seq*hd*heads*2 + 4LL*2LL*seq*h*h;
}

std::string ModelAnalyzer::format_report(const ModelReport& r) {
    std::ostringstream os;
    os << "Model: " << r.model_id << "\nLayers: " << r.layer_count
       << "\nParams: " << std::fixed << std::setprecision(1) << r.param_count/1e9 << "B"
       << "\nFLOPs: " << r.total_flops/1e9 << " G\nMemory: " << r.memory_bytes/(1024.0*1024.0) << " MB\n";
    return os.str();
}

}  // namespace prodxcloud::analysis
