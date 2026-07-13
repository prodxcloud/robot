#pragma once

/// @file ai_chat_handler.hpp
/// @brief REST API handler for AI-powered agent chat with LLM reasoning.

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <crow.h>
#include <nlohmann/json.hpp>

#include "ai/ai_agent.hpp"
#include "agents/agent_controller.hpp"

namespace prodxcloud::api::handlers {

class AIChatHandler {
public:
    explicit AIChatHandler(std::shared_ptr<agents::AgentController> controller);

    /// POST /api/v1/ai/chat — Chat with an AI agent (auto-detect type from query)
    crow::response chat(const crow::request& req);

    /// POST /api/v1/ai/agents/:type/chat — Chat with a specific agent type
    crow::response chat_with_type(const crow::request& req, const std::string& agent_type);

    /// POST /api/v1/ai/agents/:id/chat — Chat with a specific agent instance
    crow::response chat_with_agent(const crow::request& req, const std::string& agent_id);

    /// GET /api/v1/ai/providers — List available LLM providers and models
    crow::response list_providers(const crow::request& req);

    /// POST /api/v1/ai/agents — Create a persistent AI agent instance
    crow::response create_ai_agent(const crow::request& req);

    /// GET /api/v1/ai/agents — List AI agent instances
    crow::response list_ai_agents(const crow::request& req);

    /// DELETE /api/v1/ai/agents/:id — Delete an AI agent instance
    crow::response delete_ai_agent(const crow::request& req, const std::string& agent_id);

    /// POST /api/v1/ai/agents/:id/config — Update LLM config for an agent
    crow::response update_agent_config(const crow::request& req, const std::string& agent_id);

    /// GET /api/v1/ai/agents/:id/memory — Get agent memory/context
    crow::response get_agent_memory(const crow::request& req, const std::string& agent_id);

    /// DELETE /api/v1/ai/agents/:id/memory — Clear agent memory
    crow::response clear_agent_memory(const crow::request& req, const std::string& agent_id);

    /// POST /api/v1/ai/agents/:id/remember — Store a fact in agent memory
    crow::response remember(const crow::request& req, const std::string& agent_id);

    /// POST /api/v1/ai/intent — Detect intent from a query (no execution)
    crow::response detect_intent(const crow::request& req);

    /// POST /api/v1/ai/tools/:tool_name — Execute a tool directly
    crow::response execute_tool(const crow::request& req, const std::string& tool_name);

    /// GET /api/v1/ai/health — Check AI subsystem health
    crow::response ai_health(const crow::request& req);

private:
    std::shared_ptr<agents::AgentController> controller_;
    mutable std::mutex agents_mutex_;
    std::unordered_map<std::string, std::shared_ptr<ai::AIAgent>> ai_agents_;
    ai::IntentDetector intent_detector_;

    std::shared_ptr<ai::AIAgent> get_or_create_agent(const std::string& agent_type,
                                                       const std::string& tenant_id,
                                                       const std::string& provider = "anthropic",
                                                       const std::string& model = "");
    ai::LLMConfig build_llm_config(const nlohmann::json& body);
};

}  // namespace prodxcloud::api::handlers
