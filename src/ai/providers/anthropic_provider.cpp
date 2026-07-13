/// @file anthropic_provider.cpp
/// @brief Anthropic Claude API provider implementation.
///
/// Communicates with the Anthropic Messages API using curl via popen.
/// Supports: claude-sonnet-4-20250514, claude-opus-4-20250514, claude-haiku-4-5-20251001.

#include "ai/providers/anthropic_provider.hpp"
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

AnthropicProvider::AnthropicProvider() {
    spdlog::debug("AnthropicProvider initialized");
}

// ─── Available Models ──────────────────────────────────────────────────────

std::vector<std::string> AnthropicProvider::available_models() const {
    return {
        "claude-sonnet-4-20250514",
        "claude-opus-4-20250514",
        "claude-haiku-4-5-20251001"
    };
}

// ─── HTTP POST via curl/popen ──────────────────────────────────────────────

Result<std::string> AnthropicProvider::http_post(const std::string& url,
                                                  const std::string& body,
                                                  const std::string& api_key,
                                                  int32_t timeout_sec) {
    // Escape the body for safe shell embedding by writing to a temp approach:
    // We pipe the JSON body via stdin to curl using echo + pipe to avoid
    // shell-escaping issues with the JSON content.

    // Build the curl command. Use --data @- to read body from stdin.
    std::ostringstream cmd;
    cmd << "echo '"
        // Escape single quotes in the body for the shell: replace ' with '\''
        ;

    // Properly escape the body for single-quoted shell string
    std::string escaped_body;
    escaped_body.reserve(body.size() + 64);
    for (char c : body) {
        if (c == '\'') {
            escaped_body += "'\\''";
        } else {
            escaped_body += c;
        }
    }

    cmd << escaped_body
        << "' | curl -s -X POST"
        << " --max-time " << timeout_sec
        << " -H \"content-type: application/json\""
        << " -H \"x-api-key: " << api_key << "\""
        << " -H \"anthropic-version: " << API_VERSION << "\""
        << " --data @-"
        << " \"" << url << "\""
        << " 2>&1";

    spdlog::debug("Anthropic HTTP POST to {}", url);

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return std::unexpected(Error::internal("Failed to open curl process for Anthropic API"));
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
        return std::unexpected(Error::internal("Empty response from Anthropic API"));
    }

    return response;
}

// ─── Build Request Body ────────────────────────────────────────────────────

