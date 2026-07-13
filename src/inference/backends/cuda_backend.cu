/// @file cuda_backend.cu
/// @brief CUDA kernels and cuBLAS integration for GPU inference.

#ifdef PRODXCLOUD_CUDA_ENABLED

#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <cmath>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include "inference/backends/cuda_backend.hpp"

namespace prodxcloud::inference {

#define CUDA_CHECK(call) do {                                              \
    cudaError_t err = (call);                                               \
    if (err != cudaSuccess) {                                               \
        spdlog::error("CUDA error at {}:{}: {}", __FILE__, __LINE__,       \
                     cudaGetErrorString(err));                              \
        throw std::runtime_error(std::string("CUDA: ") + cudaGetErrorString(err)); \
    }                                                                       \
} while (0)

#define CUBLAS_CHECK(call) do {                                            \
    cublasStatus_t s = (call);                                              \
    if (s != CUBLAS_STATUS_SUCCESS) {                                       \
        throw std::runtime_error("cuBLAS error: " + std::to_string(s));    \
    }                                                                       \
} while (0)

__global__ void softmax_kernel(const float* in, float* out, int rows, int cols) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= rows) return;
    const float* ri = in + row * cols;
    float* ro       = out + row * cols;
    float mx = ri[0];
    for (int j = 1; j < cols; ++j) mx = fmaxf(mx, ri[j]);
    float sum = 0.0f;
    for (int j = 0; j < cols; ++j) { ro[j] = expf(ri[j] - mx); sum += ro[j]; }
    float inv = 1.0f / sum;
    for (int j = 0; j < cols; ++j) ro[j] *= inv;
}

__global__ void relu_kernel(const float* in, float* out, int size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < size) out[idx] = fmaxf(0.0f, in[idx]);
}

__global__ void layer_norm_kernel(const float* in, const float* gamma, const float* beta,
                                   float* out, int hidden, float eps) {
    int b = blockIdx.x;
    const float* x = in + b * hidden;
    float* y       = out + b * hidden;
    float mean = 0.0f;
    for (int i = 0; i < hidden; ++i) mean += x[i];
    mean /= hidden;
    float var = 0.0f;
    for (int i = 0; i < hidden; ++i) { float d = x[i] - mean; var += d * d; }
    var /= hidden;
    float inv = rsqrtf(var + eps);
    for (int i = threadIdx.x; i < hidden; i += blockDim.x)
        y[i] = (x[i] - mean) * inv * gamma[i] + beta[i];
}

struct CudaBackend::Impl {
    cublasHandle_t cublas = nullptr;
    cudaStream_t stream   = nullptr;
    int device_id         = 0;
    cudaDeviceProp props;

    Impl() {
        CUDA_CHECK(cudaGetDevice(&device_id));
        CUDA_CHECK(cudaGetDeviceProperties(&props, device_id));
        CUDA_CHECK(cudaStreamCreate(&stream));
        CUBLAS_CHECK(cublasCreate(&cublas));
        CUBLAS_CHECK(cublasSetStream(cublas, stream));
        spdlog::info("CUDA: {} (compute {}.{}), {} MB VRAM",
                     props.name, props.major, props.minor,
                     props.totalGlobalMem / (1024 * 1024));
    }
    ~Impl() { if (cublas) cublasDestroy(cublas); if (stream) cudaStreamDestroy(stream); }
};

CudaBackend::CudaBackend() : impl_(std::make_unique<Impl>()) {}
CudaBackend::~CudaBackend() = default;

bool CudaBackend::is_available() const {
    int cnt = 0; return cudaGetDeviceCount(&cnt) == cudaSuccess && cnt > 0;
}

