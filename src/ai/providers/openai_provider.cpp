/// @file openai_provider.cpp
/// @brief OpenAI GPT API provider implementation.
///
/// Communicates with the OpenAI Chat Completions API using curl via popen.
/// Supports: gpt-4o, gpt-4o-mini, gpt-4-turbo, gpt-3.5-turbo.

#include "ai/providers/openai_provider.hpp"
#include "common/uuid.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace prodxcloud::ai::providers {

// ─── Constructor ───────────────────────────────────────────────────────────

OpenAIProvider::OpenAIProvider() {
    spdlog::debug("OpenAIProvider initialized");
}

// ─── Available Models ──────────────────────────────────────────────────────

std::vector<std::string> OpenAIProvider::available_models() const {
    return {
        "gpt-4o",
        "gpt-4o-mini",
        "gpt-4-turbo",
        "gpt-3.5-turbo"
    };
}

// ─── HTTP POST via curl/popen ──────────────────────────────────────────────

Result<std::string> OpenAIProvider::http_post(const std::string& url,
                                               const std::string& body,
                                               const std::string& api_key,
                                               int32_t timeout_sec) {
    // Escape single quotes in the body for safe shell embedding
    std::string escaped_body;
    escaped_body.reserve(body.size() + 64);
    for (char c : body) {
        if (c == '\'') {
            escaped_body += "'\\''";
        } else {
            escaped_body += c;
        }
    }

    std::ostringstream cmd;
    cmd << "echo '" << escaped_body << "'"
        << " | curl -s -X POST"
        << " --max-time " << timeout_sec
        << " -H \"Content-Type: application/json\""
        << " -H \"Authorization: Bearer " << api_key << "\""
        << " --data @-"
        << " \"" << url << "\""
        << " 2>&1";

    spdlog::debug("OpenAI HTTP POST to {}", url);

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return std::unexpected(Error::internal("Failed to open curl process for OpenAI API"));
    }

    std::string response;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        response += buffer.data();
    }

    int exit_code = pclose(pipe);
    if (exit_code != 0 && response.empty()) {
        return std::unexpected(Error::internal(
            "curl process failed with exit code " + std::to_string(exit_code)));
    }

    if (response.empty()) {
        return std::unexpected(Error::internal("Empty response from OpenAI API"));
    }

    return response;
}

// ─── Build Request Body ────────────────────────────────────────────────────

std::string OpenAIProvider::build_request_body(const std::vector<ChatMessage>& messages,
                                                const std::vector<ToolDefinition>& tools,
                                                const LLMConfig& config) {
    json body;

    // Model
    std::string model = config.model.empty() ? DEFAULT_MODEL : config.model;
    body["model"] = model;

    // Temperature
    body["temperature"] = config.temperature;

    // Top-p
    if (config.top_p < 1.0f) {
        body["top_p"] = config.top_p;
    }

    // Max tokens
    if (config.max_tokens > 0) {
        body["max_tokens"] = config.max_tokens;
    }

    // Build messages array — OpenAI supports system messages inline
    json msg_array = json::array();

    // Prepend system prompt from config if provided and no system message exists
    bool has_system_message = std::any_of(messages.begin(), messages.end(),
        [](const ChatMessage& m) { return m.role == "system"; });

    if (!has_system_message && !config.system_prompt.empty()) {
        json sys_msg;
        sys_msg["role"] = "system";
        sys_msg["content"] = config.system_prompt;
        msg_array.push_back(sys_msg);
    }

    for (const auto& msg : messages) {
        json m;
        m["role"] = msg.role;

        if (msg.role == "assistant" && !msg.tool_calls_json.empty()) {
            // Assistant message with tool_calls
            if (!msg.content.empty()) {
                m["content"] = msg.content;
            } else {
                m["content"] = nullptr;
            }

            // Parse tool_calls_json into OpenAI format
            auto tc_arr = json::parse(msg.tool_calls_json, nullptr, false);
            if (!tc_arr.is_discarded() && tc_arr.is_array()) {
                json openai_tool_calls = json::array();
                for (const auto& tc : tc_arr) {
                    json tool_call;
                    tool_call["id"] = tc.value("id", generate_uuid());
                    tool_call["type"] = "function";

                    json func;
                    func["name"] = tc.value("name", "");
                    // Arguments may be an object (need to stringify) or already a string
                    if (tc.contains("arguments")) {
                        if (tc["arguments"].is_string()) {
                            func["arguments"] = tc["arguments"].get<std::string>();
                        } else {
                            func["arguments"] = tc["arguments"].dump();
                        }
                    } else if (tc.contains("input")) {
                        func["arguments"] = tc["input"].dump();
                    } else {
                        func["arguments"] = "{}";
                    }
                    tool_call["function"] = func;
                    openai_tool_calls.push_back(tool_call);
                }
                m["tool_calls"] = openai_tool_calls;
            }
        } else if (msg.role == "tool") {
            // Tool result message
            m["role"] = "tool";
            m["content"] = msg.content;
            m["tool_call_id"] = msg.tool_call_id;
            if (!msg.name.empty()) {
                m["name"] = msg.name;
            }
        } else {
            m["content"] = msg.content;
        }

        msg_array.push_back(m);
    }

    body["messages"] = msg_array;

    // Tools
    if (!tools.empty()) {
        json tools_array = json::array();
        for (const auto& tool : tools) {
            auto tool_json = json::parse(tool.to_openai_json(), nullptr, false);
            if (!tool_json.is_discarded()) {
                tools_array.push_back(tool_json);
            }
        }
        body["tools"] = tools_array;
    }

    // Streaming
    if (config.stream) {
        body["stream"] = true;
    }

    return body.dump();
}

