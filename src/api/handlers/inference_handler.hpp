#pragma once
/// @brief Inference handler — proxies all model execution to the SLM-Models service.
///        This application does NOT run models locally.
#include <memory>
#include <crow.h>

namespace prodxcloud::inference { class RemoteModelRegistry; }

namespace prodxcloud::api::handlers {

class InferenceHandler {
public:
    explicit InferenceHandler(std::shared_ptr<inference::RemoteModelRegistry> registry);

    crow::response infer(const crow::request& req);
    crow::response list_models(const crow::request& req);
    crow::response load_model(const crow::request& req, const std::string& model_id);
    crow::response unload_model(const crow::request& req, const std::string& model_id);

private:
    std::shared_ptr<inference::RemoteModelRegistry> registry_;
};

}  // namespace prodxcloud::api::handlers
