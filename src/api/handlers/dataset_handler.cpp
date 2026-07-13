#include "api/handlers/dataset_handler.hpp"
#include "api/middleware/tenant_middleware.hpp"
#include "common/types.hpp"
#include "common/uuid.hpp"
#include "datasets/cleaner.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <vector>
#include <algorithm>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;

// ─── In-memory dataset info store ───────────────────────────────────────────

static std::mutex& ds_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<DatasetInfo>& ds_store() {
    static std::vector<DatasetInfo> store;
    return store;
}

static json ds_to_json(const DatasetInfo& d) {
    return {
        {"id", d.id},
        {"tenant_id", d.tenant_id},
        {"name", d.name},
        {"description", d.description},
        {"format", d.format},
        {"size_bytes", d.size_bytes},
        {"row_count", d.row_count},
        {"status", d.status},
        {"created_at", d.created_at}
    };
}

crow::response DatasetHandler::validate(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("rows") || !body["rows"].is_array())
        return crow::response(400, json{{"error", "rows array required"}}.dump());

    Dataset dataset;
    dataset.id = body.value("id", "inline");
    dataset.tenant_id = *tenant;
    for (const auto& row_json : body["rows"]) {
        DatasetRow row;
        for (auto it = row_json.begin(); it != row_json.end(); ++it)
            row.fields[it.key()] = it.value().dump();
        dataset.rows.push_back(std::move(row));
    }

    // Basic validation: report row count, empty fields
    int empty_count = 0;
    for (const auto& row : dataset.rows)
        for (const auto& [k, v] : row.fields)
            if (v.empty() || v == "\"\"" || v == "null") ++empty_count;

    json resp = {
        {"dataset_id", dataset.id},
        {"total_rows", dataset.rows.size()},
        {"empty_fields", empty_count},
        {"valid", empty_count == 0}
    };
    return crow::response(200, resp.dump());
}

crow::response DatasetHandler::clean(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("rows") || !body["rows"].is_array())
        return crow::response(400, json{{"error", "rows array required"}}.dump());

    Dataset dataset;
    dataset.id = body.value("id", "inline");
    dataset.tenant_id = *tenant;
    for (const auto& row_json : body["rows"]) {
        DatasetRow row;
        for (auto it = row_json.begin(); it != row_json.end(); ++it)
            row.fields[it.key()] = it.value().dump();
        dataset.rows.push_back(std::move(row));
    }

    size_t original = dataset.rows.size();

    // Apply cleaning: remove rows with all empty fields
    datasets::DataCleaner cleaner;
    auto result = cleaner.add_stage("remove_empty", [](Dataset& ds) -> Result<void> {
        ds.rows.erase(
            std::remove_if(ds.rows.begin(), ds.rows.end(), [](const DatasetRow& r) {
                return r.fields.empty();
            }),
            ds.rows.end()
        );
        return {};
    }).clean(dataset);

    if (!result)
        return crow::response(500, json{{"error", result.error().message}}.dump());

    json resp = {
        {"dataset_id", dataset.id},
        {"original_rows", original},
        {"cleaned_rows", dataset.rows.size()},
        {"removed", original - dataset.rows.size()}
    };
    return crow::response(200, resp.dump());
}

// ─── CRUD Handlers ──────────────────────────────────────────────────────────

crow::response DatasetHandler::list_datasets(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(ds_mutex());
    json arr = json::array();

    for (const auto& d : ds_store()) {
        if (d.tenant_id == *tenant)
            arr.push_back(ds_to_json(d));
    }

    return crow::response(200, json{{"datasets", arr}, {"count", arr.size()}}.dump());
}

crow::response DatasetHandler::get_dataset(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(ds_mutex());

    auto it = std::find_if(ds_store().begin(), ds_store().end(),
        [&](const DatasetInfo& d) { return d.id == id && d.tenant_id == *tenant; });

    if (it == ds_store().end())
        return crow::response(404, json{{"error", "Dataset not found"}}.dump());

    return crow::response(200, ds_to_json(*it).dump());
}

crow::response DatasetHandler::create_dataset(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("name"))
        return crow::response(400, json{{"error", "name is required"}}.dump());

    DatasetInfo ds;
    ds.id = generate_uuid();
    ds.tenant_id = *tenant;
    ds.name = body.value("name", "");
    ds.description = body.value("description", "");
    ds.format = body.value("format", "json");
    ds.size_bytes = body.value("size_bytes", 0);
    ds.row_count = body.value("row_count", 0);
    ds.status = "ready";
    ds.created_at = now_iso8601();

    std::lock_guard<std::mutex> lock(ds_mutex());
    ds_store().push_back(ds);

    spdlog::info("Created dataset {} for tenant {}", ds.id, ds.tenant_id);
    return crow::response(201, ds_to_json(ds).dump());
}

crow::response DatasetHandler::delete_dataset(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(ds_mutex());

    auto it = std::find_if(ds_store().begin(), ds_store().end(),
        [&](const DatasetInfo& d) { return d.id == id && d.tenant_id == *tenant; });

    if (it == ds_store().end())
        return crow::response(404, json{{"error", "Dataset not found"}}.dump());

    std::string deleted_id = it->id;
    ds_store().erase(it);

    spdlog::info("Deleted dataset {}", deleted_id);
    return crow::response(200, json{{"status", "deleted"}, {"id", deleted_id}}.dump());
}

}  // namespace prodxcloud::api::handlers
