#include "inference/backends/cpu_backend.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__AVX2__)
#include <immintrin.h>
#define HAS_AVX2 1
#else
#define HAS_AVX2 0
#endif

namespace prodxcloud::inference {

CpuBackend::CpuBackend() : avx2_available_(HAS_AVX2) {}

void CpuBackend::matmul(const float* A, const float* B, float* C, int M, int K, int N) {
    std::memset(C, 0, M * N * sizeof(float));
#if HAS_AVX2
    if (avx2_available_ && N >= 8) {
        #pragma omp parallel for schedule(dynamic) if(M > 4)
        for (int i = 0; i < M; ++i)
            for (int k = 0; k < K; ++k) {
                __m256 a_val = _mm256_set1_ps(A[i * K + k]);
                int j = 0;
                for (; j + 8 <= N; j += 8) {
                    __m256 b = _mm256_loadu_ps(&B[k * N + j]);
                    __m256 c = _mm256_loadu_ps(&C[i * N + j]);
                    _mm256_storeu_ps(&C[i * N + j], _mm256_fmadd_ps(a_val, b, c));
                }
                for (; j < N; ++j) C[i * N + j] += A[i * K + k] * B[k * N + j];
            }
    } else
#endif
    {
        #pragma omp parallel for schedule(dynamic) if(M > 4)
        for (int i = 0; i < M; ++i)
            for (int k = 0; k < K; ++k) {
                float a = A[i * K + k];
                for (int j = 0; j < N; ++j) C[i * N + j] += a * B[k * N + j];
            }
    }
}

void CpuBackend::softmax(const float* input, float* output, int rows, int cols) {
    #pragma omp parallel for if(rows > 4)
    for (int i = 0; i < rows; ++i) {
        const float* ri = input + i * cols;
        float* ro       = output + i * cols;
        float mx        = *std::max_element(ri, ri + cols);
        float sum       = 0.0f;
        for (int j = 0; j < cols; ++j) { ro[j] = std::exp(ri[j] - mx); sum += ro[j]; }
        float inv = 1.0f / sum;
        for (int j = 0; j < cols; ++j) ro[j] *= inv;
    }
}

void CpuBackend::relu(const float* input, float* output, int size) {
    for (int i = 0; i < size; ++i) output[i] = std::max(0.0f, input[i]);
}

void CpuBackend::layer_norm(const float* input, const float* gamma, const float* beta,
                             float* output, int batch_size, int hidden_size, float eps) {
    #pragma omp parallel for if(batch_size > 4)
    for (int b = 0; b < batch_size; ++b) {
        const float* x = input + b * hidden_size;
        float* y       = output + b * hidden_size;
        double mean = 0.0;
        for (int i = 0; i < hidden_size; ++i) mean += x[i];
        mean /= hidden_size;
        double var = 0.0;
        for (int i = 0; i < hidden_size; ++i) { double d = x[i] - mean; var += d * d; }
        var /= hidden_size;
        double inv_std = 1.0 / std::sqrt(var + eps);
        for (int i = 0; i < hidden_size; ++i)
            y[i] = static_cast<float>((x[i] - mean) * inv_std * gamma[i] + beta[i]);
    }
}

}  // namespace prodxcloud::inference
