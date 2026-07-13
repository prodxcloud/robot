#include "api/handlers/ai_chat_handler.hpp"
#include "api/middleware/tenant_middleware.hpp"
#include "ai/ai_agent.hpp"
#include "common/uuid.hpp"
#include "common/types.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;
using namespace ai;

// ─── Helper: build LLM config from JSON body ───────────────────────────────

LLMConfig AIChatHandler::build_llm_config(const json& body) {
    LLMConfig config;
    config.provider = body.value("provider", "anthropic");
    config.model = body.value("model", "");
    config.temperature = body.value("temperature", 0.7f);
    config.max_tokens = body.value("max_tokens", 4096);

    // Model defaults per provider
    if (config.model.empty()) {
        if (config.provider == "anthropic") config.model = "claude-sonnet-4-20250514";
        else if (config.provider == "openai") config.model = "gpt-4o";
        else if (config.provider == "ollama") config.model = "llama3.1";
    }

    // API keys from body or environment
    config.api_key = body.value("api_key", "");
    if (config.api_key.empty()) {
        if (config.provider == "anthropic") {
            const char* key = std::getenv("ANTHROPIC_API_KEY");
            if (key) config.api_key = key;
        } else if (config.provider == "openai") {
            const char* key = std::getenv("OPENAI_API_KEY");
            if (key) config.api_key = key;
        }
    }

    config.base_url = body.value("base_url", "");
    config.stream = body.value("stream", false);
    return config;
}

// ─── Helper: get or create AI agent ─────────────────────────────────────────

std::shared_ptr<AIAgent> AIChatHandler::get_or_create_agent(
    const std::string& agent_type, const std::string& tenant_id,
    const std::string& provider, const std::string& model) {

    std::string key = tenant_id + ":" + agent_type + ":" + provider;

    std::lock_guard lock(agents_mutex_);
    auto it = ai_agents_.find(key);
    if (it != ai_agents_.end()) return it->second;

    AIAgentConfig config;
    config.agent_type = agent_type;
    config.agent_id = generate_uuid();
    config.tenant_id = tenant_id;
    config.llm_config.provider = provider;
    config.llm_config.model = model;
    if (config.llm_config.model.empty()) {
        if (provider == "anthropic") config.llm_config.model = "claude-sonnet-4-20250514";
        else if (provider == "openai") config.llm_config.model = "gpt-4o";
        else if (provider == "ollama") config.llm_config.model = "llama3.1";
    }

    // API keys from environment
    if (provider == "anthropic") {
        const char* key_env = std::getenv("ANTHROPIC_API_KEY");
        if (key_env) config.llm_config.api_key = key_env;
    } else if (provider == "openai") {
        const char* key_env = std::getenv("OPENAI_API_KEY");
        if (key_env) config.llm_config.api_key = key_env;
    }

    // Set agent-specific system prompt
    if (agent_type == "devops") config.system_prompt = prompts::devops_agent_system_prompt();
    else if (agent_type == "sre") config.system_prompt = prompts::sre_agent_system_prompt();
    else if (agent_type == "openclaw") config.system_prompt = prompts::openclaw_agent_system_prompt();
    else if (agent_type == "cicd") config.system_prompt = prompts::cicd_agent_system_prompt();

    auto agent = std::make_shared<AIAgent>(std::move(config));
    ai_agents_[key] = agent;

    spdlog::info("Created AI agent: type={}, provider={}, model={}",
                 agent_type, provider, agent->llm_config().model);
    return agent;
}

// ─── Constructor ────────────────────────────────────────────────────────────

AIChatHandler::AIChatHandler(std::shared_ptr<agents::AgentController> controller)
    : controller_(std::move(controller)) {}

// ─── Chat (auto-detect agent type) ─────────────────────────────────────────

