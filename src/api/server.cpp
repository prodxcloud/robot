#include "api/server.hpp"
#include "inference/remote_registry.hpp"
#include "api/handlers/inference_handler.hpp"
#include "api/handlers/agent_handler.hpp"
#include "api/handlers/specialized_agent_handler.hpp"
#include "api/handlers/dataset_handler.hpp"
#include "api/handlers/analysis_handler.hpp"
#include "api/handlers/catalog_handler.hpp"
#include "api/handlers/deployment_handler.hpp"
#include "api/handlers/knowledge_handler.hpp"
#include "api/handlers/fine_tuning_handler.hpp"
#include "api/handlers/training_handler.hpp"
#include "api/handlers/evaluation_handler.hpp"
#include "api/handlers/tools_handler.hpp"
#include "api/handlers/web_assets_handler.hpp"
#include "api/handlers/workflow_handler.hpp"
#include "api/handlers/dashboard_handler.hpp"
#include "api/handlers/ai_chat_handler.hpp"
#include "api/middleware/tenant_middleware.hpp"
#include "api/middleware/rate_limiter.hpp"
#include "telemetry/metrics.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace prodxcloud::api {

ApiServer::ApiServer(const ServerConfig& config, ServerDeps deps)
    : config_(config), deps_(std::move(deps)) {
    register_middleware();
    register_routes();
}

ApiServer::~ApiServer() { stop(); }

void ApiServer::register_middleware() {
    // CORS headers added manually in responses since SimpleApp doesn't support middleware template
}

