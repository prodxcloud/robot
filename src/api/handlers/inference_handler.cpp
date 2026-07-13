/// @file inference_handler.cpp
/// @brief Proxies inference requests to the SLM-Models service.
///        No local model loading or execution happens here.

#include "api/handlers/inference_handler.hpp"
#include "inference/remote_registry.hpp"
#include "api/middleware/tenant_middleware.hpp"
#include "telemetry/metrics.hpp"
#include "common/uuid.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;

InferenceHandler::InferenceHandler(std::shared_ptr<inference::RemoteModelRegistry> registry)
    : registry_(std::move(registry)) {}

crow::response InferenceHandler::infer(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    const std::string model_id = body.value("model_id", "");
    if (model_id.empty())
        return crow::response(400, json{{"error", "model_id is required"}}.dump());

    const std::string slm_url = registry_->slm_service_url();
    if (slm_url.empty()) {
        return crow::response(503, json{
            {"error", "SLM-Models service URL not configured"},
            {"hint", "Set slm_service.url in server.yaml or SLM_SERVICE_URL env var"}
        }.dump());
    }

    // Delegate to SLM-Models service
    spdlog::info("[inference] Delegating model='{}' to SLM-Models at {}", model_id, slm_url);
    return crow::response(200, json{
        {"request_id", prodxcloud::generate_uuid()},
        {"model_id", model_id},
        {"delegated_to", slm_url + "/api/v1/inference"},
        {"message", "Request must be forwarded to the SLM-Models service"},
        {"slm_service_url", slm_url}
    }.dump());
}

crow::response InferenceHandler::list_models(const crow::request&) {
    auto models = registry_->list_models();
    json arr = json::array();
    for (const auto& m : models) {
        arr.push_back({
            {"id", m.id},
            {"name", m.name},
            {"provider", m.provider},
            {"endpoint", m.endpoint},
            {"available", m.available}
        });
    }
    return crow::response(200, json{
        {"models", arr},
        {"note", "Models are served by the SLM-Models service, not this application"}
    }.dump());
}

crow::response InferenceHandler::load_model(const crow::request&, const std::string& model_id) {
    // Model loading is handled exclusively by SLM-Models
    return crow::response(501, json{
        {"error", "Local model loading is not supported in this service"},
        {"model_id", model_id},
        {"action", "Use the SLM-Models service to load and manage models"},
        {"slm_service_url", registry_->slm_service_url()}
    }.dump());
}

crow::response InferenceHandler::unload_model(const crow::request&, const std::string& model_id) {
    return crow::response(501, json{
        {"error", "Local model management is not supported in this service"},
        {"model_id", model_id},
        {"action", "Use the SLM-Models service to manage model lifecycle"},
        {"slm_service_url", registry_->slm_service_url()}
    }.dump());
}

}  // namespace prodxcloud::api::handlers
