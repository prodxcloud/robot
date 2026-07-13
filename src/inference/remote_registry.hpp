#pragma once
/// @file remote_registry.hpp
/// @brief Lightweight registry that tracks remote model endpoints served by SLM-Models.
///        This service does NOT load or run models locally — all model execution
///        is delegated to the SLM-Models Python service.

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "common/types.hpp"

namespace prodxcloud::inference {

struct RemoteModelEntry {
    std::string id;
    std::string name;
    std::string provider;       // e.g. "slm-models", "openai", "anthropic", "ollama"
    std::string endpoint;       // base URL of the remote service
    std::string description;
    bool available = true;
};

/// Registry of remote model endpoints. No weights, no backends, no local inference.
class RemoteModelRegistry {
public:
    explicit RemoteModelRegistry(const std::string& slm_service_url = "")
        : slm_service_url_(slm_service_url) {}

    void set_slm_service_url(const std::string& url) {
        std::lock_guard lock(mu_);
        slm_service_url_ = url;
    }

    std::string slm_service_url() const {
        std::lock_guard lock(mu_);
        return slm_service_url_;
    }

    void register_model(const RemoteModelEntry& entry) {
        std::lock_guard lock(mu_);
        models_[entry.id] = entry;
    }

    std::vector<RemoteModelEntry> list_models() const {
        std::lock_guard lock(mu_);
        std::vector<RemoteModelEntry> out;
        out.reserve(models_.size());
        for (const auto& [_, m] : models_) out.push_back(m);
        return out;
    }

    bool has_model(const std::string& id) const {
        std::lock_guard lock(mu_);
        return models_.count(id) > 0;
    }

private:
    mutable std::mutex mu_;
    std::string slm_service_url_;
    std::unordered_map<std::string, RemoteModelEntry> models_;
};

}  // namespace prodxcloud::inference
