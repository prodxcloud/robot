#pragma once

/// @file engine.hpp
/// @brief Core inference engine supporting GGUF and ONNX model formats.

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"
#include "inference/backends/backend_interface.hpp"

namespace prodxcloud::inference {

class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();

    InferenceEngine(const InferenceEngine&)            = delete;
    InferenceEngine& operator=(const InferenceEngine&) = delete;

    Result<void> load_model(const ModelConfig& config);
    Result<void> unload_model(const std::string& model_id);
    Result<InferenceResult> infer(const InferenceRequest& request);
    std::vector<std::string> loaded_models() const;
    std::vector<ModelConfig> list_models() const;
    bool is_model_loaded(const std::string& model_id) const;

private:
    struct LoadedModel {
        ModelConfig config;
        std::shared_ptr<BackendInterface> backend;
        std::vector<float> weights;
        size_t param_count = 0;
    };

    mutable std::shared_mutex models_mutex_;
    std::unordered_map<std::string, LoadedModel> loaded_models_;
    std::shared_ptr<BackendInterface> cpu_backend_;
#ifdef PRODXCLOUD_CUDA_ENABLED
    std::shared_ptr<BackendInterface> cuda_backend_;
#endif
    std::shared_ptr<BackendInterface> get_backend(const std::string& name) const;
};

}  // namespace prodxcloud::inference
