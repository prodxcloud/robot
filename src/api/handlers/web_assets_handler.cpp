#include "api/handlers/web_assets_handler.hpp"
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

// ─── In-memory web assets store ─────────────────────────────────────────────

static std::mutex& wa_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<WebAsset>& wa_store() {
    static std::vector<WebAsset> store;
    return store;
}

static json wa_to_json(const WebAsset& w) {
    return {
        {"id", w.id},
        {"name", w.name},
        {"type", w.type},
        {"agent_id", w.agent_id},
        {"config", json::parse(w.config_json, nullptr, false)},
        {"views", w.views},
        {"interactions", w.interactions},
        {"status", w.status},
        {"created_at", w.created_at}
    };
}

// ─── Handlers ───────────────────────────────────────────────────────────────

crow::response WebAssetsHandler::list_web_assets(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(wa_mutex());
    json arr = json::array();

    for (const auto& w : wa_store())
        arr.push_back(wa_to_json(w));

    return crow::response(200, json{{"web_assets", arr}, {"count", arr.size()}}.dump());
}

crow::response WebAssetsHandler::get_web_asset(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(wa_mutex());

    auto it = std::find_if(wa_store().begin(), wa_store().end(),
        [&](const WebAsset& w) { return w.id == id; });

    if (it == wa_store().end())
        return crow::response(404, json{{"error", "Web asset not found"}}.dump());

    return crow::response(200, wa_to_json(*it).dump());
}

crow::response WebAssetsHandler::create_web_asset(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("name"))
        return crow::response(400, json{{"error", "name is required"}}.dump());

    std::string asset_type = body.value("type", "chatbot");
    if (asset_type != "chatbot" && asset_type != "embed" && asset_type != "api")
        return crow::response(400, json{{"error", "type must be: chatbot, embed, or api"}}.dump());

    WebAsset asset;
    asset.id = generate_uuid();
    asset.name = body.value("name", "");
    asset.type = asset_type;
    asset.agent_id = body.value("agent_id", "");
    asset.config_json = body.contains("config") ? body["config"].dump() : "{}";
    asset.status = "active";
    asset.created_at = now_iso8601();

    std::lock_guard<std::mutex> lock(wa_mutex());
    wa_store().push_back(asset);

    spdlog::info("Created web asset {}: {}", asset.id, asset.name);
    return crow::response(201, wa_to_json(asset).dump());
}

crow::response WebAssetsHandler::delete_web_asset(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(wa_mutex());

    auto it = std::find_if(wa_store().begin(), wa_store().end(),
        [&](const WebAsset& w) { return w.id == id; });

    if (it == wa_store().end())
        return crow::response(404, json{{"error", "Web asset not found"}}.dump());

    std::string deleted_id = it->id;
    wa_store().erase(it);

    spdlog::info("Deleted web asset {}", deleted_id);
    return crow::response(200, json{{"status", "deleted"}, {"id", deleted_id}}.dump());
}

}  // namespace prodxcloud::api::handlers
