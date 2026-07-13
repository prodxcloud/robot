#include "inference/quantization.hpp"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace prodxcloud::inference {

Result<QuantizedWeights> QuantizationPipeline::quantize_fp32_to_int8(
    const std::vector<float>& w, int block_size) {
    if (w.empty()) return std::unexpected(Error::bad_request("Empty weights"));
    size_t nb = (w.size() + block_size - 1) / block_size;
    QuantizedWeights qw;
    qw.data.resize(w.size()); qw.scales.resize(nb); qw.zero_points.resize(nb, 0.0f);
    qw.block_size = block_size; qw.bits = 8; qw.original_size = w.size();
    for (size_t b = 0; b < nb; ++b) {
        size_t s = b * block_size, e = std::min(s + block_size, w.size());
        float am = 0.0f;
        for (size_t i = s; i < e; ++i) am = std::max(am, std::abs(w[i]));
        float scale = am > 0.0f ? am / 127.0f : 1.0f;
        qw.scales[b] = scale;
        for (size_t i = s; i < e; ++i) {
            int32_t q = std::clamp(static_cast<int32_t>(std::round(w[i] / scale)), -127, 127);
            qw.data[i] = static_cast<uint8_t>(q + 128);
        }
    }
    return qw;
}

Result<QuantizedWeights> QuantizationPipeline::quantize_fp32_to_q4(
    const std::vector<float>& w, int block_size) {
    if (w.empty()) return std::unexpected(Error::bad_request("Empty weights"));
    size_t nb = (w.size() + block_size - 1) / block_size;
    QuantizedWeights qw;
    qw.data.resize((w.size() + 1) / 2); qw.scales.resize(nb); qw.zero_points.resize(nb);
    qw.block_size = block_size; qw.bits = 4; qw.original_size = w.size();
    for (size_t b = 0; b < nb; ++b) {
        size_t s = b * block_size, e = std::min(s + block_size, w.size());
        float mn = *std::min_element(w.begin() + s, w.begin() + e);
        float mx = *std::max_element(w.begin() + s, w.begin() + e);
        float sc = (mx - mn) / 15.0f;
        float zp = -mn / (sc > 0.0f ? sc : 1.0f);
        qw.scales[b] = sc > 0.0f ? sc : 1.0f; qw.zero_points[b] = zp;
        for (size_t i = s; i < e; ++i) {
            int32_t q = std::clamp(static_cast<int32_t>(std::round(w[i] / qw.scales[b] + zp)), 0, 15);
            size_t bi = i / 2;
            if (i % 2 == 0) qw.data[bi] = static_cast<uint8_t>(q & 0x0F);
            else qw.data[bi] |= static_cast<uint8_t>((q & 0x0F) << 4);
        }
    }
    return qw;
}

std::vector<float> QuantizationPipeline::dequantize_int8(const QuantizedWeights& qw) {
    std::vector<float> r(qw.original_size);
    for (size_t i = 0; i < qw.original_size; ++i)
        r[i] = (static_cast<int32_t>(qw.data[i]) - 128) * qw.scales[i / qw.block_size];
    return r;
}

std::vector<float> QuantizationPipeline::dequantize_q4(const QuantizedWeights& qw) {
    std::vector<float> r(qw.original_size);
    for (size_t i = 0; i < qw.original_size; ++i) {
        size_t b = i / qw.block_size, bi = i / 2;
        int32_t q = (i % 2 == 0) ? (qw.data[bi] & 0x0F) : ((qw.data[bi] >> 4) & 0x0F);
        r[i] = (q - qw.zero_points[b]) * qw.scales[b];
    }
    return r;
}

Result<double> QuantizationPipeline::validate_accuracy(const std::vector<float>& orig,
                                                        const std::vector<float>& deq,
                                                        double max_mse) {
    if (orig.size() != deq.size()) return std::unexpected(Error::bad_request("Size mismatch"));
    double mse = 0.0;
    for (size_t i = 0; i < orig.size(); ++i) { double d = orig[i] - deq[i]; mse += d * d; }
    mse /= orig.size();
    if (mse > max_mse)
        return std::unexpected(Error::internal("MSE " + std::to_string(mse) + " > threshold " + std::to_string(max_mse)));
    return mse;
}

}  // namespace prodxcloud::inference