// ─── Parse Response ────────────────────────────────────────────────────────

Result<LLMResponse> OpenAIProvider::parse_response(const std::string& raw_json) {
    auto parsed = json::parse(raw_json, nullptr, false);
    if (parsed.is_discarded()) {
        return std::unexpected(Error::internal(
            "Failed to parse OpenAI API response as JSON"));
    }

    // Check for API-level errors
    if (parsed.contains("error")) {
        std::string err_type = parsed["error"].value("type", "unknown");
        std::string err_msg = parsed["error"].value("message", "Unknown error");
        std::string err_code = parsed["error"].value("code", "");
        spdlog::error("OpenAI API error: type={}, code={}, message={}", err_type, err_code, err_msg);

        if (err_type == "authentication_error" || err_code == "invalid_api_key") {
            return std::unexpected(Error::auth("OpenAI API authentication failed: " + err_msg));
        }
        if (err_type == "rate_limit_error" || err_code == "rate_limit_exceeded") {
            return std::unexpected(Error::rate_limited("OpenAI rate limit: " + err_msg));
        }
        if (err_code == "context_length_exceeded") {
            return std::unexpected(Error::bad_request("OpenAI context length exceeded: " + err_msg));
        }
        return std::unexpected(Error::internal("OpenAI API error: " + err_msg));
    }

    LLMResponse response;
    response.provider = "openai";
    response.raw_response = raw_json;
    response.model = parsed.value("model", "");

    // Parse usage
    if (parsed.contains("usage")) {
        response.input_tokens = parsed["usage"].value("prompt_tokens", 0);
        response.output_tokens = parsed["usage"].value("completion_tokens", 0);
    }

    // Parse choices array
    if (!parsed.contains("choices") || !parsed["choices"].is_array() || parsed["choices"].empty()) {
        return std::unexpected(Error::internal("OpenAI response missing choices array"));
    }

    const auto& choice = parsed["choices"][0];
    response.finish_reason = choice.value("finish_reason", "stop");

    // Normalize finish_reason
    if (response.finish_reason == "tool_calls") {
        response.finish_reason = "tool_use";
    }

    if (choice.contains("message")) {
        const auto& message = choice["message"];

        // Extract text content
        if (message.contains("content") && !message["content"].is_null()) {
            response.content = message["content"].get<std::string>();
        }

        // Extract tool calls
        if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
            for (const auto& tc : message["tool_calls"]) {
                ToolCall tool_call;
                tool_call.id = tc.value("id", generate_uuid());

                if (tc.contains("function")) {
                    tool_call.name = tc["function"].value("name", "");
                    tool_call.arguments_json = tc["function"].value("arguments", "{}");
                }

                response.tool_calls.push_back(tool_call);
            }
        }
    }

    spdlog::debug("OpenAI response parsed: content_len={}, tool_calls={}, tokens=({},{})",
                  response.content.size(), response.tool_calls.size(),
                  response.input_tokens, response.output_tokens);

    return response;
}

