#include "api/handlers/agent_handler.hpp"
#include "api/middleware/tenant_middleware.hpp"
#include "common/uuid.hpp"
#include "common/types.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <vector>
#include <algorithm>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;

// ─── In-memory chat history store ───────────────────────────────────────────

static std::mutex& chat_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<ChatMessage>& chat_store() {
    static std::vector<ChatMessage> store;
    return store;
}

// ─── In-memory agent metadata store (for get/update beyond controller) ─────

struct AgentMeta {
    std::string id;
    std::string tenant_id;
    std::string name;
    std::string description;
    std::string model_id;
    std::string system_prompt;
    std::string created_at;
    std::string updated_at;
};

static std::mutex& agent_meta_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<AgentMeta>& agent_meta_store() {
    static std::vector<AgentMeta> store;
    return store;
}

// Simple default agent for API-created agents
class DefaultAgent : public agents::AgentBase {
public:
    explicit DefaultAgent(AgentConfig cfg) : AgentBase(std::move(cfg)) {}
    Result<TaskResult> execute(Task& task) override {
        transition_to(agents::AgentState::RUNNING);
        TaskResult r;
        r.task_id = task.id;
        r.success = true;
        r.output = "executed";
        transition_to(agents::AgentState::IDLE);
        return r;
    }
    void cancel() override { cancellation_token_.cancel(); }
    Result<bool> health_check() override { return true; }
};

AgentHandler::AgentHandler(std::shared_ptr<agents::AgentController> controller)
    : controller_(std::move(controller)) {}

crow::response AgentHandler::list_agents(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    auto agents = controller_->list_agents(*tenant);
    json arr = json::array();
    for (const auto& a : agents)
        arr.push_back({{"id", a.id}, {"name", a.name}, {"tenant_id", a.tenant_id}});
    return crow::response(200, json{{"agents", arr}, {"count", arr.size()}}.dump());
}

crow::response AgentHandler::create_agent(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    AgentConfig cfg;
    cfg.id = prodxcloud::generate_uuid();
    cfg.name = body.value("name", "unnamed-agent");
    cfg.tenant_id = *tenant;
    cfg.max_retries = body.value("max_retries", 3);
    cfg.timeout_ms = body.value("timeout_ms", 30000);

    auto agent = std::make_shared<DefaultAgent>(cfg);
    auto result = controller_->spawn_agent(cfg, agent);
    if (!result)
        return crow::response(500, json{{"error", result.error().message}}.dump());

    // Store metadata for get/update operations
    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex());
        AgentMeta meta;
        meta.id = cfg.id;
        meta.tenant_id = cfg.tenant_id;
        meta.name = cfg.name;
        meta.description = body.value("description", "");
        meta.model_id = body.value("model_id", "");
        meta.system_prompt = body.value("system_prompt", "");
        meta.created_at = now_iso8601();
        meta.updated_at = meta.created_at;
        agent_meta_store().push_back(meta);
    }

    return crow::response(201, json{{"agent_id", cfg.id}, {"name", cfg.name},
                                     {"tenant_id", cfg.tenant_id}}.dump());
}

crow::response AgentHandler::execute_agent(const crow::request& req, const std::string& agent_id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    auto state = controller_->get_agent_state(agent_id);
    if (!state)
        return crow::response(404, json{{"error", state.error().message}}.dump());

    return crow::response(200, json{{"agent_id", agent_id},
                                     {"state", std::string(agents::agent_state_to_string(*state))},
                                     {"status", "submitted"}}.dump());
}

crow::response AgentHandler::get_agent(const crow::request& req, const std::string& agent_id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    // Try to get state from controller
    auto state = controller_->get_agent_state(agent_id);
    if (!state)
        return crow::response(404, json{{"error", state.error().message}}.dump());

    // Try to get metadata
    json resp = {
        {"id", agent_id},
        {"tenant_id", *tenant},
        {"state", std::string(agents::agent_state_to_string(*state))}
    };

    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex());
        auto it = std::find_if(agent_meta_store().begin(), agent_meta_store().end(),
            [&](const AgentMeta& m) { return m.id == agent_id; });
        if (it != agent_meta_store().end()) {
            resp["name"] = it->name;
            resp["description"] = it->description;
            resp["model_id"] = it->model_id;
            resp["system_prompt"] = it->system_prompt;
            resp["created_at"] = it->created_at;
            resp["updated_at"] = it->updated_at;
        }
    }

    return crow::response(200, resp.dump());
}

