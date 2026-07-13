#include "storage/model_store.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace prodxcloud::storage {

ModelStore::ModelStore(const std::string& storage_path) : storage_path_(storage_path) {}

ModelStore::~ModelStore() = default;

Result<void> ModelStore::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    artifacts_by_model_.clear();
    spdlog::info("ModelStore initialized in memory (path hint: {})", storage_path_);
    return {};
}

Result<void> ModelStore::save_artifact(const ModelArtifact& artifact) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& artifacts = artifacts_by_model_[artifact.model_id];

    auto it = std::find_if(artifacts.begin(), artifacts.end(), [&](const ModelArtifact& current) {
        return current.version == artifact.version;
    });

    if (it != artifacts.end()) {
        *it = artifact;
    } else {
        artifacts.push_back(artifact);
    }

    spdlog::info("Saved artifact {}:{}", artifact.model_id, artifact.version);
    return {};
}

Result<ModelArtifact> ModelStore::get_artifact(const std::string& model_id,
                                               const std::string& version) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto model_it = artifacts_by_model_.find(model_id);
    if (model_it == artifacts_by_model_.end()) {
        return std::unexpected(Error::not_found("Artifact not found: " + model_id + ":" + version));
    }

    const auto& artifacts = model_it->second;
    auto it = std::find_if(artifacts.begin(), artifacts.end(), [&](const ModelArtifact& current) {
        return current.version == version;
    });

    if (it == artifacts.end()) {
        return std::unexpected(Error::not_found("Artifact not found: " + model_id + ":" + version));
    }

    return *it;
}

Result<std::vector<ModelArtifact>> ModelStore::list_artifacts(const std::string& model_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto model_it = artifacts_by_model_.find(model_id);
    if (model_it == artifacts_by_model_.end()) {
        return std::vector<ModelArtifact>{};
    }

    return model_it->second;
}

Result<ModelArtifact> ModelStore::get_latest(const std::string& model_id) const {
    auto artifacts = list_artifacts(model_id);
    if (!artifacts || artifacts->empty()) {
        return std::unexpected(Error::not_found("No artifacts for model: " + model_id));
    }

    return artifacts->back();
}

Result<void> ModelStore::delete_artifact(const std::string& model_id,
                                         const std::string& version) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto model_it = artifacts_by_model_.find(model_id);
    if (model_it == artifacts_by_model_.end()) {
        return {};
    }

    auto& artifacts = model_it->second;
    artifacts.erase(
        std::remove_if(artifacts.begin(), artifacts.end(), [&](const ModelArtifact& current) {
            return current.version == version;
        }),
        artifacts.end());

    if (artifacts.empty()) {
        artifacts_by_model_.erase(model_it);
    }

    return {};
}

Result<void> ModelStore::update_status(const std::string& model_id,
                                       const std::string& version,
                                       const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto model_it = artifacts_by_model_.find(model_id);
    if (model_it == artifacts_by_model_.end()) {
        return std::unexpected(Error::not_found("Artifact not found: " + model_id + ":" + version));
    }

    auto& artifacts = model_it->second;
    auto it = std::find_if(artifacts.begin(), artifacts.end(), [&](const ModelArtifact& current) {
        return current.version == version;
    });

    if (it == artifacts.end()) {
        return std::unexpected(Error::not_found("Artifact not found: " + model_id + ":" + version));
    }

    it->status = status;
    return {};
}

std::string ModelStore::make_key(const std::string& model_id, const std::string& version) {
    return model_id + ":" + version;
}

}  // namespace prodxcloud::storage
