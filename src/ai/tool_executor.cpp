/// @file tool_executor.cpp
/// @brief ReAct-style Tool-Use Reasoning Loop implementation.
///
/// Implements the Think -> Act -> Observe cycle with parallel tool execution,
/// streaming support, safety guards, and tool statistics tracking.

#include "ai/tool_executor.hpp"
#include "common/uuid.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <future>

using json = nlohmann::json;

namespace prodxcloud::ai {

// ─── Constructor ────────────────────────────────────────────────────────────

ToolExecutor::ToolExecutor() = default;

// ─── Tool Registration ──────────────────────────────────────────────────────

void ToolExecutor::register_tool(const ToolDefinition& definition,
                                  ToolImplementation implementation,
                                  bool requires_confirmation) {
    spdlog::info("Registering tool: {}", definition.name);

    RegisteredTool entry;
    entry.definition = definition;
    entry.implementation = std::move(implementation);
    entry.requires_confirmation = requires_confirmation;
    entry.call_count = 0;
    entry.avg_latency_ms = 0.0;

    tools_[definition.name] = std::move(entry);
}

void ToolExecutor::unregister_tool(const std::string& tool_name) {
    spdlog::info("Unregistering tool: {}", tool_name);
    tools_.erase(tool_name);
}

std::vector<ToolDefinition> ToolExecutor::get_tool_definitions() const {
    std::vector<ToolDefinition> defs;
    defs.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        defs.push_back(tool.definition);
    }
    return defs;
}

bool ToolExecutor::has_tool(const std::string& tool_name) const {
    return tools_.find(tool_name) != tools_.end();
}

RegisteredTool ToolExecutor::get_tool_info(const std::string& tool_name) const {
    auto it = tools_.find(tool_name);
    if (it != tools_.end()) {
        return it->second;
    }
    return {};
}

// ─── Safety ─────────────────────────────────────────────────────────────────

void ToolExecutor::set_confirmation_callback(
    std::function<bool(const std::string& tool_name, const std::string& args)> callback) {
    confirmation_callback_ = std::move(callback);
}

bool ToolExecutor::is_tool_blocked(const std::string& tool_name,
                                    const ToolExecutionConfig& config) const {
    return std::find(config.blocked_tools.begin(), config.blocked_tools.end(), tool_name)
           != config.blocked_tools.end();
}

// ─── Tool Statistics ────────────────────────────────────────────────────────

void ToolExecutor::update_tool_stats(const std::string& tool_name, double latency_ms) {
    auto it = tools_.find(tool_name);
    if (it == tools_.end()) return;

    auto& tool = it->second;
    // Running average: new_avg = (old_avg * count + new_value) / (count + 1)
    double total = tool.avg_latency_ms * tool.call_count + latency_ms;
    tool.call_count++;
    tool.avg_latency_ms = total / tool.call_count;
}

// ─── Internal: Run a single LLM step ───────────────────────────────────────

Result<LLMResponse> ToolExecutor::run_llm_step(LLMProvider& llm, MemoryManager& memory,
                                                 const LLMConfig& config) {
    auto tool_defs = get_tool_definitions();
    auto context = memory.build_context(config.system_prompt);

    spdlog::debug("Running LLM step: {} messages, {} tools",
                  context.messages.size(), tool_defs.size());

    auto response = llm.chat(context.messages, tool_defs, config);
    if (!response.has_value()) {
        spdlog::error("LLM step failed: {}", response.error().message);
    }
    return response;
}

// ─── Internal: Execute tool calls ───────────────────────────────────────────