// ─── Chat (non-streaming) ──────────────────────────────────────────────────

Result<LLMResponse> OpenAIProvider::chat(const std::vector<ChatMessage>& messages,
                                          const std::vector<ToolDefinition>& tools,
                                          const LLMConfig& config) {
    auto start = std::chrono::steady_clock::now();

    // Resolve API key
    std::string api_key = config.api_key;
    if (api_key.empty()) {
        const char* env_key = std::getenv("OPENAI_API_KEY");
        if (env_key) {
            api_key = env_key;
        }
    }
    if (api_key.empty()) {
        return std::unexpected(Error::auth(
            "OpenAI API key not set. Provide via config or OPENAI_API_KEY env var."));
    }

    // Build a non-streaming config copy
    LLMConfig request_config = config;
    request_config.stream = false;

    // Resolve base URL
    std::string base_url = config.base_url.empty() ? DEFAULT_BASE_URL : config.base_url;
    std::string url = base_url + "/chat/completions";

    // Build request
    std::string body = build_request_body(messages, tools, request_config);
    spdlog::debug("OpenAI request body size: {} bytes", body.size());

    // Send request
    auto http_result = http_post(url, body, api_key, config.timeout_seconds);
    if (!http_result.has_value()) {
        return std::unexpected(http_result.error());
    }

    // Parse response
    auto response_result = parse_response(http_result.value());
    if (!response_result.has_value()) {
        return std::unexpected(response_result.error());
    }

    auto& response = response_result.value();

    // Set latency
    auto end = std::chrono::steady_clock::now();
    response.latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    // Set model from config if not returned
    if (response.model.empty()) {
        response.model = request_config.model.empty() ? DEFAULT_MODEL : request_config.model;
    }

    spdlog::info("OpenAI chat completed: model={}, latency={:.1f}ms, tokens=({},{})",
                 response.model, response.latency_ms,
                 response.input_tokens, response.output_tokens);

    return response;
}

// ─── Chat Stream ───────────────────────────────────────────────────────────

