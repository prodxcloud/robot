#pragma once

/// @file specialized_agent_handler.hpp
/// @brief REST API handler for specialized agent operations (DevOps, SRE, OpenClaw, CICD).

#include <memory>
#include <crow.h>
#include "agents/agent_controller.hpp"

namespace prodxcloud::api::handlers {

class SpecializedAgentHandler {
public:
    explicit SpecializedAgentHandler(std::shared_ptr<agents::AgentController> controller);

    // Agent type discovery
    crow::response list_agent_types(const crow::request& req);

    // Create specialized agent by type
    crow::response create_specialized_agent(const crow::request& req);

    // Execute operation on a specialized agent
    crow::response execute_operation(const crow::request& req, const std::string& agent_id);

    // Get agent capabilities
    crow::response get_agent_capabilities(const crow::request& req, const std::string& agent_type);

private:
    std::shared_ptr<agents::AgentController> controller_;
};

}  // namespace prodxcloud::api::handlers
