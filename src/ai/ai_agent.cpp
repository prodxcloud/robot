/// @file ai_agent.cpp
/// @brief AIAgent implementation — wires LLM + Intent Detection + Memory + Tool-Use
///        into a unified intelligent agent that can reason, plan, and execute.

#include "ai/ai_agent.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "common/uuid.hpp"

using json = nlohmann::json;

namespace prodxcloud::ai {

// ============================================================================
// AIAgent — Constructor & Initialization
// ============================================================================

AIAgent::AIAgent(AIAgentConfig config)
    : config_(std::move(config)),
      memory_(config_.memory_config),
      intent_detector_(),
      tool_executor_() {

    // Generate a unique session ID for this agent instance
    session_id_ = generate_uuid();

    spdlog::info("AIAgent[{}] creating: type={}, session={}",
                 config_.agent_id, config_.agent_type, session_id_);

    // Set up memory with session/agent tracking
    memory_.set_session(session_id_, config_.agent_id);

    // Initialize the LLM provider from config
    initialize_provider();

    spdlog::info("AIAgent[{}] ready: provider={}, model={}",
                 config_.agent_id,
                 config_.llm_config.provider,
                 config_.llm_config.model);
}

void AIAgent::initialize_provider() {
    provider_ = LLMProviderFactory::create(config_.llm_config);
    if (!provider_) {
        spdlog::error("AIAgent[{}] failed to create LLM provider: provider='{}', model='{}'",
                      config_.agent_id,
                      config_.llm_config.provider,
                      config_.llm_config.model);
    } else {
        spdlog::info("AIAgent[{}] LLM provider initialized: {} (tool_use={}, streaming={})",
                     config_.agent_id,
                     provider_->provider_name(),
                     provider_->supports_tool_use(),
                     provider_->supports_streaming());
    }
}

// ============================================================================
// Chat Interface
// ============================================================================

Result<AIChatResult> AIAgent::chat(const std::string& user_message) {
    if (!provider_) {
        return std::unexpected(Error::internal("LLM provider not initialized"));
    }

    spdlog::info("AIAgent[{}] chat: message_length={}", config_.agent_id, user_message.size());

    // 1. Start timing
    auto start = Clock::now();

    // 2. Add user message to conversation memory
    memory_.add_user_message(user_message);

    // 3. Intent detection (if enabled)
    IntentResult intent{};
    if (config_.enable_intent_detection) {
        spdlog::debug("AIAgent[{}] detecting intent...", config_.agent_id);
        auto intent_result = intent_detector_.detect_auto(
            user_message,
            provider_.get(),
            config_.llm_config,
            config_.intent_confidence_threshold
        );
        if (intent_result.has_value()) {
            intent = intent_result.value();
            spdlog::info("AIAgent[{}] intent detected: op={}, confidence={:.2f}, method={}",
                         config_.agent_id,
                         intent.operation,
                         intent.confidence,
                         intent.method);
        } else {
            spdlog::warn("AIAgent[{}] intent detection failed: {}",
                         config_.agent_id,
                         intent_result.error().message);
        }
    }

    // 4. Run the ReAct tool-use reasoning loop
    //    The system prompt is injected into the LLM config so the tool executor
    //    uses it when building the context window.
    LLMConfig llm_config = config_.llm_config;
    llm_config.system_prompt = config_.system_prompt;

    auto exec_result = tool_executor_.execute(
        user_message,
        *provider_,
        memory_,
        llm_config,
        config_.tool_config
    );

    // 5. Build the AIChatResult
    auto end = Clock::now();
    double total_latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    AIChatResult result;
    result.agent_type = config_.agent_type;
    result.intent = intent;
    result.session_id = session_id_;
    result.timestamp = now_iso8601();
    result.total_latency_ms = total_latency_ms;

    if (exec_result.has_value()) {
        const auto& exec = exec_result.value();
        result.response = exec.final_answer;
        result.execution = exec;
        result.total_tokens = exec.total_input_tokens + exec.total_output_tokens;

        // Record the assistant response in memory
        memory_.add_assistant_message(exec.final_answer);

        spdlog::info("AIAgent[{}] chat complete: iterations={}, tool_calls={}, tokens={}, latency={:.1f}ms",
                     config_.agent_id,
                     exec.total_iterations,
                     exec.total_tool_calls,
                     result.total_tokens,
                     total_latency_ms);
    } else {
        result.response = "I encountered an error processing your request: " +
                          exec_result.error().message;
        result.execution.success = false;
        result.execution.error_message = exec_result.error().message;
        result.total_tokens = 0;

        memory_.add_error(exec_result.error().message);

        spdlog::error("AIAgent[{}] chat failed: {}", config_.agent_id, exec_result.error().message);
    }

    return result;
}

Result<AIChatResult> AIAgent::chat_stream(const std::string& user_message,
                                           StreamEventCallback callback) {
    if (!provider_) {
        return std::unexpected(Error::internal("LLM provider not initialized"));
    }

    spdlog::info("AIAgent[{}] chat_stream: message_length={}", config_.agent_id, user_message.size());

    // 1. Start timing
    auto start = Clock::now();

    // 2. Add user message to conversation memory
    memory_.add_user_message(user_message);

    // 3. Intent detection (if enabled)
    IntentResult intent{};
    if (config_.enable_intent_detection) {
        spdlog::debug("AIAgent[{}] detecting intent (stream mode)...", config_.agent_id);
        auto intent_result = intent_detector_.detect_auto(
            user_message,
            provider_.get(),
            config_.llm_config,
            config_.intent_confidence_threshold
        );
        if (intent_result.has_value()) {
            intent = intent_result.value();
            spdlog::info("AIAgent[{}] intent detected: op={}, confidence={:.2f}",
                         config_.agent_id,
                         intent.operation,
                         intent.confidence);
        } else {
            spdlog::warn("AIAgent[{}] intent detection failed: {}",
                         config_.agent_id,
                         intent_result.error().message);
        }
    }

    // 4. Run the ReAct tool-use reasoning loop with streaming
    LLMConfig llm_config = config_.llm_config;
    llm_config.system_prompt = config_.system_prompt;
    llm_config.stream = true;

    auto exec_result = tool_executor_.execute_stream(
        user_message,
        *provider_,
        memory_,
        llm_config,
        callback,
        config_.tool_config
    );

    // 5. Build the AIChatResult
    auto end = Clock::now();
    double total_latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    AIChatResult result;
    result.agent_type = config_.agent_type;
    result.intent = intent;
    result.session_id = session_id_;
    result.timestamp = now_iso8601();
    result.total_latency_ms = total_latency_ms;

    if (exec_result.has_value()) {
        const auto& exec = exec_result.value();
        result.response = exec.final_answer;
        result.execution = exec;
        result.total_tokens = exec.total_input_tokens + exec.total_output_tokens;

        memory_.add_assistant_message(exec.final_answer);

        spdlog::info("AIAgent[{}] chat_stream complete: iterations={}, tool_calls={}, tokens={}, latency={:.1f}ms",
                     config_.agent_id,
                     exec.total_iterations,
                     exec.total_tool_calls,
                     result.total_tokens,
                     total_latency_ms);
    } else {
        result.response = "I encountered an error processing your request: " +
                          exec_result.error().message;
        result.execution.success = false;
        result.execution.error_message = exec_result.error().message;
        result.total_tokens = 0;

        memory_.add_error(exec_result.error().message);

        // Send error event to the stream callback
        StreamEvent error_event;
        error_event.type = StreamEventType::ERROR;
        error_event.data = exec_result.error().message;
        callback(error_event);

        spdlog::error("AIAgent[{}] chat_stream failed: {}", config_.agent_id, exec_result.error().message);
    }

    return result;
}

Result<std::string> AIAgent::execute_tool(const std::string& tool_name,
                                           const std::string& args_json) {
    spdlog::info("AIAgent[{}] direct tool execution: tool={}", config_.agent_id, tool_name);
    return tool_executor_.execute_tool(tool_name, args_json);
}

// ============================================================================
// Tool Management
// ============================================================================

void AIAgent::register_tool(const ToolDefinition& definition,
                             ToolImplementation implementation,
                             bool requires_confirmation) {
    spdlog::info("AIAgent[{}] registering tool: {}", config_.agent_id, definition.name);
    tool_executor_.register_tool(definition, std::move(implementation), requires_confirmation);
}

std::vector<ToolDefinition> AIAgent::get_tools() const {
    return tool_executor_.get_tool_definitions();
}

// ============================================================================
// LLM Configuration
// ============================================================================

void AIAgent::set_llm_config(const LLMConfig& config) {
    spdlog::info("AIAgent[{}] updating LLM config: provider={}, model={}",
                 config_.agent_id, config.provider, config.model);
    config_.llm_config = config;
}

Result<void> AIAgent::set_provider(const std::string& provider, const std::string& model) {
    spdlog::info("AIAgent[{}] switching provider: {} -> {}, model={}",
                 config_.agent_id, config_.llm_config.provider, provider, model);

    config_.llm_config.provider = provider;
    if (!model.empty()) {
        config_.llm_config.model = model;
    }

    auto new_provider = LLMProviderFactory::create(config_.llm_config);
    if (!new_provider) {
        std::string err_msg = "Failed to create provider: " + provider;
        spdlog::error("AIAgent[{}] {}", config_.agent_id, err_msg);
        return std::unexpected(Error::internal(err_msg));
    }

    provider_ = std::move(new_provider);
    spdlog::info("AIAgent[{}] provider switched successfully: {} (tool_use={}, streaming={})",
                 config_.agent_id,
                 provider_->provider_name(),
                 provider_->supports_tool_use(),
                 provider_->supports_streaming());

    return {};
}

// ============================================================================
// Memory
// ============================================================================

void AIAgent::clear_history() {
    spdlog::info("AIAgent[{}] clearing conversation history", config_.agent_id);
    memory_.clear_conversation();
}

void AIAgent::remember(const std::string& key, const std::string& fact) {
    spdlog::debug("AIAgent[{}] storing knowledge: key={}", config_.agent_id, key);
    memory_.store_knowledge(key, fact);
}

std::string AIAgent::recall(const std::string& key) const {
    spdlog::debug("AIAgent[{}] recalling knowledge: key={}", config_.agent_id, key);
    return memory_.get_knowledge(key);
}

// ============================================================================
// Health Check
// ============================================================================

Result<bool> AIAgent::health_check() {
    if (!provider_) {
        return std::unexpected(Error::internal("LLM provider not initialized"));
    }

    spdlog::info("AIAgent[{}] running health check...", config_.agent_id);

    try {
        // Send a simple ping message to verify LLM connectivity
        std::vector<ChatMessage> messages;
        ChatMessage ping_msg;
        ping_msg.role = "user";
        ping_msg.content = "ping";
        messages.push_back(ping_msg);

        LLMConfig health_config = config_.llm_config;
        health_config.max_tokens = 16;
        health_config.temperature = 0.0f;

        auto result = provider_->chat(messages, {}, health_config);
        if (result.has_value()) {
            spdlog::info("AIAgent[{}] health check passed: provider={}, model={}, latency={:.1f}ms",
                         config_.agent_id,
                         result.value().provider,
                         result.value().model,
                         result.value().latency_ms);
            return true;
        } else {
            spdlog::error("AIAgent[{}] health check failed: {}",
                          config_.agent_id,
                          result.error().message);
            return std::unexpected(result.error());
        }
    } catch (const std::exception& e) {
        spdlog::error("AIAgent[{}] health check exception: {}", config_.agent_id, e.what());
        return std::unexpected(Error::internal(std::string("Health check exception: ") + e.what()));
    }
}

// ============================================================================
// System Prompts for Each Agent Type
// ============================================================================

namespace prompts {

std::string devops_agent_system_prompt() {
    return R"(You are an expert DevOps Agent for ProdxCloud. You manage servers, containers, services, and deployments.

Your capabilities:
- Server management: health checks, resource monitoring, process management
- SSH remote execution across multiple hosts in parallel
- Docker: container lifecycle, compose, image management, stats, logs
- Log management: tail, search, rotate, clear
- Systemd service management: start, stop, restart, status
- Package management and system updates
- Network diagnostics: port checks, DNS, ping, connections
- Deployment: health checks, rolling restarts, rollbacks

Guidelines:
- Check server health before performing operations
- Use dry_run for destructive operations (clear_logs, prune)
- For multi-host operations, start with one host to verify before scaling
- Always check deployment health after changes
- Monitor logs for errors after service restarts)";
}

std::string sre_agent_system_prompt() {
    return R"(You are an expert Site Reliability Engineering Agent for ProdxCloud. You ensure system reliability, manage incidents, and track SLOs.

Your capabilities:
- Incident management: create, escalate, resolve with severity tracking (SEV1-SEV5)
- SLO/SLA tracking: define SLOs, monitor burn rates, track error budgets
- Alerting: create rules, manage alert lifecycle
- Chaos engineering: inject failures, stress tests, validate resilience
- Runbook automation: execute and manage operational runbooks
- Post-mortem generation from incidents
- Capacity planning and forecasting
- Toil tracking and automation recommendations

Guidelines:
- For SEV1/SEV2 incidents, immediately escalate and notify on-call
- Always check error budget before approving risky changes
- Recommend chaos experiments after major deployments
- Generate post-mortems for all SEV1-SEV3 incidents
- Track toil to identify automation opportunities)";
}

