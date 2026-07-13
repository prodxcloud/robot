#pragma once

/// @file cpu_backend.hpp
/// @brief CPU backend with AVX2 SIMD and OpenMP parallelism.

#include "inference/backends/backend_interface.hpp"

namespace prodxcloud::inference {

class CpuBackend : public BackendInterface {
public:
    CpuBackend();
    [[nodiscard]] std::string name() const override { return "cpu"; }
    [[nodiscard]] bool is_available() const override { return true; }

    void matmul(const float* A, const float* B, float* C, int M, int K, int N) override;
    void softmax(const float* input, float* output, int rows, int cols) override;
    void relu(const float* input, float* output, int size) override;
    void layer_norm(const float* input, const float* gamma, const float* beta,
                    float* output, int batch_size, int hidden_size, float eps) override;

private:
    bool avx2_available_;
};

}  // namespace prodxcloud::inference