crow::response AIChatHandler::chat(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant) return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::string message = body.value("message", "");
    if (message.empty())
        return crow::response(400, json{{"error", "message is required"}}.dump());

    // Auto-detect agent type from query
    auto intent = intent_detector_.detect(message);
    std::string agent_type = "devops";  // default
    if (intent) agent_type = intent->agent_type;

    std::string provider = body.value("provider", "anthropic");
    std::string model = body.value("model", "");

    auto agent = get_or_create_agent(agent_type, *tenant, provider, model);

    // Override LLM config if specified
    if (body.contains("provider") || body.contains("model")) {
        auto llm_config = build_llm_config(body);
        agent->set_llm_config(llm_config);
    }

    auto result = agent->chat(message);
    if (!result)
        return crow::response(500, json{{"error", result.error().message}}.dump());

    json response = {
        {"response", result->response},
        {"agent_type", result->agent_type},
        {"intent", {
            {"operation", result->intent.operation},
            {"agent_type", result->intent.agent_type},
            {"confidence", result->intent.confidence},
            {"method", result->intent.method}
        }},
        {"execution", {
            {"total_iterations", result->execution.total_iterations},
            {"total_tool_calls", result->execution.total_tool_calls},
            {"finish_reason", result->execution.finish_reason}
        }},
        {"model", agent->llm_config().model},
        {"provider", agent->llm_config().provider},
        {"total_tokens", result->total_tokens},
        {"latency_ms", result->total_latency_ms},
        {"session_id", result->session_id}
    };

    // Include reasoning steps if requested
    if (body.value("include_reasoning", false)) {
        json steps = json::array();
        for (const auto& step : result->execution.steps) {
            json tool_calls = json::array();
            for (const auto& tc : step.tool_calls)
                tool_calls.push_back({{"name", tc.name}, {"arguments", tc.arguments_json}});
            steps.push_back({
                {"iteration", step.iteration},
                {"thought", step.thought},
                {"tool_calls", tool_calls},
                {"tool_results_count", step.tool_results.size()},
                {"latency_ms", step.latency_ms}
            });
        }
        response["reasoning_steps"] = steps;
    }

    return crow::response(200, response.dump());
}

// ─── Chat with specific agent type ──────────────────────────────────────────

crow::response AIChatHandler::chat_with_type(const crow::request& req,
                                               const std::string& agent_type) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant) return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::string message = body.value("message", "");
    if (message.empty())
        return crow::response(400, json{{"error", "message is required"}}.dump());

    std::string provider = body.value("provider", "anthropic");
    std::string model = body.value("model", "");

    auto agent = get_or_create_agent(agent_type, *tenant, provider, model);
    if (body.contains("provider") || body.contains("model"))
        agent->set_llm_config(build_llm_config(body));

    auto result = agent->chat(message);
    if (!result)
        return crow::response(500, json{{"error", result.error().message}}.dump());

    return crow::response(200, json{
        {"response", result->response},
        {"agent_type", agent_type},
        {"model", agent->llm_config().model},
        {"provider", agent->llm_config().provider},
        {"tool_calls", result->execution.total_tool_calls},
        {"latency_ms", result->total_latency_ms}
    }.dump());
}

// ─── Chat with specific agent instance ──────────────────────────────────────

crow::response AIChatHandler::chat_with_agent(const crow::request& req,
                                                const std::string& agent_id) {
    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::string message = body.value("message", "");
    if (message.empty())
        return crow::response(400, json{{"error", "message is required"}}.dump());

    std::lock_guard lock(agents_mutex_);
    auto it = std::find_if(ai_agents_.begin(), ai_agents_.end(),
        [&](const auto& pair) { return pair.second->agent_id() == agent_id; });

    if (it == ai_agents_.end())
        return crow::response(404, json{{"error", "AI agent not found: " + agent_id}}.dump());

    auto result = it->second->chat(message);
    if (!result)
        return crow::response(500, json{{"error", result.error().message}}.dump());

    return crow::response(200, json{
        {"response", result->response},
        {"agent_id", agent_id},
        {"agent_type", it->second->agent_type()},
        {"tool_calls", result->execution.total_tool_calls},
        {"latency_ms", result->total_latency_ms}
    }.dump());
}

// ─── List Providers ─────────────────────────────────────────────────────────

