#include "inference/engine.hpp"
#include <chrono>
#include <fstream>
#include <spdlog/spdlog.h>
#include "inference/backends/cpu_backend.hpp"
#ifdef PRODXCLOUD_CUDA_ENABLED
#include "inference/backends/cuda_backend.hpp"
#endif

namespace prodxcloud::inference {

InferenceEngine::InferenceEngine() {
    cpu_backend_ = std::make_shared<CpuBackend>();
    spdlog::info("InferenceEngine initialized with CPU backend");
#ifdef PRODXCLOUD_CUDA_ENABLED
    try {
        cuda_backend_ = std::make_shared<CudaBackend>();
        if (cuda_backend_->is_available()) spdlog::info("CUDA backend available");
    } catch (const std::exception& e) { spdlog::warn("CUDA init failed: {}", e.what()); }
#endif
}

InferenceEngine::~InferenceEngine() {
    spdlog::info("InferenceEngine shutting down, {} models", loaded_models_.size());
}

Result<void> InferenceEngine::load_model(const ModelConfig& config) {
    std::ifstream file(config.path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return std::unexpected(Error::not_found("Model not found: " + config.path));
    auto file_size = file.tellg();
    file.close();
    auto backend = get_backend(config.backend);
    if (!backend) return std::unexpected(Error::bad_request("Unknown backend: " + config.backend));
    LoadedModel m{.config = config, .backend = backend, .weights = {},
                  .param_count = static_cast<size_t>(file_size / sizeof(float))};
    { std::unique_lock lock(models_mutex_); loaded_models_[config.id] = std::move(m); }
    spdlog::info("Loaded model '{}' (backend={}, size={} bytes)", config.id, config.backend, file_size);
    return {};
}

Result<void> InferenceEngine::unload_model(const std::string& mid) {
    std::unique_lock lock(models_mutex_);
    auto it = loaded_models_.find(mid);
    if (it == loaded_models_.end()) return std::unexpected(Error::not_found("Not loaded: " + mid));
    loaded_models_.erase(it);
    return {};
}

Result<InferenceResult> InferenceEngine::infer(const InferenceRequest& request) {
    auto start = Clock::now();
    std::shared_lock lock(models_mutex_);
    auto it = loaded_models_.find(request.model_id);
    if (it == loaded_models_.end())
        return std::unexpected(Error::not_found("Model not loaded: " + request.model_id));
    InferenceResult result;
    result.model_id         = request.model_id;
    result.request_id       = request.request_id;
    result.text             = "[Inference output for: " + request.prompt.substr(0, 50) + "...]";
    result.tokens_generated = std::min(request.max_tokens, int32_t{64});
    result.latency_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

std::vector<std::string> InferenceEngine::loaded_models() const {
    std::shared_lock lock(models_mutex_);
    std::vector<std::string> ids;
    for (const auto& [id, _] : loaded_models_) ids.push_back(id);
    return ids;
}

std::vector<ModelConfig> InferenceEngine::list_models() const {
    std::shared_lock lock(models_mutex_);
    std::vector<ModelConfig> configs;
    for (const auto& [id, m] : loaded_models_) configs.push_back(m.config);
    return configs;
}

bool InferenceEngine::is_model_loaded(const std::string& mid) const {
    std::shared_lock lock(models_mutex_);
    return loaded_models_.contains(mid);
}

std::shared_ptr<BackendInterface> InferenceEngine::get_backend(const std::string& n) const {
#ifdef PRODXCLOUD_CUDA_ENABLED
    if (n == "cuda" && cuda_backend_ && cuda_backend_->is_available()) return cuda_backend_;
#endif
    return cpu_backend_;
}

}  // namespace prodxcloud::inference
