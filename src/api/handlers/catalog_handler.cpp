#include "api/handlers/catalog_handler.hpp"
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

// ─── In-memory catalog store ────────────────────────────────────────────────

static std::mutex& catalog_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<CatalogItem>& catalog_store() {
    static std::vector<CatalogItem> store = [] {
        // Seed with some default catalog entries
        std::vector<CatalogItem> s;
        s.push_back({"gpt-4o", "GPT-4o", "OpenAI", "chat", "openai", "2024-08-06", "Most advanced GPT model for chat and reasoning", "available"});
        s.push_back({"gpt-4o-mini", "GPT-4o Mini", "OpenAI", "chat", "openai", "2024-07-18", "Fast and affordable GPT model", "available"});
        s.push_back({"claude-3-5-sonnet", "Claude 3.5 Sonnet", "Anthropic", "chat", "anthropic", "2024-10-22", "Best balance of speed and intelligence", "available"});
        s.push_back({"claude-3-5-haiku", "Claude 3.5 Haiku", "Anthropic", "chat", "anthropic", "2024-10-22", "Fastest Claude model", "available"});
        s.push_back({"llama-3.1-70b", "Llama 3.1 70B", "Meta", "chat", "huggingface", "3.1", "Open-weight large language model", "available"});
        s.push_back({"llama-3.1-8b", "Llama 3.1 8B", "Meta", "chat", "huggingface", "3.1", "Efficient open-weight model", "available"});
        s.push_back({"mistral-large", "Mistral Large", "Mistral AI", "chat", "mistral", "2", "Flagship Mistral model", "available"});
        s.push_back({"text-embedding-3-large", "Text Embedding 3 Large", "OpenAI", "embedding", "openai", "3", "High-quality text embeddings", "available"});
        s.push_back({"whisper-large-v3", "Whisper Large v3", "OpenAI", "speech-to-text", "openai", "3", "Speech recognition model", "available"});
        s.push_back({"stable-diffusion-xl", "Stable Diffusion XL", "Stability AI", "image-generation", "huggingface", "1.0", "Text-to-image generation", "available"});
        return s;
    }();
    return store;
}

// ─── Handlers ───────────────────────────────────────────────────────────────

crow::response CatalogHandler::list_catalog(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    // Parse pagination
    auto limit_str = req.url_params.get("limit");
    auto offset_str = req.url_params.get("offset");
    int limit = limit_str ? std::atoi(limit_str) : 20;
    int offset = offset_str ? std::atoi(offset_str) : 0;
    if (limit <= 0) limit = 20;
    if (offset < 0) offset = 0;

    std::lock_guard<std::mutex> lock(catalog_mutex());
    const auto& store = catalog_store();

    int total = static_cast<int>(store.size());
    json arr = json::array();

    for (int i = offset; i < total && i < offset + limit; ++i) {
        const auto& item = store[i];
        arr.push_back({
            {"id", item.id},
            {"name", item.name},
            {"provider", item.provider},
            {"type", item.type},
            {"source", item.source},
            {"version", item.version},
            {"description", item.description},
            {"status", item.status}
        });
    }

    return crow::response(200, json{
        {"models", arr},
        {"total", total},
        {"limit", limit},
        {"offset", offset}
    }.dump());
}

crow::response CatalogHandler::catalog_summary(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(catalog_mutex());
    const auto& store = catalog_store();

    json by_source = json::object();
    json by_type = json::object();

    for (const auto& item : store) {
        by_source[item.source] = by_source.value(item.source, 0) + 1;
        by_type[item.type] = by_type.value(item.type, 0) + 1;
    }

    return crow::response(200, json{
        {"total_models", store.size()},
        {"by_source", by_source},
        {"by_type", by_type}
    }.dump());
}

crow::response CatalogHandler::get_catalog_item(const crow::request& req, const std::string& model_id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(catalog_mutex());
    const auto& store = catalog_store();

    auto it = std::find_if(store.begin(), store.end(),
        [&](const CatalogItem& c) { return c.id == model_id; });

    if (it == store.end())
        return crow::response(404, json{{"error", "Catalog item not found"}}.dump());

    return crow::response(200, json{
        {"id", it->id},
        {"name", it->name},
        {"provider", it->provider},
        {"type", it->type},
        {"source", it->source},
        {"version", it->version},
        {"description", it->description},
        {"status", it->status}
    }.dump());
}

}  // namespace prodxcloud::api::handlers
