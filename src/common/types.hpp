#pragma once

/// @file types.hpp
/// @brief Common type definitions used across all ProdxCloud AI Engine modules.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

namespace prodxcloud {

// ─── Error Handling ─────────────────────────────────────────────────────────

struct Error {
    int code;
    std::string message;
    std::string detail;

    static Error internal(std::string msg) { return {500, std::move(msg), ""}; }
    static Error not_found(std::string msg) { return {404, std::move(msg), ""}; }
    static Error bad_request(std::string msg) { return {400, std::move(msg), ""}; }
    static Error validation(std::string msg) { return {422, std::move(msg), ""}; }
    static Error auth(std::string msg) { return {401, std::move(msg), ""}; }
    static Error unauthorized(std::string msg) { return {401, std::move(msg), ""}; }
    static Error rate_limited(std::string msg) { return {429, std::move(msg), ""}; }
    static Error timeout(std::string msg) { return {408, std::move(msg), ""}; }
};

template <typename T>
using Result = std::expected<T, Error>;

// ─── Time Aliases ───────────────────────────────────────────────────────────

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration  = Clock::duration;
using Millis    = std::chrono::milliseconds;

inline std::string now_iso8601() {
    auto now    = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t));
    return buf;
}

// ─── Inference Types ────────────────────────────────────────────────────────

struct InferenceRequest {
    std::string prompt;
    int32_t max_tokens     = 2048;
    float temperature      = 0.7f;
    std::string tenant_id;
    std::string model_id;
    std::string request_id;
    bool stream            = false;
};

struct InferenceResult {
    std::string text;
    int32_t tokens_generated = 0;
    double latency_ms        = 0.0;
    std::string model_id;
    std::string request_id;
};

// ─── Task Types ─────────────────────────────────────────────────────────────

struct Task {
    std::string id;
    std::string agent_id;
    std::string tenant_id;
    std::string type;
    std::string payload;
    std::string input_json;
    int32_t priority = 0;
    TimePoint created_at = Clock::now();
};

struct TaskResult {
    std::string task_id;
    bool success = false;
    std::string output;
    double duration_ms = 0.0;
    std::string error_message;
};

// ─── Model Types ────────────────────────────────────────────────────────────

enum class ModelFormat { GGUF, ONNX, UNKNOWN };

struct ModelConfig {
    std::string id;
    std::string name;
    ModelFormat format = ModelFormat::GGUF;
    std::string path;
    std::string backend = "cpu";
    int32_t context_length = 4096;
    std::string quantization;
};

struct ModelArtifact {
    std::string model_id;
    std::string version;
    std::string path;
    std::string format;
    size_t size_bytes     = 0;
    std::string checksum;
    std::string status    = "registered";
    std::string metadata_json = "{}";
    std::string created_at;
};

// ─── Dataset Types ──────────────────────────────────────────────────────────

struct DatasetRow {
    std::unordered_map<std::string, std::string> fields;
};

struct Dataset {
    std::string id;
    std::string name;
    std::string tenant_id;
    std::vector<std::string> column_names;
    std::vector<DatasetRow> rows;
};

// ─── Agent Config ───────────────────────────────────────────────────────────

struct AgentConfig {
    std::string id;
    std::string name;
    std::string tenant_id;
    std::string policy_name;
    int32_t max_concurrent_tasks = 1;
    int32_t max_retries          = 3;
    int32_t timeout_ms           = 30000;
};

// ─── Catalog Types ─────────────────────────────────────────────────────────

struct CatalogItem {
    std::string id;
    std::string name;
    std::string provider;
    std::string type;        // e.g. "chat", "completion", "embedding"
    std::string source;      // e.g. "openai", "azure", "huggingface", "local"
    std::string version;
    std::string description;
    std::string status = "available";
};

// ─── Deployment Types ──────────────────────────────────────────────────────

struct Deployment {
    std::string id;
    std::string tenant_id;
    std::string model_id;
    std::string name;
    std::string endpoint_url;
    std::string region;
    std::string status = "stopped";    // running, stopped, provisioning, failed
    int64_t tokens_used = 0;
    int32_t requests_per_min = 0;
    double cost_usd = 0.0;
    std::string sku;
    std::string config_json = "{}";
    std::string created_at;
};

