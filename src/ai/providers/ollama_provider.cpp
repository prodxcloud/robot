/// @file ollama_provider.cpp
/// @brief Ollama local LLM provider implementation.
///
/// Communicates with the Ollama REST API (localhost:11434) using curl via popen.
/// Supports: llama3.1, llama3.2, mistral, codellama, phi3, gemma2.

#include "ai/providers/ollama_provider.hpp"
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

OllamaProvider::OllamaProvider() {
    spdlog::debug("OllamaProvider initialized");
}

// ─── Available Models ──────────────────────────────────────────────────────

std::vector<std::string> OllamaProvider::available_models() const {
    return {
        "llama3.1",
        "llama3.2",
        "mistral",
        "codellama",
        "phi3",
        "gemma2"
    };
}

// ─── HTTP POST via curl/popen (no API key) ─────────────────────────────────

Result<std::string> OllamaProvider::http_post(const std::string& url,
                                               const std::string& body,
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
        << " --data @-"
        << " \"" << url << "\""
        << " 2>&1";

    spdlog::debug("Ollama HTTP POST to {}", url);

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return std::unexpected(Error::internal("Failed to open curl process for Ollama API"));
    }

    std::string response;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        response += buffer.data();
    }

    int exit_code = pclose(pipe);
    if (exit_code != 0 && response.empty()) {
        return std::unexpected(Error::internal(
            "curl process failed with exit code " + std::to_string(exit_code)
            + ". Is the Ollama server running?"));
    }

    if (response.empty()) {
        return std::unexpected(Error::internal(
            "Empty response from Ollama API. Ensure Ollama is running on localhost:11434."));
    }

    return response;
}

// ─── Helper: HTTP GET via curl ─────────────────────────────────────────────

static Result<std::string> http_get(const std::string& url, int32_t timeout_sec) {
    std::ostringstream cmd;
    cmd << "curl -s --max-time " << timeout_sec
        << " \"" << url << "\""
        << " 2>&1";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return std::unexpected(Error::internal("Failed to open curl process for Ollama GET"));
    }

    std::string response;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        response += buffer.data();
    }

    int exit_code = pclose(pipe);
    if (exit_code != 0 && response.empty()) {
        return std::unexpected(Error::internal(
            "curl GET failed with exit code " + std::to_string(exit_code)));
    }

    if (response.empty()) {
        return std::unexpected(Error::internal("Empty response from Ollama API GET"));
    }

    return response;
}

// ─── Build Request Body ────────────────────────────────────────────────────

std::string OllamaProvider::build_request_body(const std::vector<ChatMessage>& messages,
                                                const std::vector<ToolDefinition>& tools,
                                                const LLMConfig& config) {
    json body;

    // Model
    std::string model = config.model.empty() ? DEFAULT_MODEL : config.model;
    body["model"] = model;

    // Stream flag
    body["stream"] = config.stream;

    // Options (temperature, top_p, etc.)
    json options;
    options["temperature"] = config.temperature;
    if (config.top_p < 1.0f) {
        options["top_p"] = config.top_p;
    }
    if (config.max_tokens > 0) {
        options["num_predict"] = config.max_tokens;
    }
    body["options"] = options;

    // Build messages array — Ollama supports system, user, assistant, tool roles
    json msg_array = json::array();

    // Prepend system prompt from config if no system message exists
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
        m["content"] = msg.content;

        if (msg.role == "assistant" && !msg.tool_calls_json.empty()) {
            // Assistant with tool calls — Ollama uses OpenAI-compatible format
            auto tc_arr = json::parse(msg.tool_calls_json, nullptr, false);
            if (!tc_arr.is_discarded() && tc_arr.is_array()) {
                json ollama_tool_calls = json::array();
                for (const auto& tc : tc_arr) {
                    json tool_call;
                    tool_call["id"] = tc.value("id", generate_uuid());
                    tool_call["type"] = "function";

                    json func;
                    func["name"] = tc.value("name", "");
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
                    ollama_tool_calls.push_back(tool_call);
                }
                m["tool_calls"] = ollama_tool_calls;
            }
        }

        msg_array.push_back(m);
    }

    body["messages"] = msg_array;

    // Tools — Ollama uses OpenAI-compatible tool format
    if (!tools.empty()) {
        json tools_array = json::array();
        for (const auto& tool : tools) {
            auto tool_json = json::parse(tool.to_ollama_json(), nullptr, false);
            if (!tool_json.is_discarded()) {
                tools_array.push_back(tool_json);
            }
        }
        body["tools"] = tools_array;
    }

    return body.dump();
}

// ─── Parse Response ────────────────────────────────────────────────────────