void ApiServer::register_routes() {
    // Health
    CROW_ROUTE(app_, "/health").methods("GET"_method)([]() {
        return crow::response(200, nlohmann::json{{"status", "healthy"}}.dump());
    });

    // Readiness — no local engine required
    CROW_ROUTE(app_, "/ready").methods("GET"_method)([this]() {
        bool ready = deps_.model_registry != nullptr && deps_.agent_controller != nullptr;
        int code = ready ? 200 : 503;
        return crow::response(code, nlohmann::json{{"ready", ready}}.dump());
    });

    // Metrics endpoint
    CROW_ROUTE(app_, "/metrics").methods("GET"_method)([]() {
        return crow::response(200, telemetry::PrometheusMetrics::instance().serialize());
    });

    // ─── Initialize all handlers ────────────────────────────────────────────
    auto inf_handler = std::make_shared<handlers::InferenceHandler>(deps_.model_registry);
    auto agent_handler = std::make_shared<handlers::AgentHandler>(deps_.agent_controller);
    auto specialized_handler = std::make_shared<handlers::SpecializedAgentHandler>(deps_.agent_controller);
    auto dataset_handler = std::make_shared<handlers::DatasetHandler>();
    auto analysis_handler = std::make_shared<handlers::AnalysisHandler>();
    auto catalog_handler = std::make_shared<handlers::CatalogHandler>();
    auto deployment_handler = std::make_shared<handlers::DeploymentHandler>();
    auto knowledge_handler = std::make_shared<handlers::KnowledgeHandler>();
    auto fine_tuning_handler = std::make_shared<handlers::FineTuningHandler>();
    auto training_handler = std::make_shared<handlers::TrainingHandler>();
    auto evaluation_handler = std::make_shared<handlers::EvaluationHandler>();
    auto tools_handler = std::make_shared<handlers::ToolsHandler>();
    auto web_assets_handler = std::make_shared<handlers::WebAssetsHandler>();
    auto workflow_handler = std::make_shared<handlers::WorkflowHandler>();
    auto dashboard_handler = std::make_shared<handlers::DashboardHandler>();
    auto ai_chat_handler = std::make_shared<handlers::AIChatHandler>(deps_.agent_controller);

    // ─── Inference ──────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/inference").methods("POST"_method)(
        [inf_handler](const crow::request& req) { return inf_handler->infer(req); });

    CROW_ROUTE(app_, "/api/v1/models").methods("GET"_method)(
        [inf_handler](const crow::request& req) { return inf_handler->list_models(req); });

    CROW_ROUTE(app_, "/api/v1/models/<string>/load").methods("POST"_method)(
        [inf_handler](const crow::request& req, const std::string& id) {
            return inf_handler->load_model(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/models/<string>/unload").methods("POST"_method)(
        [inf_handler](const crow::request& req, const std::string& id) {
            return inf_handler->unload_model(req, id);
        });

    // ─── Agents ─────────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/agents").methods("GET"_method)(
        [agent_handler](const crow::request& req) { return agent_handler->list_agents(req); });

    CROW_ROUTE(app_, "/api/v1/agents").methods("POST"_method)(
        [agent_handler](const crow::request& req) { return agent_handler->create_agent(req); });

    CROW_ROUTE(app_, "/api/v1/agents/<string>").methods("GET"_method)(
        [agent_handler](const crow::request& req, const std::string& id) {
            return agent_handler->get_agent(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/agents/<string>").methods("PUT"_method)(
        [agent_handler](const crow::request& req, const std::string& id) {
            return agent_handler->update_agent(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/agents/<string>/execute").methods("POST"_method)(
        [agent_handler](const crow::request& req, const std::string& id) {
            return agent_handler->execute_agent(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/agents/<string>").methods("DELETE"_method)(
        [agent_handler](const crow::request& req, const std::string& id) {
            return agent_handler->delete_agent(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/agents/<string>/chat").methods("POST"_method)(
        [agent_handler](const crow::request& req, const std::string& id) {
            return agent_handler->chat(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/agents/<string>/history").methods("GET"_method)(
        [agent_handler](const crow::request& req, const std::string& id) {
            return agent_handler->get_chat_history(req, id);
        });

    // ─── Specialized Agents ────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/agents/types").methods("GET"_method)(
        [specialized_handler](const crow::request& req) {
            return specialized_handler->list_agent_types(req);
        });

    CROW_ROUTE(app_, "/api/v1/agents/specialized").methods("POST"_method)(
        [specialized_handler](const crow::request& req) {
            return specialized_handler->create_specialized_agent(req);
        });

    CROW_ROUTE(app_, "/api/v1/agents/<string>/operate").methods("POST"_method)(
        [specialized_handler](const crow::request& req, const std::string& id) {
            return specialized_handler->execute_operation(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/agents/types/<string>/capabilities").methods("GET"_method)(
        [specialized_handler](const crow::request& req, const std::string& type) {
            return specialized_handler->get_agent_capabilities(req, type);
        });

    // ─── Datasets ───────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/datasets").methods("GET"_method)(
        [dataset_handler](const crow::request& req) { return dataset_handler->list_datasets(req); });

    CROW_ROUTE(app_, "/api/v1/datasets").methods("POST"_method)(
        [dataset_handler](const crow::request& req) { return dataset_handler->create_dataset(req); });

    CROW_ROUTE(app_, "/api/v1/datasets/<string>").methods("GET"_method)(
        [dataset_handler](const crow::request& req, const std::string& id) {
            return dataset_handler->get_dataset(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/datasets/<string>").methods("DELETE"_method)(
        [dataset_handler](const crow::request& req, const std::string& id) {
            return dataset_handler->delete_dataset(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/datasets/validate").methods("POST"_method)(
        [dataset_handler](const crow::request& req) { return dataset_handler->validate(req); });

    CROW_ROUTE(app_, "/api/v1/datasets/clean").methods("POST"_method)(
        [dataset_handler](const crow::request& req) { return dataset_handler->clean(req); });

    // ─── Analysis ───────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/analysis/benchmark").methods("POST"_method)(
        [analysis_handler](const crow::request& req) { return analysis_handler->benchmark(req); });

    CROW_ROUTE(app_, "/api/v1/analysis/evaluate").methods("POST"_method)(
        [analysis_handler](const crow::request& req) { return analysis_handler->evaluate(req); });

    CROW_ROUTE(app_, "/api/v1/analysis/drift").methods("POST"_method)(
        [analysis_handler](const crow::request& req) { return analysis_handler->drift_detect(req); });

    // ─── Catalog ────────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/catalog").methods("GET"_method)(
        [catalog_handler](const crow::request& req) { return catalog_handler->list_catalog(req); });

    CROW_ROUTE(app_, "/api/v1/catalog/summary").methods("GET"_method)(
        [catalog_handler](const crow::request& req) { return catalog_handler->catalog_summary(req); });

    CROW_ROUTE(app_, "/api/v1/catalog/<string>").methods("GET"_method)(
        [catalog_handler](const crow::request& req, const std::string& id) {
            return catalog_handler->get_catalog_item(req, id);
        });

    // ─── Deployments ────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/deployments").methods("GET"_method)(
        [deployment_handler](const crow::request& req) { return deployment_handler->list_deployments(req); });

    CROW_ROUTE(app_, "/api/v1/deployments/summary").methods("GET"_method)(
        [deployment_handler](const crow::request& req) { return deployment_handler->deployment_summary(req); });

    CROW_ROUTE(app_, "/api/v1/deployments").methods("POST"_method)(
        [deployment_handler](const crow::request& req) { return deployment_handler->create_deployment(req); });

    CROW_ROUTE(app_, "/api/v1/deployments/<string>").methods("GET"_method)(
        [deployment_handler](const crow::request& req, const std::string& id) {
            return deployment_handler->get_deployment(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/deployments/<string>/status").methods("PATCH"_method)(
        [deployment_handler](const crow::request& req, const std::string& id) {
            return deployment_handler->update_deployment_status(req, id);
        });

    // ─── Knowledge Bases ────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/knowledge").methods("GET"_method)(
        [knowledge_handler](const crow::request& req) { return knowledge_handler->list_knowledge_bases(req); });

    CROW_ROUTE(app_, "/api/v1/knowledge").methods("POST"_method)(
        [knowledge_handler](const crow::request& req) { return knowledge_handler->create_knowledge_base(req); });

    CROW_ROUTE(app_, "/api/v1/knowledge/<string>").methods("GET"_method)(
        [knowledge_handler](const crow::request& req, const std::string& id) {
            return knowledge_handler->get_knowledge_base(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/knowledge/<string>").methods("DELETE"_method)(
        [knowledge_handler](const crow::request& req, const std::string& id) {
            return knowledge_handler->delete_knowledge_base(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/knowledge/<string>/upload").methods("POST"_method)(
        [knowledge_handler](const crow::request& req, const std::string& id) {
            return knowledge_handler->upload_documents(req, id);
        });

    // ─── Fine-Tuning ────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/fine-tuning").methods("GET"_method)(
        [fine_tuning_handler](const crow::request& req) { return fine_tuning_handler->list_jobs(req); });

    CROW_ROUTE(app_, "/api/v1/fine-tuning").methods("POST"_method)(
        [fine_tuning_handler](const crow::request& req) { return fine_tuning_handler->create_job(req); });

    CROW_ROUTE(app_, "/api/v1/fine-tuning/<string>").methods("GET"_method)(
        [fine_tuning_handler](const crow::request& req, const std::string& id) {
            return fine_tuning_handler->get_job(req, id);
        });

    // ─── Training ───────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/training").methods("GET"_method)(
        [training_handler](const crow::request& req) { return training_handler->list_jobs(req); });

    CROW_ROUTE(app_, "/api/v1/training").methods("POST"_method)(
        [training_handler](const crow::request& req) { return training_handler->create_job(req); });

    CROW_ROUTE(app_, "/api/v1/training/<string>").methods("GET"_method)(
        [training_handler](const crow::request& req, const std::string& id) {
            return training_handler->get_job(req, id);
        });

    // ─── Evaluations ────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/evals").methods("GET"_method)(
        [evaluation_handler](const crow::request& req) { return evaluation_handler->list_evals(req); });

    CROW_ROUTE(app_, "/api/v1/evals").methods("POST"_method)(
        [evaluation_handler](const crow::request& req) { return evaluation_handler->create_eval(req); });

    CROW_ROUTE(app_, "/api/v1/evals/feedback").methods("POST"_method)(
        [evaluation_handler](const crow::request& req) { return evaluation_handler->submit_feedback(req); });

    CROW_ROUTE(app_, "/api/v1/evals/stats").methods("GET"_method)(
        [evaluation_handler](const crow::request& req) { return evaluation_handler->feedback_stats(req); });

    CROW_ROUTE(app_, "/api/v1/evals/feedback/<string>").methods("GET"_method)(
        [evaluation_handler](const crow::request& req, const std::string& id) {
            return evaluation_handler->get_feedback(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/evals/<string>").methods("GET"_method)(
        [evaluation_handler](const crow::request& req, const std::string& id) {
            return evaluation_handler->get_eval(req, id);
        });

    // ─── Tools ──────────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/tools").methods("GET"_method)(
        [tools_handler](const crow::request& req) { return tools_handler->list_tools(req); });

    CROW_ROUTE(app_, "/api/v1/tools").methods("POST"_method)(
        [tools_handler](const crow::request& req) { return tools_handler->create_tool(req); });

    CROW_ROUTE(app_, "/api/v1/tools/<string>").methods("PATCH"_method)(
        [tools_handler](const crow::request& req, const std::string& id) {
            return tools_handler->update_tool(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/tools/<string>").methods("DELETE"_method)(
        [tools_handler](const crow::request& req, const std::string& id) {
            return tools_handler->delete_tool(req, id);
        });

    // ─── MCP Servers ────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/mcp-servers").methods("GET"_method)(
        [tools_handler](const crow::request& req) { return tools_handler->list_mcp_servers(req); });

    CROW_ROUTE(app_, "/api/v1/mcp-servers").methods("POST"_method)(
        [tools_handler](const crow::request& req) { return tools_handler->add_mcp_server(req); });

    CROW_ROUTE(app_, "/api/v1/mcp-servers/<string>").methods("DELETE"_method)(
        [tools_handler](const crow::request& req, const std::string& id) {
            return tools_handler->remove_mcp_server(req, id);
        });

    // ─── Web Assets ─────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/web-assets").methods("GET"_method)(
        [web_assets_handler](const crow::request& req) { return web_assets_handler->list_web_assets(req); });

    CROW_ROUTE(app_, "/api/v1/web-assets").methods("POST"_method)(
        [web_assets_handler](const crow::request& req) { return web_assets_handler->create_web_asset(req); });

    CROW_ROUTE(app_, "/api/v1/web-assets/<string>").methods("GET"_method)(
        [web_assets_handler](const crow::request& req, const std::string& id) {
            return web_assets_handler->get_web_asset(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/web-assets/<string>").methods("DELETE"_method)(
        [web_assets_handler](const crow::request& req, const std::string& id) {
            return web_assets_handler->delete_web_asset(req, id);
        });

    // ─── Workflows ──────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/workflows").methods("GET"_method)(
        [workflow_handler](const crow::request& req) { return workflow_handler->list_workflows(req); });

    CROW_ROUTE(app_, "/api/v1/workflows/trigger").methods("POST"_method)(
        [workflow_handler](const crow::request& req) { return workflow_handler->trigger_workflow(req); });

    // ─── Dashboard ──────────────────────────────────────────────────────────
    CROW_ROUTE(app_, "/api/v1/summary").methods("GET"_method)(
        [dashboard_handler](const crow::request& req) { return dashboard_handler->summary(req); });

    // ═══════════════════════════════════════════════════════════════════════
    // ─── AI / LLM-POWERED AGENTS ───────────────────────────────────────────
    // ═══════════════════════════════════════════════════════════════════════

    CROW_ROUTE(app_, "/api/v1/ai/chat").methods("POST"_method)(
        [ai_chat_handler](const crow::request& req) { return ai_chat_handler->chat(req); });

    CROW_ROUTE(app_, "/api/v1/ai/agents/<string>/chat").methods("POST"_method)(
        [ai_chat_handler](const crow::request& req, const std::string& type) {
            return ai_chat_handler->chat_with_type(req, type);
        });

    CROW_ROUTE(app_, "/api/v1/ai/agents/<string>/session").methods("POST"_method)(
        [ai_chat_handler](const crow::request& req, const std::string& id) {
            return ai_chat_handler->chat_with_agent(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/ai/providers").methods("GET"_method)(
        [ai_chat_handler](const crow::request& req) { return ai_chat_handler->list_providers(req); });

    CROW_ROUTE(app_, "/api/v1/ai/agents").methods("POST"_method)(
        [ai_chat_handler](const crow::request& req) { return ai_chat_handler->create_ai_agent(req); });

    CROW_ROUTE(app_, "/api/v1/ai/agents").methods("GET"_method)(
        [ai_chat_handler](const crow::request& req) { return ai_chat_handler->list_ai_agents(req); });

    CROW_ROUTE(app_, "/api/v1/ai/agents/<string>").methods("DELETE"_method)(
        [ai_chat_handler](const crow::request& req, const std::string& id) {
            return ai_chat_handler->delete_ai_agent(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/ai/agents/<string>/config").methods("POST"_method)(
        [ai_chat_handler](const crow::request& req, const std::string& id) {
            return ai_chat_handler->update_agent_config(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/ai/agents/<string>/memory").methods("GET"_method)(
        [ai_chat_handler](const crow::request& req, const std::string& id) {
            return ai_chat_handler->get_agent_memory(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/ai/agents/<string>/memory").methods("DELETE"_method)(
        [ai_chat_handler](const crow::request& req, const std::string& id) {
            return ai_chat_handler->clear_agent_memory(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/ai/agents/<string>/remember").methods("POST"_method)(
        [ai_chat_handler](const crow::request& req, const std::string& id) {
            return ai_chat_handler->remember(req, id);
        });

    CROW_ROUTE(app_, "/api/v1/ai/intent").methods("POST"_method)(
        [ai_chat_handler](const crow::request& req) { return ai_chat_handler->detect_intent(req); });

    CROW_ROUTE(app_, "/api/v1/ai/tools/<string>").methods("POST"_method)(
        [ai_chat_handler](const crow::request& req, const std::string& tool) {
            return ai_chat_handler->execute_tool(req, tool);
        });

    CROW_ROUTE(app_, "/api/v1/ai/health").methods("GET"_method)(
        [ai_chat_handler](const crow::request& req) { return ai_chat_handler->ai_health(req); });

}

void ApiServer::start() {
    if (running_.exchange(true)) return;
    spdlog::info("Starting API server on {}:{}", config_.host, config_.port);
    app_.port(config_.port).concurrency(config_.threads).bindaddr(config_.host).run();
}

void ApiServer::stop() {
    if (!running_.exchange(false)) return;
    spdlog::info("Stopping API server");
    app_.stop();
}

}  // namespace prodxcloud::api
