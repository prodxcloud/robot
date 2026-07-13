/// @file llm_provider.cpp
/// @brief ToolDefinition serialization and LLMProviderFactory implementation.

#include "ai/llm_provider.hpp"
#include "ai/providers/anthropic_provider.hpp"
#include "ai/providers/ollama_provider.hpp"
#include "ai/providers/openai_provider.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <stdexcept>

using json = nlohmann::json;

namespace prodxcloud::ai {

// ─── ToolDefinition ─── OpenAI JSON format ─────────────────────────────────
// {"type":"function","function":{"name":"...","description":"...","parameters":
//   {"type":"object","properties":{...},"required":[...]}}}

std::string ToolDefinition::to_openai_json() const {
    json properties = json::object();
    json required_params = json::array();

    for (const auto& param : parameters) {
        json prop;
        prop["type"] = param.type;
        prop["description"] = param.description;

        // Attach enum values if present
        auto enum_arr = json::parse(param.enum_values_json, nullptr, false);
        if (!enum_arr.is_discarded() && enum_arr.is_array() && !enum_arr.empty()) {
            prop["enum"] = enum_arr;
        }

        // Attach default value if present
        if (!param.default_value.empty()) {
            // Try parsing as JSON first; fall back to string literal
            auto def_val = json::parse(param.default_value, nullptr, false);
            if (!def_val.is_discarded()) {
                prop["default"] = def_val;
            } else {
                prop["default"] = param.default_value;
            }
        }

        properties[param.name] = prop;

        if (param.required) {
            required_params.push_back(param.name);
        }
    }

    json func;
    func["name"] = name;
    func["description"] = description;
    func["parameters"] = {
        {"type", "object"},
        {"properties", properties}
    };
    if (!required_params.empty()) {
        func["parameters"]["required"] = required_params;
    }

    json tool;
    tool["type"] = "function";
    tool["function"] = func;

    return tool.dump();
}

// ─── ToolDefinition ─── Anthropic JSON format ──────────────────────────────
// {"name":"...","description":"...","input_schema":
//   {"type":"object","properties":{...},"required":[...]}}

std::string ToolDefinition::to_anthropic_json() const {
    json properties = json::object();
    json required_params = json::array();

    for (const auto& param : parameters) {
        json prop;
        prop["type"] = param.type;
        prop["description"] = param.description;

        auto enum_arr = json::parse(param.enum_values_json, nullptr, false);
        if (!enum_arr.is_discarded() && enum_arr.is_array() && !enum_arr.empty()) {
            prop["enum"] = enum_arr;
        }

        if (!param.default_value.empty()) {
            auto def_val = json::parse(param.default_value, nullptr, false);
            if (!def_val.is_discarded()) {
                prop["default"] = def_val;
            } else {
                prop["default"] = param.default_value;
            }
        }

        properties[param.name] = prop;

        if (param.required) {
            required_params.push_back(param.name);
        }
    }

    json tool;
    tool["name"] = name;
    tool["description"] = description;
    tool["input_schema"] = {
        {"type", "object"},
        {"properties", properties}
    };
    if (!required_params.empty()) {
        tool["input_schema"]["required"] = required_params;
    }

    return tool.dump();
}

// ─── ToolDefinition ─── Ollama JSON format (OpenAI-compatible) ─────────────

std::string ToolDefinition::to_ollama_json() const {
    // Ollama uses the same format as OpenAI
    return to_openai_json();
}

// ─── LLMProviderFactory ────────────────────────────────────────────────────

std::unique_ptr<LLMProvider> LLMProviderFactory::create(const std::string& provider) {
    std::string lower_provider = provider;
    std::transform(lower_provider.begin(), lower_provider.end(), lower_provider.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower_provider == "anthropic" || lower_provider == "claude") {
        spdlog::info("Creating Anthropic LLM provider");
        return std::make_unique<providers::AnthropicProvider>();
    }
    if (lower_provider == "openai" || lower_provider == "gpt") {
        spdlog::info("Creating OpenAI LLM provider");
        return std::make_unique<providers::OpenAIProvider>();
    }
    if (lower_provider == "ollama" || lower_provider == "local") {
        spdlog::info("Creating Ollama LLM provider");
        return std::make_unique<providers::OllamaProvider>();
    }

    spdlog::error("Unknown LLM provider: {}", provider);
    return nullptr;
}

std::unique_ptr<LLMProvider> LLMProviderFactory::create(const LLMConfig& config) {
    auto provider = create(config.provider);
    if (!provider) {
        spdlog::error("Failed to create LLM provider from config: provider='{}'", config.provider);
    }
    return provider;
}

std::vector<std::string> LLMProviderFactory::available_providers() {
    return {"anthropic", "openai", "ollama"};
}

}  // namespace prodxcloud::ai
