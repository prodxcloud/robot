#pragma once
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/types.hpp"

namespace prodxcloud::storage {

class ModelStore {
public:
    explicit ModelStore(const std::string& storage_path);
    ~ModelStore();

    ModelStore(const ModelStore&) = delete;
    ModelStore& operator=(const ModelStore&) = delete;

    Result<void> init();
    Result<void> save_artifact(const ModelArtifact& artifact);
    Result<ModelArtifact> get_artifact(const std::string& model_id, const std::string& version) const;
    Result<std::vector<ModelArtifact>> list_artifacts(const std::string& model_id) const;
    Result<ModelArtifact> get_latest(const std::string& model_id) const;
    Result<void> delete_artifact(const std::string& model_id, const std::string& version);
    Result<void> update_status(const std::string& model_id, const std::string& version,
                               const std::string& status);

private:
    std::string storage_path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<ModelArtifact>> artifacts_by_model_;

    static std::string make_key(const std::string& model_id, const std::string& version);
};

}  // namespace prodxcloud::storage