// ─── Knowledge Base Types ──────────────────────────────────────────────────

struct KnowledgeBase {
    std::string id;
    std::string tenant_id;
    std::string name;
    std::string type;        // vector-store, dataset, documents
    int64_t size_bytes = 0;
    int32_t doc_count = 0;
    std::string status = "active";
    std::string created_at;
    std::string updated_at;
};

// ─── Fine-Tuning Types ────────────────────────────────────────────────────

struct FineTuningJob {
    std::string id;
    std::string tenant_id;
    std::string name;
    std::string base_model;
    std::string status = "pending";    // pending, running, completed, failed
    std::string training_file;
    int32_t epochs = 3;
    int32_t batch_size = 8;
    float learning_rate = 0.0001f;
    float progress = 0.0f;
    std::string created_at;
};

// ─── Training Types ───────────────────────────────────────────────────────

struct TrainingJob {
    std::string id;
    std::string tenant_id;
    std::string name;
    std::string type;          // pre-training, rlhf, distillation, sft
    std::string base_model;
    std::string status = "pending";
    int32_t current_epoch = 0;
    int32_t total_epochs = 10;
    float loss = 0.0f;
    float accuracy = 0.0f;
    std::string gpu_type;
    std::string dataset_id;
    std::string created_at;
};

// ─── Evaluation Types ─────────────────────────────────────────────────────

struct EvalRun {
    std::string id;
    std::string tenant_id;
    std::string name;
    std::string model_id;
    std::string evaluator_name;
    std::string status = "pending";    // pending, running, completed, failed
    std::string metrics_json = "{}";
    int32_t total_prompts = 0;
    int32_t completed_prompts = 0;
    std::string created_at;
};

struct Feedback {
    std::string id;
    std::string request_id;
    std::string model_id;
    std::string tenant_id;
    std::string feedback_type;     // thumbs_up, thumbs_down, rating, text
    std::string feedback_value;
    std::string feedback_text;
    std::string query;
    std::string response;
    std::string created_at;
};

// ─── Tool Types ───────────────────────────────────────────────────────────

struct Tool {
    std::string id;
    std::string name;
    std::string description;
    std::string type;              // function, api, plugin
    bool enabled = true;
};

struct MCPServer {
    std::string id;
    std::string name;
    std::string description;
    std::string endpoint;
    std::string protocol;          // stdio, http, websocket
    std::string status = "disconnected";
    int32_t tools_count = 0;
    int32_t resources_count = 0;
};

// ─── Web Asset Types ──────────────────────────────────────────────────────

struct WebAsset {
    std::string id;
    std::string name;
    std::string type;              // chatbot, embed, api
    std::string agent_id;
    std::string config_json = "{}";
    int64_t views = 0;
    int64_t interactions = 0;
    std::string status = "active";
    std::string created_at;
};

// ─── Workflow Types ───────────────────────────────────────────────────────

struct Workflow {
    std::string id;
    std::string name;
    std::string description;
    std::string status = "inactive";   // active, inactive, running
    std::string trigger;               // manual, schedule, event
    std::string steps_json = "[]";
    std::string created_at;
};

// ─── Chat Types ───────────────────────────────────────────────────────────

struct ChatMessage {
    std::string id;
    std::string agent_id;
    std::string role;              // user, assistant, system
    std::string content;
    std::string timestamp;
};

// ─── Dataset Info (extended for CRUD) ─────────────────────────────────────

struct DatasetInfo {
    std::string id;
    std::string tenant_id;
    std::string name;
    std::string description;
    std::string format;            // csv, json, parquet
    int64_t size_bytes = 0;
    int32_t row_count = 0;
    std::string status = "ready";
    std::string created_at;
};

// ─── Cancellation Token ─────────────────────────────────────────────────────

class CancellationToken {
public:
    void cancel() { cancelled_.store(true, std::memory_order_release); }
    [[nodiscard]] bool is_cancelled() const { return cancelled_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> cancelled_{false};
};

}  // namespace prodxcloud
