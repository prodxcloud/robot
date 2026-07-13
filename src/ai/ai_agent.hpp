#pragma once

/// @file ai_agent.hpp
/// @brief AI-Enhanced Agent Base — wires LLM + Intent Detection + Memory + Tool-Use
///        into a unified intelligent agent that can reason, plan, and execute.
///
/// Each specialized agent (Cloud, DevOps, SRE, OpenClaw, CICD) creates an AIAgent
/// instance with its specific tools and system prompt. The AIAgent handles:
///   - Natural language understanding via intent detection
///   - Multi-step reasoning via ReAct tool-use loop
///   - Conversation memory with context management
///   - Multi-provider LLM support (Claude, GPT, Ollama/LLaMA)
///   - Streaming responses for real-time UX

#include <memory>
#include <string>
#include <vector>

#include "ai/intent_detector.hpp"
#include "ai/llm_provider.hpp"
#include "ai/memory.hpp"
#include "ai/tool_executor.hpp"
#include "common/types.hpp"

namespace prodxcloud::ai {

// ─── AI Agent Config ────────────────────────────────────────────────────────

struct AIAgentConfig {
    std::string agent_type;         // cloud, devops, sre, openclaw, cicd
    std::string agent_id;
    std::string tenant_id;
    LLMConfig llm_config;
    MemoryConfig memory_config;
    ToolExecutionConfig tool_config;
    std::string system_prompt;      // agent-specific system prompt
    double intent_confidence_threshold = 0.6;
    bool enable_intent_detection = true;
    bool enable_memory = true;
    bool enable_streaming = false;
};

// ─── Chat Result ────────────────────────────────────────────────────────────

struct AIChatResult {
    std::string response;           // final text response to user
    std::string agent_type;
    IntentResult intent;            // detected intent (if applicable)
    ToolExecutionResult execution;  // full reasoning trace
    int32_t total_tokens = 0;
    double total_latency_ms = 0.0;
    std::string session_id;
    std::string timestamp;
};

// ─── AI Agent ───────────────────────────────────────────────────────────────

class AIAgent {
public:
    explicit AIAgent(AIAgentConfig config);
    ~AIAgent() = default;

    // ─── Chat Interface ─────────────────────────────────────────────────────

    /// Process a natural language query — full pipeline:
    /// intent detection → context building → LLM reasoning → tool execution → response
    Result<AIChatResult> chat(const std::string& user_message);

    /// Chat with streaming events
    Result<AIChatResult> chat_stream(const std::string& user_message,
                                      StreamEventCallback callback);

    /// Direct tool execution (bypass LLM, for structured API calls)
    Result<std::string> execute_tool(const std::string& tool_name,
                                      const std::string& args_json);

    // ─── Tool Management ────────────────────────────────────────────────────

    /// Register a tool the agent can use
    void register_tool(const ToolDefinition& definition,
                       ToolImplementation implementation,
                       bool requires_confirmation = false);

    /// Get all available tools
    std::vector<ToolDefinition> get_tools() const;

    // ─── LLM Configuration ──────────────────────────────────────────────────

    /// Switch LLM provider/model at runtime
    void set_llm_config(const LLMConfig& config);

    /// Get current LLM config
    [[nodiscard]] const LLMConfig& llm_config() const { return config_.llm_config; }

    /// Set a different provider (anthropic, openai, ollama)
    Result<void> set_provider(const std::string& provider, const std::string& model = "");

    // ─── Memory ─────────────────────────────────────────────────────────────

    /// Get the memory manager for direct access
    MemoryManager& memory() { return memory_; }

    /// Clear conversation history
    void clear_history();

    /// Store a fact in long-term memory
    void remember(const std::string& key, const std::string& fact);

    /// Recall a fact from long-term memory
    std::string recall(const std::string& key) const;

    // ─── Configuration ──────────────────────────────────────────────────────

    [[nodiscard]] const std::string& agent_type() const { return config_.agent_type; }
    [[nodiscard]] const std::string& agent_id() const { return config_.agent_id; }
    [[nodiscard]] const std::string& system_prompt() const { return config_.system_prompt; }
    void set_system_prompt(const std::string& prompt) { config_.system_prompt = prompt; }

    // ─── Health ─────────────────────────────────────────────────────────────

    /// Test LLM connectivity
    Result<bool> health_check();

private:
    AIAgentConfig config_;
    std::unique_ptr<LLMProvider> provider_;
    MemoryManager memory_;
    IntentDetector intent_detector_;
    ToolExecutor tool_executor_;
    std::string session_id_;

    void initialize_provider();
};

// ─── System Prompts for Each Agent Type ─────────────────────────────────────

namespace prompts {

std::string devops_agent_system_prompt();
std::string sre_agent_system_prompt();
std::string openclaw_agent_system_prompt();
std::string cicd_agent_system_prompt();

}  // namespace prompts

}  // namespace prodxcloud::ai