Result<LLMResponse> OpenAIProvider::chat_stream(const std::vector<ChatMessage>& messages,
                                                 StreamCallback callback,
                                                 const std::vector<ToolDefinition>& tools,
                                                 const LLMConfig& config) {
    auto start = std::chrono::steady_clock::now();

    // Resolve API key
    std::string api_key = config.api_key;
    if (api_key.empty()) {
        const char* env_key = std::getenv("OPENAI_API_KEY");
        if (env_key) {
            api_key = env_key;
        }
    }
    if (api_key.empty()) {
        return std::unexpected(Error::auth(
            "OpenAI API key not set. Provide via config or OPENAI_API_KEY env var."));
    }

    // Build a streaming config
    LLMConfig stream_config = config;
    stream_config.stream = true;

    std::string base_url = config.base_url.empty() ? DEFAULT_BASE_URL : config.base_url;
    std::string url = base_url + "/chat/completions";

    std::string body = build_request_body(messages, tools, stream_config);

    // Escape body for shell
    std::string escaped_body;
    escaped_body.reserve(body.size() + 64);
    for (char c : body) {
        if (c == '\'') {
            escaped_body += "'\\''";
        } else {
            escaped_body += c;
        }
    }

    std::ostringstream cmd;
    cmd << "echo '" << escaped_body << "'"
        << " | curl -s -N -X POST"
        << " --max-time " << config.timeout_seconds
        << " -H \"Content-Type: application/json\""
        << " -H \"Authorization: Bearer " << api_key << "\""
        << " --data @-"
        << " \"" << url << "\""
        << " 2>&1";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return std::unexpected(Error::internal("Failed to open curl process for OpenAI streaming"));
    }

    LLMResponse response;
    response.provider = "openai";

    std::string accumulated_content;
    std::array<char, 8192> line_buf{};

    // Accumulators for tool calls being streamed incrementally
    struct PartialToolCall {
        std::string id;
        std::string name;
        std::string arguments_accum;
    };
    std::vector<PartialToolCall> partial_tools;

    while (fgets(line_buf.data(), static_cast<int>(line_buf.size()), pipe) != nullptr) {
        std::string line(line_buf.data());

        // Strip trailing whitespace
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }

        // OpenAI SSE: lines prefixed with "data: "
        if (line.rfind("data: ", 0) != 0) {
            continue;
        }

        std::string data_str = line.substr(6);

        // Terminal marker
        if (data_str == "[DONE]") {
            if (callback) {
                callback("", true);
            }
            break;
        }

        auto data = json::parse(data_str, nullptr, false);
        if (data.is_discarded()) {
            continue;
        }

        // Extract model on first chunk
        if (response.model.empty() && data.contains("model")) {
            response.model = data.value("model", "");
        }

        // Extract usage from final chunk (if present)
        if (data.contains("usage") && !data["usage"].is_null()) {
            response.input_tokens = data["usage"].value("prompt_tokens", 0);
            response.output_tokens = data["usage"].value("completion_tokens", 0);
        }

        if (!data.contains("choices") || !data["choices"].is_array() || data["choices"].empty()) {
            continue;
        }

        const auto& choice = data["choices"][0];

        // Finish reason
        if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
            response.finish_reason = choice["finish_reason"].get<std::string>();
            if (response.finish_reason == "tool_calls") {
                response.finish_reason = "tool_use";
            }
        }

        if (!choice.contains("delta")) {
            continue;
        }

        const auto& delta = choice["delta"];

        // Text content delta
        if (delta.contains("content") && !delta["content"].is_null()) {
            std::string text = delta["content"].get<std::string>();
            accumulated_content += text;
            if (callback) {
                callback(text, false);
            }
        }

        // Tool calls delta
        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
            for (const auto& tc_delta : delta["tool_calls"]) {
                int index = tc_delta.value("index", 0);

                // Ensure we have enough slots
                while (static_cast<int>(partial_tools.size()) <= index) {
                    partial_tools.push_back({});
                }

                auto& ptc = partial_tools[static_cast<size_t>(index)];

                if (tc_delta.contains("id") && !tc_delta["id"].is_null()) {
                    ptc.id = tc_delta["id"].get<std::string>();
                }
                if (tc_delta.contains("function")) {
                    const auto& func = tc_delta["function"];
                    if (func.contains("name") && !func["name"].is_null()) {
                        ptc.name = func["name"].get<std::string>();
                    }
                    if (func.contains("arguments") && !func["arguments"].is_null()) {
                        ptc.arguments_accum += func["arguments"].get<std::string>();
                    }
                }
            }
        }
    }

    pclose(pipe);

    response.content = accumulated_content;

    // Finalize partial tool calls
    for (const auto& ptc : partial_tools) {
        if (ptc.id.empty() && ptc.name.empty()) {
            continue;  // Skip empty slots
        }
        ToolCall tc;
        tc.id = ptc.id.empty() ? generate_uuid() : ptc.id;
        tc.name = ptc.name;
        tc.arguments_json = ptc.arguments_accum.empty() ? "{}" : ptc.arguments_accum;
        response.tool_calls.push_back(tc);
    }

    auto end = std::chrono::steady_clock::now();
    response.latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (response.model.empty()) {
        response.model = config.model.empty() ? DEFAULT_MODEL : config.model;
    }

    spdlog::info("OpenAI streaming completed: model={}, latency={:.1f}ms, tokens=({},{})",
                 response.model, response.latency_ms,
                 response.input_tokens, response.output_tokens);

    return response;
}

}  // namespace prodxcloud::ai::providers