std::string AnthropicProvider::build_request_body(const std::vector<ChatMessage>& messages,
                                                   const std::vector<ToolDefinition>& tools,
                                                   const LLMConfig& config) {
    json body;

    // Model selection
    std::string model = config.model.empty() ? DEFAULT_MODEL : config.model;
    body["model"] = model;

    // Max tokens
    body["max_tokens"] = config.max_tokens > 0 ? config.max_tokens : 4096;

    // Temperature
    body["temperature"] = config.temperature;

    // Top-p
    if (config.top_p < 1.0f) {
        body["top_p"] = config.top_p;
    }

    // System prompt — Anthropic uses a top-level "system" field, not a message
    std::string system_prompt;
    json msg_array = json::array();

    for (const auto& msg : messages) {
        if (msg.role == "system") {
            // Accumulate system messages into the top-level system field
            if (!system_prompt.empty()) {
                system_prompt += "\n\n";
            }
            system_prompt += msg.content;
            continue;
        }

        json m;
        m["role"] = msg.role;

        if (msg.role == "assistant" && !msg.tool_calls_json.empty()) {
            // Assistant message with tool_use blocks
            json content_blocks = json::array();
            if (!msg.content.empty()) {
                content_blocks.push_back({{"type", "text"}, {"text", msg.content}});
            }
            // Parse the tool_calls_json and append tool_use blocks
            auto tc_arr = json::parse(msg.tool_calls_json, nullptr, false);
            if (!tc_arr.is_discarded() && tc_arr.is_array()) {
                for (const auto& tc : tc_arr) {
                    json tool_use;
                    tool_use["type"] = "tool_use";
                    tool_use["id"] = tc.value("id", generate_uuid());
                    tool_use["name"] = tc.value("name", "");
                    if (tc.contains("input")) {
                        tool_use["input"] = tc["input"];
                    } else if (tc.contains("arguments")) {
                        // Parse arguments string to object
                        auto args = json::parse(tc["arguments"].get<std::string>(), nullptr, false);
                        tool_use["input"] = args.is_discarded() ? json::object() : args;
                    } else {
                        tool_use["input"] = json::object();
                    }
                    content_blocks.push_back(tool_use);
                }
            }
            m["content"] = content_blocks;
        } else if (msg.role == "tool") {
            // Tool result message — Anthropic expects role="user" with tool_result content block
            m["role"] = "user";
            json content_blocks = json::array();
            json tool_result;
            tool_result["type"] = "tool_result";
            tool_result["tool_use_id"] = msg.tool_call_id;
            tool_result["content"] = msg.content;
            content_blocks.push_back(tool_result);
            m["content"] = content_blocks;
        } else {
            m["content"] = msg.content;
        }

        msg_array.push_back(m);
    }

    // Use config system prompt if no system messages were in the conversation
    if (system_prompt.empty() && !config.system_prompt.empty()) {
        system_prompt = config.system_prompt;
    }

    if (!system_prompt.empty()) {
        body["system"] = system_prompt;
    }

    body["messages"] = msg_array;

    // Tools
    if (!tools.empty()) {
        json tools_array = json::array();
        for (const auto& tool : tools) {
            auto tool_json = json::parse(tool.to_anthropic_json(), nullptr, false);
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

Result<LLMResponse> AnthropicProvider::parse_response(const std::string& raw_json) {
    auto parsed = json::parse(raw_json, nullptr, false);
    if (parsed.is_discarded()) {
        return std::unexpected(Error::internal(
            "Failed to parse Anthropic API response as JSON"));
    }

    // Check for API-level errors
    if (parsed.contains("error")) {
        std::string err_type = parsed["error"].value("type", "unknown");
        std::string err_msg = parsed["error"].value("message", "Unknown error");
        spdlog::error("Anthropic API error: type={}, message={}", err_type, err_msg);

        if (err_type == "authentication_error") {
            return std::unexpected(Error::auth("Anthropic API authentication failed: " + err_msg));
        }
        if (err_type == "rate_limit_error") {
            return std::unexpected(Error::rate_limited("Anthropic rate limit: " + err_msg));
        }
        if (err_type == "overloaded_error") {
            return std::unexpected(Error::internal("Anthropic API overloaded: " + err_msg));
        }
        return std::unexpected(Error::internal("Anthropic API error: " + err_msg));
    }

    LLMResponse response;
    response.provider = "anthropic";
    response.raw_response = raw_json;
    response.model = parsed.value("model", "");

    // Parse stop reason
    std::string stop_reason = parsed.value("stop_reason", "");
    if (stop_reason == "end_turn") {
        response.finish_reason = "stop";
    } else if (stop_reason == "tool_use") {
        response.finish_reason = "tool_use";
    } else if (stop_reason == "max_tokens") {
        response.finish_reason = "length";
    } else {
        response.finish_reason = stop_reason;
    }

    // Parse usage
    if (parsed.contains("usage")) {
        response.input_tokens = parsed["usage"].value("input_tokens", 0);
        response.output_tokens = parsed["usage"].value("output_tokens", 0);
    }

    // Parse content array
    if (parsed.contains("content") && parsed["content"].is_array()) {
        json tool_calls_json_arr = json::array();

        for (const auto& block : parsed["content"]) {
            std::string block_type = block.value("type", "");

            if (block_type == "text") {
                if (!response.content.empty()) {
                    response.content += "\n";
                }
                response.content += block.value("text", "");
            } else if (block_type == "tool_use") {
                ToolCall tc;
                tc.id = block.value("id", generate_uuid());
                tc.name = block.value("name", "");
                if (block.contains("input")) {
                    tc.arguments_json = block["input"].dump();
                } else {
                    tc.arguments_json = "{}";
                }
                response.tool_calls.push_back(tc);

                // Also accumulate for the raw tool_calls representation
                json tc_json;
                tc_json["id"] = tc.id;
                tc_json["name"] = tc.name;
                tc_json["input"] = block.value("input", json::object());
                tool_calls_json_arr.push_back(tc_json);
            }
        }
    }

    spdlog::debug("Anthropic response parsed: content_len={}, tool_calls={}, tokens=({},{})",
                  response.content.size(), response.tool_calls.size(),
                  response.input_tokens, response.output_tokens);

    return response;
}

// ─── Chat (non-streaming) ──────────────────────────────────────────────────

Result<LLMResponse> AnthropicProvider::chat(const std::vector<ChatMessage>& messages,
                                             const std::vector<ToolDefinition>& tools,
                                             const LLMConfig& config) {
    auto start = std::chrono::steady_clock::now();

    // Resolve API key
    std::string api_key = config.api_key;
    if (api_key.empty()) {
        const char* env_key = std::getenv("ANTHROPIC_API_KEY");
        if (env_key) {
            api_key = env_key;
        }
    }
    if (api_key.empty()) {
        return std::unexpected(Error::auth(
            "Anthropic API key not set. Provide via config or ANTHROPIC_API_KEY env var."));
    }

    // Build a non-streaming config copy
    LLMConfig request_config = config;
    request_config.stream = false;

    // Resolve base URL
    std::string base_url = config.base_url.empty() ? DEFAULT_BASE_URL : config.base_url;
    std::string url = base_url + "/messages";

    // Build request
    std::string body = build_request_body(messages, tools, request_config);
    spdlog::debug("Anthropic request body size: {} bytes", body.size());

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

    spdlog::info("Anthropic chat completed: model={}, latency={:.1f}ms, tokens=({},{})",
                 response.model, response.latency_ms,
                 response.input_tokens, response.output_tokens);

    return response;
}

// ─── Chat Stream ───────────────────────────────────────────────────────────

Result<LLMResponse> AnthropicProvider::chat_stream(const std::vector<ChatMessage>& messages,
                                                    StreamCallback callback,
                                                    const std::vector<ToolDefinition>& tools,
                                                    const LLMConfig& config) {
    auto start = std::chrono::steady_clock::now();

    // Resolve API key
    std::string api_key = config.api_key;
    if (api_key.empty()) {
        const char* env_key = std::getenv("ANTHROPIC_API_KEY");
        if (env_key) {
            api_key = env_key;
        }
    }
    if (api_key.empty()) {
        return std::unexpected(Error::auth(
            "Anthropic API key not set. Provide via config or ANTHROPIC_API_KEY env var."));
    }

    // Build a streaming config
    LLMConfig stream_config = config;
    stream_config.stream = true;

    std::string base_url = config.base_url.empty() ? DEFAULT_BASE_URL : config.base_url;
    std::string url = base_url + "/messages";

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

    // Build curl command for streaming — we read line-by-line from the SSE stream
    std::ostringstream cmd;
    cmd << "echo '" << escaped_body << "'"
        << " | curl -s -N -X POST"
        << " --max-time " << config.timeout_seconds
        << " -H \"content-type: application/json\""
        << " -H \"x-api-key: " << api_key << "\""
        << " -H \"anthropic-version: " << API_VERSION << "\""
        << " --data @-"
        << " \"" << url << "\""
        << " 2>&1";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return std::unexpected(Error::internal("Failed to open curl process for Anthropic streaming"));
    }

    LLMResponse response;
    response.provider = "anthropic";

    // Anthropic SSE format: lines like "event: <type>\ndata: <json>\n\n"
    std::string accumulated_content;
    std::array<char, 8192> line_buf{};
    std::string current_event;

    // Accumulators for tool_use blocks being streamed
    struct PartialToolUse {
        std::string id;
        std::string name;
        std::string arguments_accum;
    };
    std::vector<PartialToolUse> partial_tools;

    while (fgets(line_buf.data(), static_cast<int>(line_buf.size()), pipe) != nullptr) {
        std::string line(line_buf.data());

        // Strip trailing newlines/carriage returns
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }

        // Parse SSE event type
        if (line.rfind("event: ", 0) == 0) {
            current_event = line.substr(7);
            continue;
        }

        // Parse SSE data
        if (line.rfind("data: ", 0) != 0) {
            continue;
        }

        std::string data_str = line.substr(6);

        // Handle terminal events
        if (current_event == "message_stop") {
            if (callback) {
                callback("", true);
            }
            break;
        }

        auto data = json::parse(data_str, nullptr, false);
        if (data.is_discarded()) {
            continue;
        }

        if (current_event == "message_start") {
            // Extract model and usage from message_start
            if (data.contains("message")) {
                response.model = data["message"].value("model", "");
                if (data["message"].contains("usage")) {
                    response.input_tokens = data["message"]["usage"].value("input_tokens", 0);
                }
            }
        } else if (current_event == "content_block_start") {
            if (data.contains("content_block")) {
                auto& block = data["content_block"];
                std::string btype = block.value("type", "");
                if (btype == "tool_use") {
                    PartialToolUse ptu;
                    ptu.id = block.value("id", generate_uuid());
                    ptu.name = block.value("name", "");
                    partial_tools.push_back(std::move(ptu));
                }
            }
        } else if (current_event == "content_block_delta") {
            if (data.contains("delta")) {
                auto& delta = data["delta"];
                std::string dtype = delta.value("type", "");

                if (dtype == "text_delta") {
                    std::string text = delta.value("text", "");
                    accumulated_content += text;
                    if (callback) {
                        callback(text, false);
                    }
                } else if (dtype == "input_json_delta") {
                    std::string partial_json = delta.value("partial_json", "");
                    if (!partial_tools.empty()) {
                        partial_tools.back().arguments_accum += partial_json;
                    }
                }
            }
        } else if (current_event == "message_delta") {
            // Final usage and stop reason
            if (data.contains("delta")) {
                std::string stop_reason = data["delta"].value("stop_reason", "");
                if (stop_reason == "end_turn") {
                    response.finish_reason = "stop";
                } else if (stop_reason == "tool_use") {
                    response.finish_reason = "tool_use";
                } else if (stop_reason == "max_tokens") {
                    response.finish_reason = "length";
                } else {
                    response.finish_reason = stop_reason;
                }
            }
            if (data.contains("usage")) {
                response.output_tokens = data["usage"].value("output_tokens", 0);
            }
        }
    }

    pclose(pipe);

    response.content = accumulated_content;

    // Finalize partial tool calls
    for (const auto& ptu : partial_tools) {
        ToolCall tc;
        tc.id = ptu.id;
        tc.name = ptu.name;
        tc.arguments_json = ptu.arguments_accum.empty() ? "{}" : ptu.arguments_accum;
        response.tool_calls.push_back(tc);
    }

    auto end = std::chrono::steady_clock::now();
    response.latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (response.model.empty()) {
        response.model = config.model.empty() ? DEFAULT_MODEL : config.model;
    }

    spdlog::info("Anthropic streaming completed: model={}, latency={:.1f}ms, tokens=({},{})",
                 response.model, response.latency_ms,
                 response.input_tokens, response.output_tokens);

    return response;
}

}  // namespace prodxcloud::ai::providers