Result<LLMResponse> OllamaProvider::parse_response(const std::string& raw_json) {
    auto parsed = json::parse(raw_json, nullptr, false);
    if (parsed.is_discarded()) {
        return std::unexpected(Error::internal(
            "Failed to parse Ollama API response as JSON"));
    }

    // Check for error field
    if (parsed.contains("error")) {
        std::string err_msg = parsed.value("error", "Unknown Ollama error");
        spdlog::error("Ollama API error: {}", err_msg);

        if (err_msg.find("not found") != std::string::npos) {
            return std::unexpected(Error::not_found("Ollama model not found: " + err_msg));
        }
        return std::unexpected(Error::internal("Ollama API error: " + err_msg));
    }

    LLMResponse response;
    response.provider = "ollama";
    response.raw_response = raw_json;
    response.model = parsed.value("model", "");

    // Parse message content
    if (parsed.contains("message")) {
        const auto& message = parsed["message"];

        if (message.contains("content") && !message["content"].is_null()) {
            response.content = message["content"].get<std::string>();
        }

        // Parse tool_calls (OpenAI-compatible format)
        if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
            for (const auto& tc : message["tool_calls"]) {
                ToolCall tool_call;
                tool_call.id = tc.value("id", generate_uuid());

                if (tc.contains("function")) {
                    tool_call.name = tc["function"].value("name", "");
                    if (tc["function"].contains("arguments")) {
                        if (tc["function"]["arguments"].is_string()) {
                            tool_call.arguments_json = tc["function"]["arguments"].get<std::string>();
                        } else {
                            tool_call.arguments_json = tc["function"]["arguments"].dump();
                        }
                    } else {
                        tool_call.arguments_json = "{}";
                    }
                }

                response.tool_calls.push_back(tool_call);
            }
        }
    }

    // Set finish reason
    if (parsed.value("done", false)) {
        if (!response.tool_calls.empty()) {
            response.finish_reason = "tool_use";
        } else {
            response.finish_reason = "stop";
        }
    } else {
        response.finish_reason = "length";
    }

    // Parse timing information
    // Ollama reports durations in nanoseconds
    if (parsed.contains("total_duration")) {
        double ns = parsed["total_duration"].get<double>();
        response.latency_ms = ns / 1e6;
    }

    // Ollama may report token counts
    if (parsed.contains("prompt_eval_count")) {
        response.input_tokens = parsed.value("prompt_eval_count", 0);
    }
    if (parsed.contains("eval_count")) {
        response.output_tokens = parsed.value("eval_count", 0);
    }

    spdlog::debug("Ollama response parsed: content_len={}, tool_calls={}, tokens=({},{})",
                  response.content.size(), response.tool_calls.size(),
                  response.input_tokens, response.output_tokens);

    return response;
}

// ─── Chat (non-streaming) ──────────────────────────────────────────────────

Result<LLMResponse> OllamaProvider::chat(const std::vector<ChatMessage>& messages,
                                          const std::vector<ToolDefinition>& tools,
                                          const LLMConfig& config) {
    auto start = std::chrono::steady_clock::now();

    // Build non-streaming config
    LLMConfig request_config = config;
    request_config.stream = false;

    // Resolve base URL
    std::string base_url = config.base_url.empty() ? DEFAULT_BASE_URL : config.base_url;
    std::string url = base_url + "/api/chat";

    // Build request
    std::string body = build_request_body(messages, tools, request_config);
    spdlog::debug("Ollama request body size: {} bytes", body.size());

    // Send request
    auto http_result = http_post(url, body, config.timeout_seconds);
    if (!http_result.has_value()) {
        return std::unexpected(http_result.error());
    }

    // Parse response
    auto response_result = parse_response(http_result.value());
    if (!response_result.has_value()) {
        return std::unexpected(response_result.error());
    }

    auto& response = response_result.value();

    // Override latency with our own measurement (more accurate end-to-end)
    auto end = std::chrono::steady_clock::now();
    response.latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (response.model.empty()) {
        response.model = request_config.model.empty() ? DEFAULT_MODEL : request_config.model;
    }

    spdlog::info("Ollama chat completed: model={}, latency={:.1f}ms, tokens=({},{})",
                 response.model, response.latency_ms,
                 response.input_tokens, response.output_tokens);

    return response;
}

// ─── Chat Stream ───────────────────────────────────────────────────────────

