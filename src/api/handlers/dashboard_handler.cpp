#include "api/handlers/dashboard_handler.hpp"
#include "api/middleware/tenant_middleware.hpp"
#include "common/types.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;

// The dashboard handler aggregates counts from all the other stores.
// We access the stores via extern declarations to the static store functions
// in the other handler cpps. However, since those are file-static, we instead
// provide a summary with configurable counts that can be set by the server
// or we return a placeholder that the frontend populates via individual API calls.

crow::response DashboardHandler::summary(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    // Return summary structure - in production this would aggregate from stores/DB
    // For now we return the structure the frontend expects
    json resp = {
        {"tenant_id", *tenant},
        {"agents", {
            {"total", 0},
            {"active", 0}
        }},
        {"models", {
            {"total", 0},
            {"loaded", 0}
        }},
        {"deployments", {
            {"total", 0},
            {"running", 0},
            {"stopped", 0}
        }},
        {"evals", {
            {"total", 0},
            {"completed", 0},
            {"pending", 0}
        }},
        {"fine_tuning", {
            {"total", 0},
            {"running", 0}
        }},
        {"training", {
            {"total", 0},
            {"running", 0}
        }},
        {"knowledge_bases", {
            {"total", 0}
        }},
        {"tools", {
            {"total", 0}
        }},
        {"workflows", {
            {"total", 0}
        }},
        {"web_assets", {
            {"total", 0}
        }},
        {"feedback", {
            {"total", 0},
            {"positive", 0},
            {"negative", 0}
        }}
    };

    return crow::response(200, resp.dump());
}

}  // namespace prodxcloud::api::handlers
