#include "api/handlers/deployment_handler.hpp"
#include "api/middleware/tenant_middleware.hpp"
#include "common/types.hpp"
#include "common/uuid.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <vector>
#include <algorithm>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;

// ─── In-memory deployment store ─────────────────────────────────────────────

static std::mutex& deployment_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<Deployment>& deployment_store() {
    static std::vector<Deployment> store;
    return store;
}

static json deployment_to_json(const Deployment& d) {
    return {
        {"id", d.id},
        {"tenant_id", d.tenant_id},
        {"model_id", d.model_id},
        {"name", d.name},
        {"endpoint_url", d.endpoint_url},
        {"region", d.region},
        {"status", d.status},
        {"tokens_used", d.tokens_used},
        {"requests_per_min", d.requests_per_min},
        {"cost_usd", d.cost_usd},
        {"sku", d.sku},
        {"config", d.config_json},
        {"created_at", d.created_at}
    };
}

// ─── Handlers ───────────────────────────────────────────────────────────────

crow::response DeploymentHandler::list_deployments(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    auto status_filter = req.url_params.get("status");
    auto model_filter = req.url_params.get("model_id");

    std::lock_guard<std::mutex> lock(deployment_mutex());
    json arr = json::array();

    for (const auto& d : deployment_store()) {
        if (d.tenant_id != *tenant) continue;
        if (status_filter && d.status != std::string(status_filter)) continue;
        if (model_filter && d.model_id != std::string(model_filter)) continue;
        arr.push_back(deployment_to_json(d));
    }

    return crow::response(200, json{{"deployments", arr}, {"count", arr.size()}}.dump());
}

crow::response DeploymentHandler::deployment_summary(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(deployment_mutex());

    int total = 0, running = 0, stopped = 0, provisioning = 0, failed = 0;
    double total_cost = 0.0;

    for (const auto& d : deployment_store()) {
        if (d.tenant_id != *tenant) continue;
        ++total;
        if (d.status == "running") ++running;
        else if (d.status == "stopped") ++stopped;
        else if (d.status == "provisioning") ++provisioning;
        else if (d.status == "failed") ++failed;
        total_cost += d.cost_usd;
    }

    return crow::response(200, json{
        {"total", total},
        {"running", running},
        {"stopped", stopped},
        {"provisioning", provisioning},
        {"failed", failed},
        {"total_cost_usd", total_cost}
    }.dump());
}

crow::response DeploymentHandler::get_deployment(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(deployment_mutex());

    auto it = std::find_if(deployment_store().begin(), deployment_store().end(),
        [&](const Deployment& d) { return d.id == id && d.tenant_id == *tenant; });

    if (it == deployment_store().end())
        return crow::response(404, json{{"error", "Deployment not found"}}.dump());

    return crow::response(200, deployment_to_json(*it).dump());
}

crow::response DeploymentHandler::create_deployment(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("model_id") || !body.contains("name"))
        return crow::response(400, json{{"error", "model_id and name are required"}}.dump());

    Deployment d;
    d.id = generate_uuid();
    d.tenant_id = *tenant;
    d.model_id = body.value("model_id", "");
    d.name = body.value("name", "");
    d.endpoint_url = body.value("endpoint_url", "");
    d.region = body.value("region", "us-east-1");
    d.status = "provisioning";
    d.sku = body.value("sku", "standard");
    d.config_json = body.contains("config") ? body["config"].dump() : "{}";
    d.created_at = now_iso8601();

    std::lock_guard<std::mutex> lock(deployment_mutex());
    deployment_store().push_back(d);

    spdlog::info("Created deployment {} for tenant {}", d.id, d.tenant_id);
    return crow::response(201, deployment_to_json(d).dump());
}

crow::response DeploymentHandler::update_deployment_status(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("status"))
        return crow::response(400, json{{"error", "status is required"}}.dump());

    std::string new_status = body["status"];
    if (new_status != "running" && new_status != "stopped" &&
        new_status != "provisioning" && new_status != "failed")
        return crow::response(400, json{{"error", "Invalid status. Must be: running, stopped, provisioning, failed"}}.dump());

    std::lock_guard<std::mutex> lock(deployment_mutex());

    auto it = std::find_if(deployment_store().begin(), deployment_store().end(),
        [&](const Deployment& d) { return d.id == id && d.tenant_id == *tenant; });

    if (it == deployment_store().end())
        return crow::response(404, json{{"error", "Deployment not found"}}.dump());

    it->status = new_status;
    spdlog::info("Updated deployment {} status to {}", id, new_status);
    return crow::response(200, deployment_to_json(*it).dump());
}

}  // namespace prodxcloud::api::handlers
