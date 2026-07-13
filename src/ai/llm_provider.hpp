#pragma once

/// @file llm_provider.hpp
/// @brief Abstract LLM provider interface and multi-provider client.
///
/// Supports: Anthropic Claude, OpenAI GPT, Ollama (LLaMA/Mistral/local models).
/// Features: tool-use, streaming, system prompts, temperature control.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/types.hpp"

namespace prodxcloud::ai {

// ─── Message Types ──────────────────────────────────────────────────────────

struct ChatMessage {
    std::string role;               // system, user, assistant, tool
    std::string content;
    std::string name;               // for tool results
    std::string tool_call_id;       // for tool results
    std::string tool_calls_json;    // assistant's tool_use blocks (JSON array)
};

// ─── Tool Definition ────────────────────────────────────────────────────────

struct ToolParameter {
    std::string name;
    std::string type;               // string, integer, number, boolean, array, object
    std::string description;
    bool required = false;
    std::string enum_values_json = "[]";  // for enum types
    std::string default_value;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<ToolParameter> parameters;

    std::string to_openai_json() const;
    std::string to_anthropic_json() const;
    std::string to_ollama_json() const;
};

// ─── Tool Call (parsed from LLM response) ───────────────────────────────────

struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments_json;     // raw JSON arguments
};

// ─── LLM Response ───────────────────────────────────────────────────────────

struct LLMResponse {
    std::string content;            // text content
    std::string finish_reason;      // stop, tool_use, length, error
    std::vector<ToolCall> tool_calls;
    int32_t input_tokens = 0;
    int32_t output_tokens = 0;
    double latency_ms = 0.0;
    std::string model;
    std::string provider;
    std::string raw_response;       // full raw JSON for debugging
    bool has_tool_calls() const { return !tool_calls.empty(); }
};

// ─── LLM Config ─────────────────────────────────────────────────────────────

struct LLMConfig {
    std::string provider;           // anthropic, openai, ollama
    std::string model;              // claude-sonnet-4-20250514, gpt-4o, llama3.1, etc.
    std::string api_key;
    std::string base_url;           // override for custom endpoints
    float temperature = 0.7f;
    int32_t max_tokens = 4096;
    float top_p = 1.0f;
    std::string system_prompt;
    bool stream = false;
    int32_t timeout_seconds = 120;
};

// ─── Streaming Callback ─────────────────────────────────────────────────────

using StreamCallback = std::function<void(const std::string& chunk, bool is_done)>;

// ─── Abstract Provider Interface ────────────────────────────────────────────

class LLMProvider {
public:
    virtual ~LLMProvider() = default;

    virtual Result<LLMResponse> chat(const std::vector<ChatMessage>& messages,
                                      const std::vector<ToolDefinition>& tools = {},
                                      const LLMConfig& config = {}) = 0;

    virtual Result<LLMResponse> chat_stream(const std::vector<ChatMessage>& messages,
                                             StreamCallback callback,
                                             const std::vector<ToolDefinition>& tools = {},
                                             const LLMConfig& config = {}) = 0;

    [[nodiscard]] virtual std::string provider_name() const = 0;
    [[nodiscard]] virtual std::vector<std::string> available_models() const = 0;
    [[nodiscard]] virtual bool supports_tool_use() const = 0;
    [[nodiscard]] virtual bool supports_streaming() const = 0;
};

// ─── Provider Factory ───────────────────────────────────────────────────────

class LLMProviderFactory {
public:
    static std::unique_ptr<LLMProvider> create(const std::string& provider);
    static std::unique_ptr<LLMProvider> create(const LLMConfig& config);
    static std::vector<std::string> available_providers();
};

}  // namespace prodxcloud::ai
