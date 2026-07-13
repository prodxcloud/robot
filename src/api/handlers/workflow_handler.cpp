#include "api/handlers/workflow_handler.hpp"
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

// ─── In-memory workflow store ───────────────────────────────────────────────

static std::mutex& wf_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<Workflow>& wf_store() {
    static std::vector<Workflow> store;
    return store;
}

static json wf_to_json(const Workflow& w) {
    return {
        {"id", w.id},
        {"name", w.name},
        {"description", w.description},
        {"status", w.status},
        {"trigger", w.trigger},
        {"steps", json::parse(w.steps_json, nullptr, false)},
        {"created_at", w.created_at}
    };
}

// ─── Handlers ───────────────────────────────────────────────────────────────

crow::response WorkflowHandler::list_workflows(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(wf_mutex());
    json arr = json::array();

    for (const auto& w : wf_store())
        arr.push_back(wf_to_json(w));

    return crow::response(200, json{{"workflows", arr}, {"count", arr.size()}}.dump());
}

crow::response WorkflowHandler::trigger_workflow(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::string workflow_id = body.value("workflow_id", "");

    // If workflow_id is provided, find and trigger it
    if (!workflow_id.empty()) {
        std::lock_guard<std::mutex> lock(wf_mutex());
        auto it = std::find_if(wf_store().begin(), wf_store().end(),
            [&](const Workflow& w) { return w.id == workflow_id; });

        if (it == wf_store().end())
            return crow::response(404, json{{"error", "Workflow not found"}}.dump());

        it->status = "running";
        spdlog::info("Triggered workflow {}", workflow_id);
        return crow::response(200, json{
            {"status", "triggered"},
            {"workflow_id", workflow_id},
            {"execution_id", generate_uuid()}
        }.dump());
    }

    // Create and trigger a new ad-hoc workflow
    Workflow wf;
    wf.id = generate_uuid();
    wf.name = body.value("name", "ad-hoc-workflow");
    wf.description = body.value("description", "");
    wf.status = "running";
    wf.trigger = "manual";
    wf.steps_json = body.contains("steps") ? body["steps"].dump() : "[]";
    wf.created_at = now_iso8601();

    std::lock_guard<std::mutex> lock(wf_mutex());
    wf_store().push_back(wf);

    spdlog::info("Created and triggered workflow {}", wf.id);
    return crow::response(201, json{
        {"status", "triggered"},
        {"workflow_id", wf.id},
        {"execution_id", generate_uuid()},
        {"workflow", wf_to_json(wf)}
    }.dump());
}

}  // namespace prodxcloud::api::handlers
