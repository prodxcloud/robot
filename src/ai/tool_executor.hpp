#pragma once

/// @file tool_executor.hpp
/// @brief ReAct-style Tool-Use Reasoning Loop.
///
/// Implements the Think → Act → Observe cycle:
///   1. LLM reasons about what to do (Think)
///   2. LLM calls a tool via tool_use (Act)
///   3. Tool result is fed back to LLM (Observe)
///   4. Repeat until LLM produces final answer or max iterations reached
///
/// Supports parallel tool calls, tool result validation, and safety guards.

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/llm_provider.hpp"
#include "ai/memory.hpp"
#include "common/types.hpp"

namespace prodxcloud::ai {

// ─── Tool Implementation ────────────────────────────────────────────────────

/// Callback signature for tool implementations
/// Takes JSON arguments string, returns JSON result string
using ToolImplementation = std::function<Result<std::string>(const std::string& args_json)>;

// ─── Tool Registry Entry ────────────────────────────────────────────────────

struct RegisteredTool {
    ToolDefinition definition;
    ToolImplementation implementation;
    bool requires_confirmation = false;  // dangerous operations need user OK
    int32_t call_count = 0;
    double avg_latency_ms = 0.0;
};

// ─── Reasoning Step ─────────────────────────────────────────────────────────

struct ReasoningStep {
    int32_t iteration = 0;
    std::string thought;            // LLM's reasoning text
    std::vector<ToolCall> tool_calls;
    std::vector<std::string> tool_results;
    double latency_ms = 0.0;
    std::string timestamp;
};

// ─── Execution Config ───────────────────────────────────────────────────────

struct ToolExecutionConfig {
    int32_t max_iterations = 10;        // max think→act→observe cycles
    int32_t max_tool_calls_per_step = 5; // max parallel tool calls per iteration
    int32_t tool_timeout_ms = 60000;     // per-tool execution timeout
    bool allow_parallel_calls = true;
    bool log_reasoning = true;
    bool dry_run = false;               // if true, don't actually execute tools
    std::vector<std::string> blocked_tools;  // tools that cannot be called
};

// ─── Execution Result ───────────────────────────────────────────────────────

struct ToolExecutionResult {
    bool success = false;
    std::string final_answer;       // LLM's final response to the user
    std::vector<ReasoningStep> steps;
    int32_t total_iterations = 0;
    int32_t total_tool_calls = 0;
    int32_t total_input_tokens = 0;
    int32_t total_output_tokens = 0;
    double total_latency_ms = 0.0;
    std::string error_message;
    std::string finish_reason;      // completed, max_iterations, error, cancelled
};

// ─── Streaming Event ────────────────────────────────────────────────────────

enum class StreamEventType {
    THINKING, TOOL_CALL, TOOL_RESULT, TEXT_DELTA, DONE, ERROR
};

struct StreamEvent {
    StreamEventType type;
    std::string data;
    std::string tool_name;
    int32_t iteration = 0;
};

using StreamEventCallback = std::function<void(const StreamEvent& event)>;

// ─── Tool Executor ──────────────────────────────────────────────────────────

class ToolExecutor {
public:
    ToolExecutor();
    ~ToolExecutor() = default;

    // ─── Tool Registration ──────────────────────────────────────────────────

    /// Register a tool with its implementation
    void register_tool(const ToolDefinition& definition,
                       ToolImplementation implementation,
                       bool requires_confirmation = false);

    /// Unregister a tool
    void unregister_tool(const std::string& tool_name);

    /// Get all registered tool definitions (for LLM)
    std::vector<ToolDefinition> get_tool_definitions() const;

    /// Check if a tool is registered
    bool has_tool(const std::string& tool_name) const;

    /// Get tool call statistics
    RegisteredTool get_tool_info(const std::string& tool_name) const;

    // ─── Execution ──────────────────────────────────────────────────────────

    /// Run the full ReAct reasoning loop
    Result<ToolExecutionResult> execute(const std::string& user_query,
                                         LLMProvider& llm,
                                         MemoryManager& memory,
                                         const LLMConfig& llm_config,
                                         const ToolExecutionConfig& exec_config = {});

    /// Run with streaming events
    Result<ToolExecutionResult> execute_stream(const std::string& user_query,
                                                LLMProvider& llm,
                                                MemoryManager& memory,
                                                const LLMConfig& llm_config,
                                                StreamEventCallback callback,
                                                const ToolExecutionConfig& exec_config = {});

    /// Execute a single tool call directly (bypass LLM)
    Result<std::string> execute_tool(const std::string& tool_name,
                                      const std::string& args_json);

    // ─── Safety ─────────────────────────────────────────────────────────────

    /// Set a callback for confirmation of dangerous operations
    void set_confirmation_callback(std::function<bool(const std::string& tool_name,
                                                       const std::string& args)> callback);

private:
    std::unordered_map<std::string, RegisteredTool> tools_;
    std::function<bool(const std::string&, const std::string&)> confirmation_callback_;
    CancellationToken cancellation_;

    // Internal execution
    Result<LLMResponse> run_llm_step(LLMProvider& llm, MemoryManager& memory,
                                      const LLMConfig& config);
    Result<std::vector<std::string>> execute_tool_calls(const std::vector<ToolCall>& calls,
                                                         const ToolExecutionConfig& config);
    bool is_tool_blocked(const std::string& tool_name,
                          const ToolExecutionConfig& config) const;
    void update_tool_stats(const std::string& tool_name, double latency_ms);
};

}  // namespace prodxcloud::ai