crow::response AIChatHandler::list_providers(const crow::request& req) {
    json providers = json::array({
        {{"provider", "anthropic"},
         {"models", json::array({"claude-sonnet-4-20250514", "claude-opus-4-20250514", "claude-haiku-4-5-20251001"})},
         {"supports_tool_use", true}, {"supports_streaming", true},
         {"requires_api_key", true}, {"env_var", "ANTHROPIC_API_KEY"}},

        {{"provider", "openai"},
         {"models", json::array({"gpt-4o", "gpt-4o-mini", "gpt-4-turbo", "gpt-3.5-turbo"})},
         {"supports_tool_use", true}, {"supports_streaming", true},
         {"requires_api_key", true}, {"env_var", "OPENAI_API_KEY"}},

        {{"provider", "ollama"},
         {"models", json::array({"llama3.1", "llama3.2", "mistral", "codellama", "phi3", "gemma2"})},
         {"supports_tool_use", true}, {"supports_streaming", true},
         {"requires_api_key", false}, {"base_url", "http://localhost:11434"}}
    });

    return crow::response(200, json{{"providers", providers}}.dump());
}

// ─── Create AI Agent ────────────────────────────────────────────────────────

crow::response AIChatHandler::create_ai_agent(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant) return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::string agent_type = body.value("type", "cloud");
    std::string provider = body.value("provider", "anthropic");
    std::string model = body.value("model", "");

    auto agent = get_or_create_agent(agent_type, *tenant, provider, model);

    // Apply custom system prompt if provided
    if (body.contains("system_prompt"))
        agent->set_system_prompt(body["system_prompt"].get<std::string>());

    return crow::response(201, json{
        {"agent_id", agent->agent_id()},
        {"agent_type", agent_type},
        {"provider", agent->llm_config().provider},
        {"model", agent->llm_config().model},
        {"tools", agent->get_tools().size()}
    }.dump());
}

// ─── List AI Agents ─────────────────────────────────────────────────────────

crow::response AIChatHandler::list_ai_agents(const crow::request& req) {
    std::lock_guard lock(agents_mutex_);
    json arr = json::array();
    for (const auto& [key, agent] : ai_agents_) {
        arr.push_back({
            {"id", agent->agent_id()},
            {"type", agent->agent_type()},
            {"provider", agent->llm_config().provider},
            {"model", agent->llm_config().model},
            {"tools", agent->get_tools().size()},
            {"memory_entries", agent->memory().entry_count()}
        });
    }
    return crow::response(200, json{{"agents", arr}, {"count", arr.size()}}.dump());
}

// ─── Delete AI Agent ────────────────────────────────────────────────────────

crow::response AIChatHandler::delete_ai_agent(const crow::request& req,
                                                const std::string& agent_id) {
    std::lock_guard lock(agents_mutex_);
    for (auto it = ai_agents_.begin(); it != ai_agents_.end(); ++it) {
        if (it->second->agent_id() == agent_id) {
            ai_agents_.erase(it);
            return crow::response(200, json{{"status", "deleted"}, {"agent_id", agent_id}}.dump());
        }
    }
    return crow::response(404, json{{"error", "Agent not found"}}.dump());
}

// ─── Update Agent Config ────────────────────────────────────────────────────

crow::response AIChatHandler::update_agent_config(const crow::request& req,
                                                    const std::string& agent_id) {
    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::lock_guard lock(agents_mutex_);
    for (auto& [key, agent] : ai_agents_) {
        if (agent->agent_id() == agent_id) {
            auto config = build_llm_config(body);
            agent->set_llm_config(config);
            if (body.contains("system_prompt"))
                agent->set_system_prompt(body["system_prompt"].get<std::string>());
            return crow::response(200, json{
                {"agent_id", agent_id}, {"provider", config.provider},
                {"model", config.model}, {"status", "updated"}
            }.dump());
        }
    }
    return crow::response(404, json{{"error", "Agent not found"}}.dump());
}

// ─── Get Agent Memory ───────────────────────────────────────────────────────

