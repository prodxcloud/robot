#include "api/handlers/tools_handler.hpp"
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

// ─── In-memory tools store ──────────────────────────────────────────────────

static std::mutex& tools_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<Tool>& tools_store() {
    static std::vector<Tool> store;
    return store;
}

static std::mutex& mcp_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<MCPServer>& mcp_store() {
    static std::vector<MCPServer> store;
    return store;
}

static json tool_to_json(const Tool& t) {
    return {
        {"id", t.id},
        {"name", t.name},
        {"description", t.description},
        {"type", t.type},
        {"enabled", t.enabled}
    };
}

static json mcp_to_json(const MCPServer& s) {
    return {
        {"id", s.id},
        {"name", s.name},
        {"description", s.description},
        {"endpoint", s.endpoint},
        {"protocol", s.protocol},
        {"status", s.status},
        {"tools_count", s.tools_count},
        {"resources_count", s.resources_count}
    };
}

// ─── Tool Handlers ──────────────────────────────────────────────────────────

crow::response ToolsHandler::list_tools(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(tools_mutex());
    json arr = json::array();

    for (const auto& t : tools_store())
        arr.push_back(tool_to_json(t));

    return crow::response(200, json{{"tools", arr}, {"count", arr.size()}}.dump());
}

crow::response ToolsHandler::create_tool(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("name"))
        return crow::response(400, json{{"error", "name is required"}}.dump());

    std::string tool_type = body.value("type", "function");
    if (tool_type != "function" && tool_type != "api" && tool_type != "plugin")
        return crow::response(400, json{{"error", "type must be: function, api, or plugin"}}.dump());

    Tool tool;
    tool.id = generate_uuid();
    tool.name = body.value("name", "");
    tool.description = body.value("description", "");
    tool.type = tool_type;
    tool.enabled = body.value("enabled", true);

    std::lock_guard<std::mutex> lock(tools_mutex());
    tools_store().push_back(tool);

    spdlog::info("Created tool {}: {}", tool.id, tool.name);
    return crow::response(201, tool_to_json(tool).dump());
}

crow::response ToolsHandler::update_tool(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::lock_guard<std::mutex> lock(tools_mutex());

    auto it = std::find_if(tools_store().begin(), tools_store().end(),
        [&](const Tool& t) { return t.id == id; });

    if (it == tools_store().end())
        return crow::response(404, json{{"error", "Tool not found"}}.dump());

    if (body.contains("enabled")) it->enabled = body["enabled"].get<bool>();
    if (body.contains("name")) it->name = body["name"].get<std::string>();
    if (body.contains("description")) it->description = body["description"].get<std::string>();

    spdlog::info("Updated tool {}", id);
    return crow::response(200, tool_to_json(*it).dump());
}

crow::response ToolsHandler::delete_tool(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(tools_mutex());

    auto it = std::find_if(tools_store().begin(), tools_store().end(),
        [&](const Tool& t) { return t.id == id; });

    if (it == tools_store().end())
        return crow::response(404, json{{"error", "Tool not found"}}.dump());

    std::string deleted_id = it->id;
    tools_store().erase(it);

    spdlog::info("Deleted tool {}", deleted_id);
    return crow::response(200, json{{"status", "deleted"}, {"id", deleted_id}}.dump());
}

// ─── MCP Server Handlers ───────────────────────────────────────────────────

crow::response ToolsHandler::list_mcp_servers(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(mcp_mutex());
    json arr = json::array();

    for (const auto& s : mcp_store())
        arr.push_back(mcp_to_json(s));

    return crow::response(200, json{{"mcp_servers", arr}, {"count", arr.size()}}.dump());
}

crow::response ToolsHandler::add_mcp_server(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("name") || !body.contains("endpoint"))
        return crow::response(400, json{{"error", "name and endpoint are required"}}.dump());

    std::string protocol = body.value("protocol", "http");
    if (protocol != "stdio" && protocol != "http" && protocol != "websocket")
        return crow::response(400, json{{"error", "protocol must be: stdio, http, or websocket"}}.dump());

    MCPServer server;
    server.id = generate_uuid();
    server.name = body.value("name", "");
    server.description = body.value("description", "");
    server.endpoint = body.value("endpoint", "");
    server.protocol = protocol;
    server.status = "connected";
    server.tools_count = body.value("tools_count", 0);
    server.resources_count = body.value("resources_count", 0);

    std::lock_guard<std::mutex> lock(mcp_mutex());
    mcp_store().push_back(server);

    spdlog::info("Added MCP server {}: {}", server.id, server.name);
    return crow::response(201, mcp_to_json(server).dump());
}

crow::response ToolsHandler::remove_mcp_server(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(mcp_mutex());

    auto it = std::find_if(mcp_store().begin(), mcp_store().end(),
        [&](const MCPServer& s) { return s.id == id; });

    if (it == mcp_store().end())
        return crow::response(404, json{{"error", "MCP server not found"}}.dump());

    std::string deleted_id = it->id;
    mcp_store().erase(it);

    spdlog::info("Removed MCP server {}", deleted_id);
    return crow::response(200, json{{"status", "removed"}, {"id", deleted_id}}.dump());
}

}  // namespace prodxcloud::api::handlers