void CudaBackend::matmul(const float* A, const float* B, float* C, int M, int K, int N) {
    float *dA, *dB, *dC;
    CUDA_CHECK(cudaMalloc(&dA, M*K*sizeof(float)));
    CUDA_CHECK(cudaMalloc(&dB, K*N*sizeof(float)));
    CUDA_CHECK(cudaMalloc(&dC, M*N*sizeof(float)));
    CUDA_CHECK(cudaMemcpyAsync(dA, A, M*K*sizeof(float), cudaMemcpyHostToDevice, impl_->stream));
    CUDA_CHECK(cudaMemcpyAsync(dB, B, K*N*sizeof(float), cudaMemcpyHostToDevice, impl_->stream));
    float alpha = 1.0f, beta = 0.0f;
    CUBLAS_CHECK(cublasSgemm(impl_->cublas, CUBLAS_OP_N, CUBLAS_OP_N,
                             N, M, K, &alpha, dB, N, dA, K, &beta, dC, N));
    CUDA_CHECK(cudaMemcpyAsync(C, dC, M*N*sizeof(float), cudaMemcpyDeviceToHost, impl_->stream));
    CUDA_CHECK(cudaStreamSynchronize(impl_->stream));
    cudaFree(dA); cudaFree(dB); cudaFree(dC);
}

void CudaBackend::softmax(const float* in, float* out, int rows, int cols) {
    float *di, *d_o; size_t sz = rows*cols*sizeof(float);
    CUDA_CHECK(cudaMalloc(&di, sz)); CUDA_CHECK(cudaMalloc(&d_o, sz));
    CUDA_CHECK(cudaMemcpyAsync(di, in, sz, cudaMemcpyHostToDevice, impl_->stream));
    softmax_kernel<<<(rows+255)/256, 256, 0, impl_->stream>>>(di, d_o, rows, cols);
    CUDA_CHECK(cudaMemcpyAsync(out, d_o, sz, cudaMemcpyDeviceToHost, impl_->stream));
    CUDA_CHECK(cudaStreamSynchronize(impl_->stream));
    cudaFree(di); cudaFree(d_o);
}

void CudaBackend::relu(const float* in, float* out, int size) {
    float *di, *d_o; size_t sz = size*sizeof(float);
    CUDA_CHECK(cudaMalloc(&di, sz)); CUDA_CHECK(cudaMalloc(&d_o, sz));
    CUDA_CHECK(cudaMemcpyAsync(di, in, sz, cudaMemcpyHostToDevice, impl_->stream));
    relu_kernel<<<(size+255)/256, 256, 0, impl_->stream>>>(di, d_o, size);
    CUDA_CHECK(cudaMemcpyAsync(out, d_o, sz, cudaMemcpyDeviceToHost, impl_->stream));
    CUDA_CHECK(cudaStreamSynchronize(impl_->stream));
    cudaFree(di); cudaFree(d_o);
}

void CudaBackend::layer_norm(const float* in, const float* gamma, const float* beta,
                              float* out, int batch, int hidden, float eps) {
    float *di, *d_o, *dg, *db;
    size_t dsz = batch*hidden*sizeof(float), psz = hidden*sizeof(float);
    CUDA_CHECK(cudaMalloc(&di, dsz)); CUDA_CHECK(cudaMalloc(&d_o, dsz));
    CUDA_CHECK(cudaMalloc(&dg, psz)); CUDA_CHECK(cudaMalloc(&db, psz));
    CUDA_CHECK(cudaMemcpyAsync(di, in, dsz, cudaMemcpyHostToDevice, impl_->stream));
    CUDA_CHECK(cudaMemcpyAsync(dg, gamma, psz, cudaMemcpyHostToDevice, impl_->stream));
    CUDA_CHECK(cudaMemcpyAsync(db, beta, psz, cudaMemcpyHostToDevice, impl_->stream));
    int threads = std::min(hidden, 256);
    layer_norm_kernel<<<batch, threads, 0, impl_->stream>>>(di, dg, db, d_o, hidden, eps);
    CUDA_CHECK(cudaMemcpyAsync(out, d_o, dsz, cudaMemcpyDeviceToHost, impl_->stream));
    CUDA_CHECK(cudaStreamSynchronize(impl_->stream));
    cudaFree(di); cudaFree(d_o); cudaFree(dg); cudaFree(db);
}

std::string CudaBackend::device_info() const {
    return std::string(impl_->props.name) + " (compute " +
           std::to_string(impl_->props.major) + "." + std::to_string(impl_->props.minor) +
           "), VRAM: " + std::to_string(impl_->props.totalGlobalMem / (1024*1024)) + " MB";
}

}  // namespace prodxcloud::inference
#endif
