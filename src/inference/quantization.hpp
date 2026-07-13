#pragma once

/// @file quantization.hpp
/// @brief Weight quantization pipeline: FP32 to INT8, FP32 to Q4.

#include <cstdint>
#include <vector>
#include "common/types.hpp"

namespace prodxcloud::inference {

struct QuantizedWeights {
    std::vector<uint8_t> data;
    std::vector<float> scales;
    std::vector<float> zero_points;
    int block_size;
    int bits;
    size_t original_size;
};

class QuantizationPipeline {
public:
    static Result<QuantizedWeights> quantize_fp32_to_int8(const std::vector<float>& weights, int block_size = 128);
    static Result<QuantizedWeights> quantize_fp32_to_q4(const std::vector<float>& weights, int block_size = 32);
    static std::vector<float> dequantize_int8(const QuantizedWeights& qw);
    static std::vector<float> dequantize_q4(const QuantizedWeights& qw);
    static Result<double> validate_accuracy(const std::vector<float>& original,
                                            const std::vector<float>& dequantized,
                                            double max_mse = 0.01);
};

}  // namespace prodxcloud::inference
