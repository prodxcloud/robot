#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "api/server.hpp"
#include "inference/remote_registry.hpp"
#include "agents/agent_controller.hpp"
#include "storage/vector_store.hpp"
#include "telemetry/logger.hpp"
#include "telemetry/metrics.hpp"
#include "telemetry/terminal_dashboard.hpp"

using namespace prodxcloud;

static std::unique_ptr<api::ApiServer> g_server;
static std::unique_ptr<telemetry::TerminalDashboard> g_dashboard;

static void signal_handler(int sig) {
    spdlog::info("Received signal {}, shutting down...", sig);
    if (g_dashboard) g_dashboard->stop();
    if (g_server) g_server->stop();
}

int main(int argc, char* argv[]) {
    telemetry::StructuredLogger::init(telemetry::LogLevel::INFO, true);
    spdlog::info("VA AgentControl starting (orchestration-only mode)...");

    std::string config_path = "config/server.yaml";
    if (argc > 1) config_path = argv[1];

    YAML::Node config;
    try {
        config = YAML::LoadFile(config_path);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load config {}: {}", config_path, e.what());
        return 1;
    }

    auto read_string = [](const YAML::Node& node,
                          std::initializer_list<const char*> keys,
                          const std::string& fallback) {
        for (const auto* key : keys) {
            if (node[key]) return node[key].as<std::string>();
        }
        return fallback;
    };
    auto read_int = [](const YAML::Node& node,
                       std::initializer_list<const char*> keys,
                       int fallback) {
        for (const auto* key : keys) {
            if (node[key]) return node[key].as<int>();
        }
        return fallback;
    };

    // ── Server config ────────────────────────────────────────────────────────
    api::ServerConfig srv_cfg;
    if (auto s = config["server"]) {
        srv_cfg.host    = read_string(s, {"host"}, srv_cfg.host);
        srv_cfg.port    = read_int(s, {"port", "http_port"}, srv_cfg.port);
        srv_cfg.threads = read_int(s, {"threads", "worker_threads"}, srv_cfg.threads);
    }
    // ── Remote model registry (no local loading) ─────────────────────────────
    std::string slm_url;
    if (auto slm = config["slm_service"]) {
        slm_url = read_string(slm, {"url"}, "");
    }
    // Allow env override
    if (const char* env = std::getenv("SLM_SERVICE_URL")) slm_url = env;

    auto model_registry = std::make_shared<inference::RemoteModelRegistry>(slm_url);

    // Register known remote models from config (catalog only — no loading)
    YAML::Node models = config["models"];
    if (!models && config["models_config"]) {
        try {
            YAML::Node mf = YAML::LoadFile(config["models_config"].as<std::string>());
            models = mf["models"];
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load models_config: {}", e.what());
        }
    }
    if (models) {
        for (const auto& m : models) {
            inference::RemoteModelEntry entry;
            entry.id       = m["id"].as<std::string>("");
            entry.name     = m["name"].as<std::string>(entry.id);
            entry.provider = m["provider"].as<std::string>("slm-models");
            entry.endpoint = m["endpoint"].as<std::string>(slm_url);
            if (!entry.id.empty()) {
                model_registry->register_model(entry);
                spdlog::info("Registered remote model: {} ({})", entry.id, entry.provider);
            }
        }
    }

    if (slm_url.empty()) {
        spdlog::warn("SLM_SERVICE_URL not set — inference endpoints will return 503");
    } else {
        spdlog::info("SLM-Models service: {}", slm_url);
    }

    // ── Agent system ─────────────────────────────────────────────────────────
    spdlog::info("Initializing agent system...");
    agents::ControllerConfig ac_cfg;
    if (auto a = config["agents"]) {
        ac_cfg.max_agents_per_tenant = a["max_agents_per_tenant"].as<size_t>(50);
        ac_cfg.thread_pool_size      = a["thread_pool_size"].as<size_t>(16);
    }
    auto agent_controller = std::make_shared<agents::AgentController>(ac_cfg);

    // ── Vector store (embeddings only — no model weights) ────────────────────
    spdlog::info("Initializing vector store...");
    storage::VectorStoreConfig vs_cfg;
    if (auto vec = config["vector_store"]) {
        vs_cfg.dimension    = vec["dimension"].as<int>(768);
        vs_cfg.max_vectors  = vec["max_vectors"].as<size_t>(1'000'000);
        vs_cfg.storage_path = vec["path"].as<std::string>("data/vectors");
    } else if (auto storage = config["storage"]) {
        if (storage["vector_store_path"])
            vs_cfg.storage_path = storage["vector_store_path"].as<std::string>(vs_cfg.storage_path);
    }
    auto vector_store = std::make_shared<storage::VectorStore>(vs_cfg);

    // ── Metrics ──────────────────────────────────────────────────────────────
    telemetry::PrometheusMetrics::instance();

    // ── Build & start server ─────────────────────────────────────────────────
    api::ServerDeps deps{
        .model_registry   = model_registry,
        .agent_controller = agent_controller,
        .vector_store     = vector_store
    };

    g_server = std::make_unique<api::ApiServer>(srv_cfg, std::move(deps));

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── Terminal dashboard ───────────────────────────────────────────────────
    telemetry::ServiceStats svc_stats;
    svc_stats.version = "1.0.0";
    svc_stats.host    = srv_cfg.host;
    svc_stats.port    = srv_cfg.port;
    svc_stats.slm_url = slm_url;

    // Print one-shot banner first (visible in logs / non-TTY)
    telemetry::TerminalDashboard::print_banner(svc_stats);

    // Start live dashboard only when attached to a real terminal
    if (isatty(STDOUT_FILENO)) {
        g_dashboard = std::make_unique<telemetry::TerminalDashboard>(svc_stats);
        g_dashboard->start(1.0);
    }

    spdlog::info("VA AgentControl ready on {}:{}", srv_cfg.host, srv_cfg.port);
    g_server->start();

    if (g_dashboard) g_dashboard->stop();
    spdlog::info("VA AgentControl shut down cleanly");
    return 0;
}