Result<std::vector<std::string>> ToolExecutor::execute_tool_calls(
    const std::vector<ToolCall>& calls,
    const ToolExecutionConfig& config) {

    std::vector<std::string> results;
    results.resize(calls.size());

    if (config.allow_parallel_calls && calls.size() > 1) {
        // Parallel execution using std::async
        std::vector<std::future<Result<std::string>>> futures;
        futures.reserve(calls.size());

        for (size_t i = 0; i < calls.size(); ++i) {
            const auto& call = calls[i];

            futures.push_back(std::async(std::launch::async, [&, i]() -> Result<std::string> {
                // Check if blocked
                if (is_tool_blocked(call.name, config)) {
                    std::string err = "Tool '" + call.name + "' is blocked";
                    spdlog::warn("{}", err);
                    return std::unexpected(Error::bad_request(err));
                }

                // Find implementation
                auto it = tools_.find(call.name);
                if (it == tools_.end()) {
                    std::string err = "Tool not found: " + call.name;
                    spdlog::error("{}", err);
                    return std::unexpected(Error::not_found(err));
                }

                auto& tool = it->second;

                // Check confirmation requirement
                if (tool.requires_confirmation && confirmation_callback_) {
                    if (!confirmation_callback_(call.name, call.arguments_json)) {
                        std::string err = "Tool call denied by user: " + call.name;
                        spdlog::warn("{}", err);
                        return std::unexpected(Error::unauthorized(err));
                    }
                }

                // Dry run check
                if (config.dry_run) {
                    json dry_result;
                    dry_result["dry_run"] = true;
                    dry_result["tool"] = call.name;
                    dry_result["arguments"] = call.arguments_json;
                    return dry_result.dump();
                }

                // Execute
                auto start = Clock::now();
                auto result = tool.implementation(call.arguments_json);
                auto elapsed = std::chrono::duration_cast<Millis>(Clock::now() - start).count();

                spdlog::debug("Tool '{}' executed in {}ms", call.name, elapsed);
                return result;
            }));
        }

        // Collect results
        for (size_t i = 0; i < futures.size(); ++i) {
            auto result = futures[i].get();
            if (result.has_value()) {
                results[i] = result.value();
                // Update stats on main thread
                auto elapsed_ms = tools_[calls[i].name].avg_latency_ms;  // approximate
                update_tool_stats(calls[i].name, elapsed_ms);
            } else {
                json err_result;
                err_result["error"] = result.error().message;
                err_result["code"] = result.error().code;
                results[i] = err_result.dump();
            }
        }
    } else {
        // Sequential execution
        for (size_t i = 0; i < calls.size(); ++i) {
            const auto& call = calls[i];

            // Check if blocked
            if (is_tool_blocked(call.name, config)) {
                spdlog::warn("Tool '{}' is blocked", call.name);
                json err_result;
                err_result["error"] = "Tool '" + call.name + "' is blocked";
                results[i] = err_result.dump();
                continue;
            }

            // Find implementation
            auto it = tools_.find(call.name);
            if (it == tools_.end()) {
                spdlog::error("Tool not found: {}", call.name);
                json err_result;
                err_result["error"] = "Tool not found: " + call.name;
                results[i] = err_result.dump();
                continue;
            }

            auto& tool = it->second;

            // Check confirmation requirement
            if (tool.requires_confirmation && confirmation_callback_) {
                if (!confirmation_callback_(call.name, call.arguments_json)) {
                    spdlog::warn("Tool call denied by user: {}", call.name);
                    json err_result;
                    err_result["error"] = "Tool call denied by user";
                    results[i] = err_result.dump();
                    continue;
                }
            }

            // Dry run check
            if (config.dry_run) {
                json dry_result;
                dry_result["dry_run"] = true;
                dry_result["tool"] = call.name;
                dry_result["arguments"] = call.arguments_json;
                results[i] = dry_result.dump();
                continue;
            }

            // Execute
            auto start = Clock::now();
            auto result = tool.implementation(call.arguments_json);
            auto elapsed = static_cast<double>(
                std::chrono::duration_cast<Millis>(Clock::now() - start).count());

            update_tool_stats(call.name, elapsed);

            if (result.has_value()) {
                results[i] = result.value();
                spdlog::debug("Tool '{}' succeeded in {}ms", call.name, elapsed);
            } else {
                json err_result;
                err_result["error"] = result.error().message;
                err_result["code"] = result.error().code;
                results[i] = err_result.dump();
                spdlog::warn("Tool '{}' failed: {}", call.name, result.error().message);
            }
        }
    }

    return results;
}

