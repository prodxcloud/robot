#pragma once

/// @file backend_interface.hpp
/// @brief Abstract interface for inference compute backends.

#include <string>

namespace prodxcloud::inference {

class BackendInterface {
public:
    virtual ~BackendInterface() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual bool is_available() const = 0;

    virtual void matmul(const float* A, const float* B, float* C, int M, int K, int N) = 0;
    virtual void softmax(const float* input, float* output, int rows, int cols)         = 0;
    virtual void relu(const float* input, float* output, int size)                      = 0;
    virtual void layer_norm(const float* input, const float* gamma, const float* beta,
                            float* output, int batch_size, int hidden_size, float eps = 1e-5f) = 0;
};

}  // namespace prodxcloud::inference