crow::response AIChatHandler::get_agent_memory(const crow::request& req,
                                                 const std::string& agent_id) {
    std::lock_guard lock(agents_mutex_);
    for (const auto& [key, agent] : ai_agents_) {
        if (agent->agent_id() == agent_id) {
            auto messages = agent->memory().get_recent_messages(50);
            json arr = json::array();
            for (const auto& m : messages)
                arr.push_back({{"role", m.role}, {"content", m.content}});
            return crow::response(200, json{
                {"agent_id", agent_id},
                {"messages", arr},
                {"total_entries", agent->memory().entry_count()},
                {"estimated_tokens", agent->memory().estimated_tokens()}
            }.dump());
        }
    }
    return crow::response(404, json{{"error", "Agent not found"}}.dump());
}

// ─── Clear Agent Memory ─────────────────────────────────────────────────────

crow::response AIChatHandler::clear_agent_memory(const crow::request& req,
                                                   const std::string& agent_id) {
    std::lock_guard lock(agents_mutex_);
    for (auto& [key, agent] : ai_agents_) {
        if (agent->agent_id() == agent_id) {
            agent->clear_history();
            return crow::response(200, json{{"agent_id", agent_id}, {"status", "memory_cleared"}}.dump());
        }
    }
    return crow::response(404, json{{"error", "Agent not found"}}.dump());
}

// ─── Remember ───────────────────────────────────────────────────────────────

crow::response AIChatHandler::remember(const crow::request& req, const std::string& agent_id) {
    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::lock_guard lock(agents_mutex_);
    for (auto& [key, agent] : ai_agents_) {
        if (agent->agent_id() == agent_id) {
            std::string fact_key = body.value("key", "");
            std::string fact_value = body.value("value", "");
            if (fact_key.empty() || fact_value.empty())
                return crow::response(400, json{{"error", "key and value required"}}.dump());
            agent->remember(fact_key, fact_value);
            return crow::response(200, json{{"status", "stored"}, {"key", fact_key}}.dump());
        }
    }
    return crow::response(404, json{{"error", "Agent not found"}}.dump());
}

// ─── Detect Intent ──────────────────────────────────────────────────────────

crow::response AIChatHandler::detect_intent(const crow::request& req) {
    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::string query = body.value("query", "");
    if (query.empty())
        return crow::response(400, json{{"error", "query is required"}}.dump());

    auto result = intent_detector_.detect(query);
    if (!result)
        return crow::response(500, json{{"error", result.error().message}}.dump());

    return crow::response(200, json{
        {"operation", result->operation},
        {"agent_type", result->agent_type},
        {"confidence", result->confidence},
        {"method", result->method},
        {"extracted_params", json::parse(result->extracted_params_json, nullptr, false)}
    }.dump());
}

// ─── Execute Tool ───────────────────────────────────────────────────────────

crow::response AIChatHandler::execute_tool(const crow::request& req,
                                             const std::string& tool_name) {
    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    // Find an agent that has this tool
    std::lock_guard lock(agents_mutex_);
    for (auto& [key, agent] : ai_agents_) {
        auto result = agent->execute_tool(tool_name, body.dump());
        if (result)
            return crow::response(200, json{{"tool", tool_name}, {"result", *result}}.dump());
    }
    return crow::response(404, json{{"error", "Tool not found: " + tool_name}}.dump());
}

// ─── AI Health ──────────────────────────────────────────────────────────────

crow::response AIChatHandler::ai_health(const crow::request& req) {
    json health = {
        {"status", "healthy"},
        {"active_agents", ai_agents_.size()},
        {"providers", json::array({"anthropic", "openai", "ollama"})},
        {"features", {
            {"tool_use_reasoning", true},
            {"intent_detection", true},
            {"conversation_memory", true},
            {"multi_provider_llm", true},
            {"streaming", true}
        }}
    };

    // Check if API keys are configured
    json key_status = json::object();
    key_status["ANTHROPIC_API_KEY"] = (std::getenv("ANTHROPIC_API_KEY") != nullptr);
    key_status["OPENAI_API_KEY"] = (std::getenv("OPENAI_API_KEY") != nullptr);
    health["api_keys_configured"] = key_status;

    return crow::response(200, health.dump());
}

}  // namespace prodxcloud::api::handlers
