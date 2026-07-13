#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <crow.h>
#include "common/types.hpp"

// Forward declarations — no local inference or model store
namespace prodxcloud::inference { class RemoteModelRegistry; }
namespace prodxcloud::agents { class AgentController; }
namespace prodxcloud::storage { class VectorStore; }

namespace prodxcloud::api::handlers {
    class InferenceHandler;
    class AgentHandler;
    class SpecializedAgentHandler;
    class DatasetHandler;
    class AnalysisHandler;
    class CatalogHandler;
    class DeploymentHandler;
    class KnowledgeHandler;
    class FineTuningHandler;
    class TrainingHandler;
    class EvaluationHandler;
    class ToolsHandler;
    class WebAssetsHandler;
    class WorkflowHandler;
    class DashboardHandler;
    class AIChatHandler;
}

namespace prodxcloud::api {

struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8788;
    int threads = 4;
    bool enable_cors = true;
    int max_request_body_bytes = 10 * 1024 * 1024;
};

struct ServerDeps {
    // Remote model registry — no local inference engine
    std::shared_ptr<inference::RemoteModelRegistry> model_registry;
    std::shared_ptr<agents::AgentController> agent_controller;
    std::shared_ptr<storage::VectorStore> vector_store;
};

class ApiServer {
public:
    ApiServer(const ServerConfig& config, ServerDeps deps);
    ~ApiServer();

    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    void start();
    void stop();
    bool is_running() const { return running_.load(); }

private:
    void register_routes();
    void register_middleware();

    ServerConfig config_;
    ServerDeps deps_;
    crow::SimpleApp app_;
    std::atomic<bool> running_{false};
};

}  // namespace prodxcloud::api