crow::response AgentHandler::update_agent(const crow::request& req, const std::string& agent_id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    // Verify agent exists
    auto state = controller_->get_agent_state(agent_id);
    if (!state)
        return crow::response(404, json{{"error", state.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex());
        auto it = std::find_if(agent_meta_store().begin(), agent_meta_store().end(),
            [&](const AgentMeta& m) { return m.id == agent_id; });

        if (it == agent_meta_store().end()) {
            // Create metadata if it didn't exist
            AgentMeta meta;
            meta.id = agent_id;
            meta.tenant_id = *tenant;
            meta.name = body.value("name", "");
            meta.description = body.value("description", "");
            meta.model_id = body.value("model_id", "");
            meta.system_prompt = body.value("system_prompt", "");
            meta.created_at = now_iso8601();
            meta.updated_at = meta.created_at;
            agent_meta_store().push_back(meta);
        } else {
            if (body.contains("name")) it->name = body["name"].get<std::string>();
            if (body.contains("description")) it->description = body["description"].get<std::string>();
            if (body.contains("model_id")) it->model_id = body["model_id"].get<std::string>();
            if (body.contains("system_prompt")) it->system_prompt = body["system_prompt"].get<std::string>();
            it->updated_at = now_iso8601();
        }
    }

    spdlog::info("Updated agent {}", agent_id);
    return crow::response(200, json{{"status", "updated"}, {"agent_id", agent_id}}.dump());
}

crow::response AgentHandler::delete_agent(const crow::request& req, const std::string& agent_id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    auto result = controller_->terminate_agent(agent_id);
    if (!result)
        return crow::response(404, json{{"error", result.error().message}}.dump());

    // Clean up metadata
    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex());
        auto it = std::find_if(agent_meta_store().begin(), agent_meta_store().end(),
            [&](const AgentMeta& m) { return m.id == agent_id; });
        if (it != agent_meta_store().end())
            agent_meta_store().erase(it);
    }

    return crow::response(200, json{{"status", "terminated"}, {"agent_id", agent_id}}.dump());
}

crow::response AgentHandler::chat(const crow::request& req, const std::string& agent_id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    // Verify agent exists
    auto state = controller_->get_agent_state(agent_id);
    if (!state)
        return crow::response(404, json{{"error", state.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    std::string message = body.value("message", "");
    if (message.empty())
        return crow::response(400, json{{"error", "message is required"}}.dump());

    // Store user message
    ChatMessage user_msg;
    user_msg.id = generate_uuid();
    user_msg.agent_id = agent_id;
    user_msg.role = "user";
    user_msg.content = message;
    user_msg.timestamp = now_iso8601();

    // Generate assistant response (simulated)
    ChatMessage assistant_msg;
    assistant_msg.id = generate_uuid();
    assistant_msg.agent_id = agent_id;
    assistant_msg.role = "assistant";
    assistant_msg.content = "I received your message: \"" + message + "\". How can I help you further?";
    assistant_msg.timestamp = now_iso8601();

    {
        std::lock_guard<std::mutex> lock(chat_mutex());
        chat_store().push_back(user_msg);
        chat_store().push_back(assistant_msg);
    }

    return crow::response(200, json{
        {"agent_id", agent_id},
        {"message_id", assistant_msg.id},
        {"role", "assistant"},
        {"content", assistant_msg.content},
        {"timestamp", assistant_msg.timestamp}
    }.dump());
}

crow::response AgentHandler::get_chat_history(const crow::request& req, const std::string& agent_id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    // Verify agent exists
    auto state = controller_->get_agent_state(agent_id);
    if (!state)
        return crow::response(404, json{{"error", state.error().message}}.dump());

    std::lock_guard<std::mutex> lock(chat_mutex());
    json arr = json::array();

    for (const auto& msg : chat_store()) {
        if (msg.agent_id == agent_id) {
            arr.push_back({
                {"id", msg.id},
                {"agent_id", msg.agent_id},
                {"role", msg.role},
                {"content", msg.content},
                {"timestamp", msg.timestamp}
            });
        }
    }

    return crow::response(200, json{{"messages", arr}, {"count", arr.size()}}.dump());
}

}  // namespace prodxcloud::api::handlers