std::string openclaw_agent_system_prompt() {
    return R"(You are an expert Open-Source Compliance and Security Agent for ProdxCloud. You scan, audit, and enforce software supply chain security.

Your capabilities:
- License scanning and compatibility checking
- Vulnerability assessment (CVE scanning, CVSS scoring)
- Dependency auditing (direct + transitive, risk scoring)
- SBOM generation (CycloneDX, SPDX formats)
- Supply chain security (signature verification, typosquat detection)
- Policy enforcement with gate checks
- Secret scanning in code
- SAST/DAST scanning
- Compliance reporting (SOC2, HIPAA)

Guidelines:
- Always run full_audit for new projects
- Block deployments with critical vulnerabilities (CVSS >= 9.0)
- Flag GPL/AGPL dependencies in proprietary projects
- Require SBOM for all production releases
- Check for leaked secrets before any deployment)";
}

std::string cicd_agent_system_prompt() {
    return R"(You are an expert CI/CD Pipeline Agent for ProdxCloud. You orchestrate builds, tests, deployments, and releases.

Your capabilities:
- Pipeline management: create, trigger, cancel, retry with DAG execution
- Build operations with parallel stage execution
- Multi-strategy deployments: rolling, blue-green, canary
- Artifact management: publish, promote, cache
- Environment management with locking
- Release management with semantic versioning and changelogs
- Test integration: unit, integration, E2E, performance
- Container registry operations
- Pipeline validation and linting

Guidelines:
- Always run tests before deployment
- Use canary deployments for production (start at 10% traffic)
- Lock environments during active deployments
- Generate changelogs for every release
- Validate pipeline config before execution
- Scan container images before pushing to registry)";
}

}  // namespace prompts

}  // namespace prodxcloud::ai
