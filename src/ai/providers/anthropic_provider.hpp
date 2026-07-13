#pragma once

/// @file anthropic_provider.hpp
/// @brief Anthropic Claude API provider — Claude Sonnet 4, Opus 4, Haiku.

#include "ai/llm_provider.hpp"

namespace prodxcloud::ai::providers {

class AnthropicProvider : public LLMProvider {
public:
    AnthropicProvider();
    ~AnthropicProvider() override = default;

    Result<LLMResponse> chat(const std::vector<ChatMessage>& messages,
                              const std::vector<ToolDefinition>& tools = {},
                              const LLMConfig& config = {}) override;

    Result<LLMResponse> chat_stream(const std::vector<ChatMessage>& messages,
                                     StreamCallback callback,
                                     const std::vector<ToolDefinition>& tools = {},
                                     const LLMConfig& config = {}) override;

    [[nodiscard]] std::string provider_name() const override { return "anthropic"; }
    [[nodiscard]] std::vector<std::string> available_models() const override;
    [[nodiscard]] bool supports_tool_use() const override { return true; }
    [[nodiscard]] bool supports_streaming() const override { return true; }

private:
    static constexpr const char* DEFAULT_BASE_URL = "https://api.anthropic.com/v1";
    static constexpr const char* DEFAULT_MODEL = "claude-sonnet-4-20250514";
    static constexpr const char* API_VERSION = "2023-06-01";

    std::string build_request_body(const std::vector<ChatMessage>& messages,
                                    const std::vector<ToolDefinition>& tools,
                                    const LLMConfig& config);
    Result<LLMResponse> parse_response(const std::string& raw_json);
    Result<std::string> http_post(const std::string& url, const std::string& body,
                                   const std::string& api_key, int32_t timeout_sec);
};

}  // namespace prodxcloud::ai::providers
