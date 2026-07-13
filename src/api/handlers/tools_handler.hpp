#pragma once
#include <crow.h>

namespace prodxcloud::api::handlers {

class ToolsHandler {
public:
    // Tools
    crow::response list_tools(const crow::request& req);
    crow::response create_tool(const crow::request& req);
    crow::response update_tool(const crow::request& req, const std::string& id);
    crow::response delete_tool(const crow::request& req, const std::string& id);

    // MCP Servers
    crow::response list_mcp_servers(const crow::request& req);
    crow::response add_mcp_server(const crow::request& req);
    crow::response remove_mcp_server(const crow::request& req, const std::string& id);
};

}  // namespace prodxcloud::api::handlers