Result<LLMResponse> OllamaProvider::chat_stream(const std::vector<ChatMessage>& messages,
                                                  StreamCallback callback,
                                                  const std::vector<ToolDefinition>& tools,
                                                  const LLMConfig& config) {
    auto start = std::chrono::steady_clock::now();

    // Build streaming config
    LLMConfig stream_config = config;
    stream_config.stream = true;

    std::string base_url = config.base_url.empty() ? DEFAULT_BASE_URL : config.base_url;
    std::string url = base_url + "/api/chat";

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
        << " --data @-"
        << " \"" << url << "\""
        << " 2>&1";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return std::unexpected(Error::internal("Failed to open curl process for Ollama streaming"));
    }

    LLMResponse response;
    response.provider = "ollama";

    std::string accumulated_content;
    std::array<char, 8192> line_buf{};

    // Ollama streaming: each line is a complete JSON object, not SSE format.
    // When stream=true, Ollama sends one JSON object per line, each with
    // {"message":{"role":"assistant","content":"..."},"done":false}
    // The final object has "done":true and includes timing/token counts.

    // Accumulators for tool calls
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

        auto data = json::parse(line, nullptr, false);
        if (data.is_discarded()) {
            continue;
        }

        // Check for errors
        if (data.contains("error")) {
            std::string err_msg = data.value("error", "Unknown error");
            pclose(pipe);
            return std::unexpected(Error::internal("Ollama streaming error: " + err_msg));
        }

        // Extract model
        if (response.model.empty() && data.contains("model")) {
            response.model = data.value("model", "");
        }

        // Extract content from message
        if (data.contains("message")) {
            const auto& message = data["message"];

            if (message.contains("content") && !message["content"].is_null()) {
                std::string text = message["content"].get<std::string>();
                if (!text.empty()) {
                    accumulated_content += text;
                    if (callback) {
                        callback(text, false);
                    }
                }
            }

            // Tool calls in streaming (usually only in the final chunk)
            if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
                for (const auto& tc : message["tool_calls"]) {
                    PartialToolCall ptc;
                    ptc.id = tc.value("id", generate_uuid());
                    if (tc.contains("function")) {
                        ptc.name = tc["function"].value("name", "");
                        if (tc["function"].contains("arguments")) {
                            if (tc["function"]["arguments"].is_string()) {
                                ptc.arguments_accum = tc["function"]["arguments"].get<std::string>();
                            } else {
                                ptc.arguments_accum = tc["function"]["arguments"].dump();
                            }
                        }
                    }
                    partial_tools.push_back(std::move(ptc));
                }
            }
        }

        // Check if done
        bool is_done = data.value("done", false);
        if (is_done) {
            // Extract final timing and token counts
            if (data.contains("prompt_eval_count")) {
                response.input_tokens = data.value("prompt_eval_count", 0);
            }
            if (data.contains("eval_count")) {
                response.output_tokens = data.value("eval_count", 0);
            }
            if (data.contains("total_duration")) {
                // Keep for reference, though we compute our own latency
            }

            if (callback) {
                callback("", true);
            }
            break;
        }
    }

    pclose(pipe);

    response.content = accumulated_content;

    // Finalize tool calls
    for (const auto& ptc : partial_tools) {
        ToolCall tc;
        tc.id = ptc.id.empty() ? generate_uuid() : ptc.id;
        tc.name = ptc.name;
        tc.arguments_json = ptc.arguments_accum.empty() ? "{}" : ptc.arguments_accum;
        response.tool_calls.push_back(tc);
    }

    // Set finish reason
    if (!response.tool_calls.empty()) {
        response.finish_reason = "tool_use";
    } else {
        response.finish_reason = "stop";
    }

    auto end = std::chrono::steady_clock::now();
    response.latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (response.model.empty()) {
        response.model = config.model.empty() ? DEFAULT_MODEL : config.model;
    }

    spdlog::info("Ollama streaming completed: model={}, latency={:.1f}ms, tokens=({},{})",
                 response.model, response.latency_ms,
                 response.input_tokens, response.output_tokens);

    return response;
}

// ─── List Local Models ─────────────────────────────────────────────────────

Result<std::vector<std::string>> OllamaProvider::list_local_models() {
    std::string base_url = DEFAULT_BASE_URL;
    std::string url = base_url + std::string("/api/tags");

    auto result = http_get(url, 30);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }

    auto parsed = json::parse(result.value(), nullptr, false);
    if (parsed.is_discarded()) {
        return std::unexpected(Error::internal("Failed to parse Ollama /api/tags response"));
    }

    if (parsed.contains("error")) {
        return std::unexpected(Error::internal(
            "Ollama API error: " + parsed.value("error", "unknown")));
    }

    std::vector<std::string> models;

    if (parsed.contains("models") && parsed["models"].is_array()) {
        for (const auto& model_entry : parsed["models"]) {
            std::string name = model_entry.value("name", "");
            if (!name.empty()) {
                models.push_back(name);
            }
        }
    }

    spdlog::info("Ollama local models: {} found", models.size());
    return models;
}

// ─── Pull Model ────────────────────────────────────────────────────────────

Result<void> OllamaProvider::pull_model(const std::string& model_name) {
    spdlog::info("Pulling Ollama model: {}", model_name);

    std::string base_url = DEFAULT_BASE_URL;
    std::string url = base_url + std::string("/api/pull");

    json body;
    body["name"] = model_name;
    body["stream"] = false;

    // Model pulls can be very large — use a generous timeout (30 minutes)
    auto result = http_post(url, body.dump(), 1800);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }

    auto parsed = json::parse(result.value(), nullptr, false);
    if (!parsed.is_discarded() && parsed.contains("error")) {
        return std::unexpected(Error::internal(
            "Failed to pull Ollama model '" + model_name + "': " + parsed.value("error", "unknown")));
    }

    // Check for success status
    if (!parsed.is_discarded() && parsed.contains("status")) {
        std::string status = parsed.value("status", "");
        if (status == "success" || status.find("success") != std::string::npos) {
            spdlog::info("Successfully pulled Ollama model: {}", model_name);
            return {};
        }
    }

    spdlog::info("Ollama model pull completed for: {}", model_name);
    return {};
}

}  // namespace prodxcloud::ai::providers
