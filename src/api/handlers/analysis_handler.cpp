/// @file analysis_handler.cpp
/// @brief Analysis endpoints. Model-dependent benchmarking and evaluation are
///        delegated to the SLM-Models Python service. Drift detection (pure math)
///        runs locally.

#include "api/handlers/analysis_handler.hpp"
#include "analysis/drift_detector.hpp"
#include "api/middleware/tenant_middleware.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;

static json slm_delegation(const std::string& feature) {
    return {
        {"error", feature + " requires model execution — delegated to SLM-Models service"},
        {"service", "SLM-Models"},
        {"docs", "See SLM-Models/README.md"}
    };
}

crow::response AnalysisHandler::benchmark(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    return crow::response(501, slm_delegation("Inference benchmarking").dump());
}

crow::response AnalysisHandler::evaluate(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    return crow::response(501, slm_delegation("Model evaluation").dump());
}

crow::response AnalysisHandler::drift_detect(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("baseline") || !body.contains("current"))
        return crow::response(400, json{{"error", "baseline and current arrays required"}}.dump());

    std::vector<double> baseline = body["baseline"].get<std::vector<double>>();
    std::vector<double> current  = body["current"].get<std::vector<double>>();

    analysis::DriftConfig cfg;
    cfg.model_id = body.value("model_id", "unknown");
    cfg.num_bins  = body.value("num_bins", 10);

    analysis::DriftDetector detector;
    auto result = detector.detect(baseline, current, cfg);
    if (!result)
        return crow::response(500, json{{"error", result.error().message}}.dump());

    const auto& r = *result;
    return crow::response(200, json{
        {"model_id",      r.model_id},
        {"psi_score",     r.psi_score},
        {"kl_divergence", r.kl_divergence},
        {"js_divergence", r.js_divergence},
        {"severity",      static_cast<int>(r.severity)},
        {"summary",       r.summary}
    }.dump());
}

}  // namespace prodxcloud::api::handlers
