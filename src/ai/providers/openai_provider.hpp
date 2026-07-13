#pragma once

/// @file openai_provider.hpp
/// @brief OpenAI GPT API provider — GPT-4o, GPT-4-turbo, GPT-3.5-turbo.

#include "ai/llm_provider.hpp"

namespace prodxcloud::ai::providers {

class OpenAIProvider : public LLMProvider {
public:
    OpenAIProvider();
    ~OpenAIProvider() override = default;

    Result<LLMResponse> chat(const std::vector<ChatMessage>& messages,
                              const std::vector<ToolDefinition>& tools = {},
                              const LLMConfig& config = {}) override;

    Result<LLMResponse> chat_stream(const std::vector<ChatMessage>& messages,
                                     StreamCallback callback,
                                     const std::vector<ToolDefinition>& tools = {},
                                     const LLMConfig& config = {}) override;

    [[nodiscard]] std::string provider_name() const override { return "openai"; }
    [[nodiscard]] std::vector<std::string> available_models() const override;
    [[nodiscard]] bool supports_tool_use() const override { return true; }
    [[nodiscard]] bool supports_streaming() const override { return true; }

private:
    static constexpr const char* DEFAULT_BASE_URL = "https://api.openai.com/v1";
    static constexpr const char* DEFAULT_MODEL = "gpt-4o";

    std::string build_request_body(const std::vector<ChatMessage>& messages,
                                    const std::vector<ToolDefinition>& tools,
                                    const LLMConfig& config);
    Result<LLMResponse> parse_response(const std::string& raw_json);
    Result<std::string> http_post(const std::string& url, const std::string& body,
                                   const std::string& api_key, int32_t timeout_sec);
};

}  // namespace prodxcloud::ai::providers
