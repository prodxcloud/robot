#include "api/handlers/knowledge_handler.hpp"
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

// ─── In-memory knowledge base store ─────────────────────────────────────────

static std::mutex& kb_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<KnowledgeBase>& kb_store() {
    static std::vector<KnowledgeBase> store;
    return store;
}

static json kb_to_json(const KnowledgeBase& kb) {
    return {
        {"id", kb.id},
        {"tenant_id", kb.tenant_id},
        {"name", kb.name},
        {"type", kb.type},
        {"size_bytes", kb.size_bytes},
        {"doc_count", kb.doc_count},
        {"status", kb.status},
        {"created_at", kb.created_at},
        {"updated_at", kb.updated_at}
    };
}

// ─── Handlers ───────────────────────────────────────────────────────────────

crow::response KnowledgeHandler::list_knowledge_bases(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(kb_mutex());
    json arr = json::array();

    for (const auto& kb : kb_store()) {
        if (kb.tenant_id == *tenant)
            arr.push_back(kb_to_json(kb));
    }

    return crow::response(200, json{{"knowledge_bases", arr}, {"count", arr.size()}}.dump());
}

crow::response KnowledgeHandler::get_knowledge_base(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(kb_mutex());

    auto it = std::find_if(kb_store().begin(), kb_store().end(),
        [&](const KnowledgeBase& kb) { return kb.id == id && kb.tenant_id == *tenant; });

    if (it == kb_store().end())
        return crow::response(404, json{{"error", "Knowledge base not found"}}.dump());

    return crow::response(200, kb_to_json(*it).dump());
}

crow::response KnowledgeHandler::create_knowledge_base(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("name"))
        return crow::response(400, json{{"error", "name is required"}}.dump());

    std::string kb_type = body.value("type", "vector-store");
    if (kb_type != "vector-store" && kb_type != "dataset" && kb_type != "documents")
        return crow::response(400, json{{"error", "type must be: vector-store, dataset, or documents"}}.dump());

    KnowledgeBase kb;
    kb.id = generate_uuid();
    kb.tenant_id = *tenant;
    kb.name = body.value("name", "");
    kb.type = kb_type;
    kb.status = "active";
    kb.created_at = now_iso8601();
    kb.updated_at = kb.created_at;

    std::lock_guard<std::mutex> lock(kb_mutex());
    kb_store().push_back(kb);

    spdlog::info("Created knowledge base {} for tenant {}", kb.id, kb.tenant_id);
    return crow::response(201, kb_to_json(kb).dump());
}

crow::response KnowledgeHandler::delete_knowledge_base(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(kb_mutex());

    auto it = std::find_if(kb_store().begin(), kb_store().end(),
        [&](const KnowledgeBase& kb) { return kb.id == id && kb.tenant_id == *tenant; });

    if (it == kb_store().end())
        return crow::response(404, json{{"error", "Knowledge base not found"}}.dump());

    std::string deleted_id = it->id;
    kb_store().erase(it);

    spdlog::info("Deleted knowledge base {}", deleted_id);
    return crow::response(200, json{{"status", "deleted"}, {"id", deleted_id}}.dump());
}

crow::response KnowledgeHandler::upload_documents(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::lock_guard<std::mutex> lock(kb_mutex());

    auto it = std::find_if(kb_store().begin(), kb_store().end(),
        [&](const KnowledgeBase& kb) { return kb.id == id && kb.tenant_id == *tenant; });

    if (it == kb_store().end())
        return crow::response(404, json{{"error", "Knowledge base not found"}}.dump());

    // Simulate document upload
    int doc_count = 0;
    int64_t total_bytes = 0;
    if (body.contains("documents") && body["documents"].is_array()) {
        doc_count = static_cast<int>(body["documents"].size());
        for (const auto& doc : body["documents"]) {
            std::string content = doc.value("content", "");
            total_bytes += static_cast<int64_t>(content.size());
        }
    } else {
        doc_count = 1;
        total_bytes = static_cast<int64_t>(req.body.size());
    }

    it->doc_count += doc_count;
    it->size_bytes += total_bytes;
    it->updated_at = now_iso8601();

    spdlog::info("Uploaded {} documents to knowledge base {}", doc_count, id);
    return crow::response(200, json{
        {"id", it->id},
        {"documents_added", doc_count},
        {"total_doc_count", it->doc_count},
        {"total_size_bytes", it->size_bytes}
    }.dump());
}

}  // namespace prodxcloud::api::handlers
