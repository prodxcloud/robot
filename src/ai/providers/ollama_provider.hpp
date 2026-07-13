#pragma once

/// @file ollama_provider.hpp
/// @brief Ollama local LLM provider — LLaMA 3.1, Mistral, CodeLlama, Phi, Gemma.

#include "ai/llm_provider.hpp"

namespace prodxcloud::ai::providers {

class OllamaProvider : public LLMProvider {
public:
    OllamaProvider();
    ~OllamaProvider() override = default;

    Result<LLMResponse> chat(const std::vector<ChatMessage>& messages,
                              const std::vector<ToolDefinition>& tools = {},
                              const LLMConfig& config = {}) override;

    Result<LLMResponse> chat_stream(const std::vector<ChatMessage>& messages,
                                     StreamCallback callback,
                                     const std::vector<ToolDefinition>& tools = {},
                                     const LLMConfig& config = {}) override;

    [[nodiscard]] std::string provider_name() const override { return "ollama"; }
    [[nodiscard]] std::vector<std::string> available_models() const override;
    [[nodiscard]] bool supports_tool_use() const override { return true; }
    [[nodiscard]] bool supports_streaming() const override { return true; }

    // Ollama-specific
    Result<std::vector<std::string>> list_local_models();
    Result<void> pull_model(const std::string& model_name);

private:
    static constexpr const char* DEFAULT_BASE_URL = "http://localhost:11434";
    static constexpr const char* DEFAULT_MODEL = "llama3.1";

    std::string build_request_body(const std::vector<ChatMessage>& messages,
                                    const std::vector<ToolDefinition>& tools,
                                    const LLMConfig& config);
    Result<LLMResponse> parse_response(const std::string& raw_json);
    Result<std::string> http_post(const std::string& url, const std::string& body,
                                   int32_t timeout_sec);
};

}  // namespace prodxcloud::ai::providers