// ─── Direct Tool Execution (bypass LLM) ────────────────────────────────────

Result<std::string> ToolExecutor::execute_tool(const std::string& tool_name,
                                                const std::string& args_json) {
    auto it = tools_.find(tool_name);
    if (it == tools_.end()) {
        return std::unexpected(Error::not_found("Tool not found: " + tool_name));
    }

    spdlog::info("Direct tool execution: {}", tool_name);

    auto start = Clock::now();
    auto result = it->second.implementation(args_json);
    auto elapsed = static_cast<double>(
        std::chrono::duration_cast<Millis>(Clock::now() - start).count());

    update_tool_stats(tool_name, elapsed);

    if (!result.has_value()) {
        spdlog::error("Direct tool execution failed: {} — {}", tool_name, result.error().message);
    }
    return result;
}

// ─── ReAct Loop: execute() ──────────────────────────────────────────────────

Result<ToolExecutionResult> ToolExecutor::execute(
    const std::string& user_query,
    LLMProvider& llm,
    MemoryManager& memory,
    const LLMConfig& llm_config,
    const ToolExecutionConfig& exec_config) {

    spdlog::info("Starting ReAct loop: query='{}', max_iterations={}",
                 user_query.substr(0, 80), exec_config.max_iterations);

    auto total_start = Clock::now();
    ToolExecutionResult result;

    // 1. Add user message to memory
    memory.add_user_message(user_query);

    // 2. ReAct loop
    for (int32_t iteration = 0; iteration < exec_config.max_iterations; ++iteration) {
        if (cancellation_.is_cancelled()) {
            result.success = false;
            result.finish_reason = "cancelled";
            result.error_message = "Execution cancelled";
            spdlog::warn("ReAct loop cancelled at iteration {}", iteration);
            break;
        }

        spdlog::debug("ReAct iteration {}/{}", iteration + 1, exec_config.max_iterations);

        // a. Build context and call LLM
        auto context = memory.build_context(llm_config.system_prompt);
        auto tool_defs = get_tool_definitions();
        auto llm_response = llm.chat(context.messages, tool_defs, llm_config);

        if (!llm_response.has_value()) {
            result.success = false;
            result.finish_reason = "error";
            result.error_message = "LLM call failed: " + llm_response.error().message;
            spdlog::error("LLM call failed at iteration {}: {}", iteration, llm_response.error().message);
            break;
        }

        auto& response = llm_response.value();
        result.total_input_tokens += response.input_tokens;
        result.total_output_tokens += response.output_tokens;

        // c. If LLM response has tool calls
        if (response.has_tool_calls()) {
            // Limit tool calls per step
            auto calls = response.tool_calls;
            if (static_cast<int32_t>(calls.size()) > exec_config.max_tool_calls_per_step) {
                calls.resize(exec_config.max_tool_calls_per_step);
            }

            // Record tool calls in memory
            for (const auto& tc : calls) {
                memory.add_tool_call(tc.name, tc.arguments_json, tc.id);
            }

            // Execute tool calls
            auto tool_results = execute_tool_calls(calls, exec_config);
            if (!tool_results.has_value()) {
                result.success = false;
                result.finish_reason = "error";
                result.error_message = "Tool execution failed: " + tool_results.error().message;
                break;
            }

            auto& results_vec = tool_results.value();

            // Record tool results in memory
            for (size_t i = 0; i < calls.size(); ++i) {
                bool success = true;
                // Check if result looks like an error
                auto parsed = json::parse(results_vec[i], nullptr, false);
                if (!parsed.is_discarded() && parsed.contains("error")) {
                    success = false;
                }
                memory.add_tool_result(calls[i].name, results_vec[i], calls[i].id, success);
            }

            // Create reasoning step
            ReasoningStep step;
            step.iteration = iteration;
            step.thought = response.content;
            step.tool_calls = calls;
            step.tool_results = results_vec;
            step.latency_ms = response.latency_ms;
            step.timestamp = now_iso8601();
            result.steps.push_back(std::move(step));
            result.total_tool_calls += static_cast<int32_t>(calls.size());
            result.total_iterations = iteration + 1;

            if (exec_config.log_reasoning) {
                spdlog::info("ReAct step {}: {} tool call(s) — [{}]",
                             iteration + 1, calls.size(),
                             [&]() {
                                 std::string names;
                                 for (const auto& c : calls) {
                                     if (!names.empty()) names += ", ";
                                     names += c.name;
                                 }
                                 return names;
                             }());
            }

            continue;  // next iteration
        }

        // d. If LLM response has no tool calls (finish_reason == "stop")
        //    This is the final answer
        memory.add_assistant_message(response.content);

        result.success = true;
        result.final_answer = response.content;
        result.finish_reason = "completed";
        result.total_iterations = iteration + 1;

        auto total_elapsed = static_cast<double>(
            std::chrono::duration_cast<Millis>(Clock::now() - total_start).count());
        result.total_latency_ms = total_elapsed;

        spdlog::info("ReAct loop completed in {} iterations, {:.0f}ms",
                     iteration + 1, total_elapsed);
        return result;
    }

    // 3. Max iterations reached — return the last LLM content as answer
    if (result.finish_reason.empty()) {
        result.finish_reason = "max_iterations";
        result.success = true;

        // Try to get the last response content
        if (!result.steps.empty()) {
            result.final_answer = result.steps.back().thought;
        }

        spdlog::warn("ReAct loop reached max iterations ({})", exec_config.max_iterations);
    }

    auto total_elapsed = static_cast<double>(
        std::chrono::duration_cast<Millis>(Clock::now() - total_start).count());
    result.total_latency_ms = total_elapsed;
    result.total_iterations = exec_config.max_iterations;

    return result;
}

