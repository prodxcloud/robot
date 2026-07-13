#pragma once

/// @file cuda_backend.hpp
/// @brief CUDA backend for GPU-accelerated inference.

#ifdef PRODXCLOUD_CUDA_ENABLED

#include <memory>
#include "inference/backends/backend_interface.hpp"

namespace prodxcloud::inference {

class CudaBackend : public BackendInterface {
public:
    CudaBackend();
    ~CudaBackend() override;
    [[nodiscard]] std::string name() const override { return "cuda"; }
    [[nodiscard]] bool is_available() const override;

    void matmul(const float* A, const float* B, float* C, int M, int K, int N) override;
    void softmax(const float* input, float* output, int rows, int cols) override;
    void relu(const float* input, float* output, int size) override;
    void layer_norm(const float* input, const float* gamma, const float* beta,
                    float* output, int batch_size, int hidden_size, float eps) override;
    [[nodiscard]] std::string device_info() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace prodxcloud::inference
#endif