// ─── ReAct Loop: execute_stream() ───────────────────────────────────────────

Result<ToolExecutionResult> ToolExecutor::execute_stream(
    const std::string& user_query,
    LLMProvider& llm,
    MemoryManager& memory,
    const LLMConfig& llm_config,
    StreamEventCallback callback,
    const ToolExecutionConfig& exec_config) {

    spdlog::info("Starting streaming ReAct loop: query='{}'", user_query.substr(0, 80));

    auto total_start = Clock::now();
    ToolExecutionResult result;

    // 1. Add user message to memory
    memory.add_user_message(user_query);

    // 2. ReAct loop
    for (int32_t iteration = 0; iteration < exec_config.max_iterations; ++iteration) {
        if (cancellation_.is_cancelled()) {
            result.success = false;
            result.finish_reason = "cancelled";
            result.error_message = "Execution cancelled";

            StreamEvent evt;
            evt.type = StreamEventType::ERROR;
            evt.data = "Execution cancelled";
            evt.iteration = iteration;
            callback(evt);
            break;
        }

        // a. Build context and call LLM
        auto context = memory.build_context(llm_config.system_prompt);
        auto tool_defs = get_tool_definitions();
        auto llm_response = llm.chat(context.messages, tool_defs, llm_config);

        if (!llm_response.has_value()) {
            result.success = false;
            result.finish_reason = "error";
            result.error_message = "LLM call failed: " + llm_response.error().message;

            StreamEvent evt;
            evt.type = StreamEventType::ERROR;
            evt.data = result.error_message;
            evt.iteration = iteration;
            callback(evt);
            break;
        }

        auto& response = llm_response.value();
        result.total_input_tokens += response.input_tokens;
        result.total_output_tokens += response.output_tokens;

        // Emit THINKING event
        if (!response.content.empty()) {
            StreamEvent thinking_evt;
            thinking_evt.type = StreamEventType::THINKING;
            thinking_evt.data = response.content;
            thinking_evt.iteration = iteration;
            callback(thinking_evt);
        }

        // c. If LLM response has tool calls
        if (response.has_tool_calls()) {
            auto calls = response.tool_calls;
            if (static_cast<int32_t>(calls.size()) > exec_config.max_tool_calls_per_step) {
                calls.resize(exec_config.max_tool_calls_per_step);
            }

            // Emit TOOL_CALL events
            for (const auto& tc : calls) {
                memory.add_tool_call(tc.name, tc.arguments_json, tc.id);

                StreamEvent tc_evt;
                tc_evt.type = StreamEventType::TOOL_CALL;
                tc_evt.data = tc.arguments_json;
                tc_evt.tool_name = tc.name;
                tc_evt.iteration = iteration;
                callback(tc_evt);
            }

            // Execute tool calls
            auto tool_results = execute_tool_calls(calls, exec_config);
            if (!tool_results.has_value()) {
                result.success = false;
                result.finish_reason = "error";
                result.error_message = "Tool execution failed: " + tool_results.error().message;

                StreamEvent err_evt;
                err_evt.type = StreamEventType::ERROR;
                err_evt.data = result.error_message;
                err_evt.iteration = iteration;
                callback(err_evt);
                break;
            }

            auto& results_vec = tool_results.value();

            // Emit TOOL_RESULT events and record in memory
            for (size_t i = 0; i < calls.size(); ++i) {
                bool success = true;
                auto parsed = json::parse(results_vec[i], nullptr, false);
                if (!parsed.is_discarded() && parsed.contains("error")) {
                    success = false;
                }
                memory.add_tool_result(calls[i].name, results_vec[i], calls[i].id, success);

                StreamEvent tr_evt;
                tr_evt.type = StreamEventType::TOOL_RESULT;
                tr_evt.data = results_vec[i];
                tr_evt.tool_name = calls[i].name;
                tr_evt.iteration = iteration;
                callback(tr_evt);
            }

            // Create reasoning step
            ReasoningStep step;
            step.iteration = iteration;
            step.thought = response.content;
            step.tool_calls = calls;
            step.tool_results = results_vec;
            step.latency_ms = response.latency_ms;
            step.timestamp = now_iso8601();
            result.steps.push_back(std::move(step));
            result.total_tool_calls += static_cast<int32_t>(calls.size());
            result.total_iterations = iteration + 1;

            continue;  // next iteration
        }

        // d. Final answer — no tool calls
        memory.add_assistant_message(response.content);

        // Emit TEXT_DELTA for the final answer
        StreamEvent text_evt;
        text_evt.type = StreamEventType::TEXT_DELTA;
        text_evt.data = response.content;
        text_evt.iteration = iteration;
        callback(text_evt);

        result.success = true;
        result.final_answer = response.content;
        result.finish_reason = "completed";
        result.total_iterations = iteration + 1;

        auto total_elapsed = static_cast<double>(
            std::chrono::duration_cast<Millis>(Clock::now() - total_start).count());
        result.total_latency_ms = total_elapsed;

        // Emit DONE
        StreamEvent done_evt;
        done_evt.type = StreamEventType::DONE;
        done_evt.data = result.final_answer;
        done_evt.iteration = iteration;
        callback(done_evt);

        spdlog::info("Streaming ReAct loop completed in {} iterations, {:.0f}ms",
                     iteration + 1, total_elapsed);
        return result;
    }

    // 3. Max iterations reached
    if (result.finish_reason.empty()) {
        result.finish_reason = "max_iterations";
        result.success = true;

        if (!result.steps.empty()) {
            result.final_answer = result.steps.back().thought;
        }

        // Emit final TEXT_DELTA + DONE
        if (!result.final_answer.empty()) {
            StreamEvent text_evt;
            text_evt.type = StreamEventType::TEXT_DELTA;
            text_evt.data = result.final_answer;
            text_evt.iteration = exec_config.max_iterations - 1;
            callback(text_evt);
        }

        StreamEvent done_evt;
        done_evt.type = StreamEventType::DONE;
        done_evt.data = result.final_answer;
        done_evt.iteration = exec_config.max_iterations - 1;
        callback(done_evt);

        spdlog::warn("Streaming ReAct loop reached max iterations ({})",
                     exec_config.max_iterations);
    }

    auto total_elapsed = static_cast<double>(
        std::chrono::duration_cast<Millis>(Clock::now() - total_start).count());
    result.total_latency_ms = total_elapsed;
    result.total_iterations = exec_config.max_iterations;

    return result;
}

}  // namespace prodxcloud::ai
