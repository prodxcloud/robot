/// @file intent_detector.cpp
/// @brief Intent detection engine implementation — keyword scoring + LLM classification.

#include "ai/intent_detector.hpp"
#include "common/uuid.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>

namespace prodxcloud::ai {

using json = nlohmann::json;

// ─── Constructor ────────────────────────────────────────────────────────────

IntentDetector::IntentDetector() {
    populate_default_intents();
    spdlog::info("[IntentDetector] initialized with {} intents", intents_.size());
}

// ─── Default Intent Registration ────────────────────────────────────────────

void IntentDetector::populate_default_intents() {
    // ── Robot Intents ───────────────────────────────────────────────────────
    // The primary vocabulary of this repo: moving a physical device safely.

    intents_.push_back({"move_joint", "robot",
        {"move", "joint", "rotate", "axis", "angle", "articulate"},
        {"move joint", "rotate axis", "set joint angle", "move the arm"},
        1.0});

    intents_.push_back({"move_linear", "robot",
        {"move", "goto", "position", "pose", "cartesian", "coordinates", "reach"},
        {"move to", "go to position", "reach the point", "move the tool"},
        1.0});

    intents_.push_back({"pick_object", "robot",
        {"pick", "grab", "grasp", "lift", "take", "collect"},
        {"pick up", "grab the", "pick and place", "grasp the part"},
        1.0});

    intents_.push_back({"place_object", "robot",
        {"place", "put", "drop", "release", "set", "deposit"},
        {"put down", "place it", "release the", "drop the part"},
        1.0});

    intents_.push_back({"go_home", "robot",
        {"home", "park", "retract", "rest", "stow"},
        {"go home", "return home", "home position", "park the arm"},
        1.0});

    intents_.push_back({"emergency_stop", "robot",
        {"stop", "halt", "estop", "abort", "freeze", "emergency"},
        {"stop now", "emergency stop", "halt the robot", "abort the motion", "shut it down"},
        1.5});  // weighted up: a stop request must never lose a tie

    intents_.push_back({"device_state", "robot",
        {"state", "status", "where", "position", "telemetry", "reading"},
        {"where is", "robot status", "device state", "what is the arm doing"},
        1.0});

    // ── Provisioning Intents (delegated to vxnode) ──────────────────────────
    // The robot recognises a provisioning request so it can hand it to the node.
    // It has no intents for *how* to provision, because it does not know how.

    intents_.push_back({"vxnode_provision", "provisioning",
        {"provision", "vm", "instance", "compute", "machine", "node", "spin"},
        {"spin up", "provision a vm", "launch instance", "i need a machine",
         "get me a server"},
        1.0});

    intents_.push_back({"vxnode_deploy", "provisioning",
        {"deploy", "ship", "release", "rollout", "stack", "service"},
        {"deploy the", "ship the service", "roll out", "deploy to the node"},
        1.0});

    intents_.push_back({"vxnode_action", "provisioning",
        {"start", "stop", "restart", "reboot", "terminate", "instance", "vm"},
        {"restart the vm", "stop the instance", "terminate the machine"},
        1.0});

    intents_.push_back({"vxnode_status", "provisioning",
        {"status", "state", "health", "instance", "vm", "node"},
        {"instance status", "is the vm up", "node health", "check the node"},
        1.0});

    // ── DevOps Agent Intents ────────────────────────────────────────────────

    intents_.push_back({"server_health", "devops",
        {"health", "cpu", "memory", "disk", "uptime", "load", "performance", "metrics"},
        {"server health", "check health", "system metrics", "cpu usage", "memory usage", "disk usage"},
        1.0});

    intents_.push_back({"restart_server", "devops",
        {"restart", "reboot", "server"},
        {"restart server", "reboot server", "restart the server"},
        1.0});

    intents_.push_back({"list_containers", "devops",
        {"containers", "docker", "running", "ps"},
        {"list containers", "docker ps", "running containers", "show containers"},
        1.0});

    intents_.push_back({"restart_container", "devops",
        {"restart", "container"},
        {"docker restart", "restart container", "restart docker container"},
        1.0});

    intents_.push_back({"docker_compose_up", "devops",
        {"compose", "services"},
        {"compose up", "start services", "docker compose", "docker compose up"},
        1.0});

    intents_.push_back({"tail_logs", "devops",
        {"tail", "log", "lines"},
        {"tail log", "view log", "show log", "last lines", "view logs", "show logs"},
        1.0});

    intents_.push_back({"clear_logs", "devops",
        {"clear", "clean", "delete", "logs"},
        {"clear logs", "clean logs", "delete logs", "old logs", "remove logs"},
        1.0});

    intents_.push_back({"service_restart", "devops",
        {"service", "systemctl"},
        {"restart service", "systemctl restart", "service restart"},
        1.0});

    intents_.push_back({"deployment_health", "devops",
        {"endpoint", "http"},
        {"health check", "status code", "check endpoint", "http health"},
        1.0});

    intents_.push_back({"execute_remote_command", "devops",
        {"ssh", "execute", "remote"},
        {"run command", "execute command", "remote command", "ssh command", "run remote"},
        1.0});

    // ── SRE Agent Intents ───────────────────────────────────────────────────

    intents_.push_back({"create_incident", "sre",
        {"incident", "outage", "down", "page", "alert", "fire"},
        {"create incident", "report outage", "service down", "page on-call", "fire alert"},
        1.0});

    intents_.push_back({"resolve_incident", "sre",
        {"resolve", "fix", "mitigate"},
        {"close incident", "resolve incident", "fix incident", "mitigate issue"},
        1.0});

    intents_.push_back({"define_slo", "sre",
        {"slo", "sli", "target"},
        {"availability target", "error budget", "define slo", "set sli", "slo target"},
        1.0});

    intents_.push_back({"create_alert_rule", "sre",
        {"alert", "rule", "threshold", "notify", "trigger"},
        {"alert rule", "create alert", "set threshold", "alert trigger", "notify on"},
        1.0});

    intents_.push_back({"chaos_experiment", "sre",
        {"chaos", "inject", "failure", "stress"},
        {"stress test", "game day", "chaos experiment", "inject failure", "chaos test"},
        1.0});

    intents_.push_back({"execute_runbook", "sre",
        {"runbook", "playbook", "procedure", "remediation"},
        {"execute runbook", "run playbook", "follow procedure", "run remediation"},
        1.0});

    intents_.push_back({"capacity_forecast", "sre",
        {"capacity", "forecast", "predict", "growth", "scaling"},
        {"capacity forecast", "predict growth", "scaling forecast", "capacity planning"},
        1.0});

    // ── OpenClaw Agent Intents ──────────────────────────────────────────────

    intents_.push_back({"scan_licenses", "openclaw",
        {"license", "scan", "compliance"},
        {"open source", "license scan", "scan licenses", "check compliance", "license compliance"},
        1.0});

    intents_.push_back({"scan_vulnerabilities", "openclaw",
        {"vulnerability", "cve", "audit"},
        {"security scan", "scan vulnerabilities", "vulnerability scan", "cve scan", "security audit"},
        1.0});

    intents_.push_back({"generate_sbom", "openclaw",
        {"sbom", "inventory"},
        {"bill of materials", "software inventory", "generate sbom", "create sbom"},
        1.0});

    intents_.push_back({"enforce_policy", "openclaw",
        {"enforce", "policy", "gate"},
        {"check compliance", "enforce policy", "policy gate", "compliance check"},
        1.0});

    intents_.push_back({"scan_secrets", "openclaw",
        {"secret", "leak", "credential", "password", "exposed"},
        {"api key", "scan secrets", "secret leak", "credential scan", "exposed secrets"},
        1.0});

    // ── CICD Agent Intents ──────────────────────────────────────────────────

    intents_.push_back({"create_pipeline", "cicd",
        {"create", "pipeline", "ci", "cd", "workflow"},
        {"create pipeline", "new pipeline", "ci pipeline", "cd pipeline", "create workflow"},
        1.0});

    intents_.push_back({"trigger_pipeline", "cicd",
        {"trigger", "run", "start", "build", "pipeline"},
        {"trigger pipeline", "run pipeline", "start build", "trigger build", "run build"},
        1.0});

    intents_.push_back({"deploy_to_env", "cicd",
        {"deploy", "release", "ship", "environment"},
        {"push to", "deploy to", "release to", "ship to", "deploy environment"},
        1.0});

    intents_.push_back({"rollback_deploy", "cicd",
        {"rollback", "revert", "undo"},
        {"previous version", "rollback deploy", "revert deployment", "undo deploy", "roll back"},
        1.0});

    intents_.push_back({"run_tests", "cicd",
        {"test", "tests"},
        {"run tests", "unit test", "integration test", "execute tests", "test suite"},
        1.0});

    intents_.push_back({"create_release", "cicd",
        {"release", "tag", "version", "publish"},
        {"create release", "tag release", "new version", "publish release", "cut release"},
        1.0});

    spdlog::debug("[IntentDetector] registered {} default intents", intents_.size());
}

// ─── Query Normalization ────────────────────────────────────────────────────

std::string IntentDetector::normalize_query(const std::string& query) const {
    std::string result;
    result.reserve(query.size());
    for (char c : query) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '.' || c == '-' || c == '_') {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            result += ' ';
        }
    }
    // Collapse multiple spaces
    std::string collapsed;
    collapsed.reserve(result.size());
    bool prev_space = false;
    for (char c : result) {
        if (c == ' ') {
            if (!prev_space) collapsed += c;
            prev_space = true;
        } else {
            collapsed += c;
            prev_space = false;
        }
    }
    // Trim leading/trailing spaces
    size_t start = collapsed.find_first_not_of(' ');
    size_t end   = collapsed.find_last_not_of(' ');
    if (start == std::string::npos) return "";
    return collapsed.substr(start, end - start + 1);
}

// ─── Keyword Scoring ────────────────────────────────────────────────────────

double IntentDetector::score_keywords(const std::string& query_lower,
                                       const IntentKeywords& intent) const {
    if (intent.keywords.empty() && intent.phrases.empty()) return 0.0;

    double total_score    = 0.0;
    double max_possible   = 0.0;

    // Tokenize the query
    std::vector<std::string> query_tokens;
    {
        std::istringstream iss(query_lower);
        std::string tok;
        while (iss >> tok) {
            query_tokens.push_back(tok);
        }
    }

    // Score phrase matches (highest weight: 3.0)
    for (const auto& phrase : intent.phrases) {
        max_possible += 3.0;
        std::string phrase_lower;
        phrase_lower.reserve(phrase.size());
        for (char c : phrase) {
            phrase_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (query_lower.find(phrase_lower) != std::string::npos) {
            total_score += 3.0;
        }
    }

    // Score keyword matches
    for (const auto& keyword : intent.keywords) {
        max_possible += 2.0;  // max per keyword is exact match weight

        std::string kw_lower;
        kw_lower.reserve(keyword.size());
        for (char c : keyword) {
            kw_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        // Exact token match (weight 2.0)
        bool exact_match = false;
        for (const auto& tok : query_tokens) {
            if (tok == kw_lower) {
                total_score += 2.0;
                exact_match = true;
                break;
            }
        }

        // Partial / substring match (weight 1.0) — only if no exact match
        if (!exact_match && query_lower.find(kw_lower) != std::string::npos) {
            total_score += 1.0;
        }
    }

    // Apply base_weight and normalize
    if (max_possible <= 0.0) return 0.0;
    double normalized = (total_score / max_possible) * intent.base_weight;
    return std::clamp(normalized, 0.0, 1.0);
}

// ─── Parameter Extraction ───────────────────────────────────────────────────

std::string IntentDetector::extract_params_from_query(const std::string& query,
                                                       const std::string& operation) const {
    json params = json::object();
    std::string q = normalize_query(query);

    // Extract IP addresses / hosts
    {
        std::regex ip_re(R"((\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}))");
        std::smatch match;
        std::string::const_iterator search_start = q.cbegin();
        std::vector<std::string> ips;
        while (std::regex_search(search_start, q.cend(), match, ip_re)) {
            ips.push_back(match[1].str());
            search_start = match.suffix().first;
        }
        if (!ips.empty()) {
            if (ips.size() == 1) {
                params["host"] = ips[0];
            } else {
                params["hosts"] = ips;
            }
        }
    }

    // Extract hostname patterns (e.g., web-server-01, prod-db-03)
    {
        std::regex hostname_re(R"(([a-z][a-z0-9]*(?:-[a-z0-9]+){1,}(?:\.[a-z]{2,})*))");
        std::smatch match;
        std::string::const_iterator search_start = q.cbegin();
        std::vector<std::string> hostnames;
        while (std::regex_search(search_start, q.cend(), match, hostname_re)) {
            std::string h = match[1].str();
            // Filter out things that look like AWS regions or instance types
            if (h.find('.') == std::string::npos &&
                !std::regex_match(h, std::regex(R"([a-z]{2}-[a-z]+-\d+)")) &&
                !std::regex_match(h, std::regex(R"([a-z]\d+\.[a-z]+)"))) {
                hostnames.push_back(h);
            }
            search_start = match.suffix().first;
        }
        if (!hostnames.empty() && !params.contains("host")) {
            params["hostname"] = hostnames[0];
        }
    }

    // Extract container names (word after "container" keyword)
    {
        std::regex container_re(R"(container\s+([a-z][a-z0-9_.-]+))");
        std::smatch match;
        if (std::regex_search(q, match, container_re)) {
            params["container"] = match[1].str();
        }
    }

    // Extract service names (word after "service" keyword)
    {
        std::regex service_re(R"(service\s+([a-z][a-z0-9_.-]+))");
        std::smatch match;
        if (std::regex_search(q, match, service_re)) {
            params["service"] = match[1].str();
        }
    }

    // Extract AWS/GCP regions (e.g., us-east-1, eu-west-1, ap-southeast-2)
    {
        std::regex region_re(R"(([a-z]{2}-[a-z]+-\d+))");
        std::smatch match;
        if (std::regex_search(q, match, region_re)) {
            params["region"] = match[1].str();
        }
    }

    // Extract instance types (e.g., t3.medium, m5.large, c6g.xlarge)
    {
        std::regex instance_re(R"(([a-z]\d+[a-z]?\.[a-z0-9]+))");
        std::smatch match;
        if (std::regex_search(q, match, instance_re)) {
            params["instance_type"] = match[1].str();
        }
    }

    // Extract numeric count (after "count" keyword, or standalone numbers for quantities)
    {
        std::regex count_re(R"(count\s+(\d+))");
        std::smatch match;
        if (std::regex_search(q, match, count_re)) {
            params["count"] = std::stoi(match[1].str());
        } else {
            // Look for patterns like "3 instances", "5 servers", etc.
            std::regex num_unit_re(R"((\d+)\s+(?:instances?|servers?|vms?|nodes?|containers?|replicas?))");
            if (std::regex_search(q, match, num_unit_re)) {
                params["count"] = std::stoi(match[1].str());
            }
        }
    }

    // Extract environment names
    {
        std::regex env_re(R"(\b(production|staging|development|dev|prod|stage|qa|uat|preprod)\b)");
        std::smatch match;
        if (std::regex_search(q, match, env_re)) {
            params["environment"] = match[1].str();
        }
    }

    // Extract port numbers
    {
        std::regex port_re(R"(port\s+(\d{1,5}))");
        std::smatch match;
        if (std::regex_search(q, match, port_re)) {
            params["port"] = std::stoi(match[1].str());
        }
    }

    return params.dump();
}

// ─── Keyword-based Detection ────────────────────────────────────────────────

Result<IntentResult> IntentDetector::detect(const std::string& query) {
    if (query.empty()) {
        return std::unexpected(Error::bad_request("Empty query provided to intent detector"));
    }

    std::string query_lower = normalize_query(query);
    spdlog::debug("[IntentDetector] keyword detect for: '{}'", query_lower);

    double best_score  = 0.0;
    const IntentKeywords* best_intent = nullptr;

    for (const auto& intent : intents_) {
        double score = score_keywords(query_lower, intent);
        if (score > best_score) {
            best_score  = score;
            best_intent = &intent;
        }
    }

    if (!best_intent || best_score < 0.05) {
        spdlog::warn("[IntentDetector] no matching intent for query: '{}'", query_lower);
        return std::unexpected(
            Error::not_found("No matching intent found for query: " + query));
    }

    IntentResult result;
    result.operation      = best_intent->operation;
    result.agent_type     = best_intent->agent_type;
    result.confidence     = best_score;
    result.method         = "keyword";
    result.original_query = query;
    result.extracted_params_json = extract_params_from_query(query, best_intent->operation);

    spdlog::info("[IntentDetector] keyword match: op='{}' agent='{}' conf={:.3f}",
                 result.operation, result.agent_type, result.confidence);

    return result;
}

// ─── LLM Classification Prompt ──────────────────────────────────────────────

std::string IntentDetector::build_classification_prompt(const std::string& query) const {
    // Group intents by agent type
    std::unordered_map<std::string, std::vector<const IntentKeywords*>> grouped;
    for (const auto& intent : intents_) {
        grouped[intent.agent_type].push_back(&intent);
    }

    std::ostringstream prompt;
    prompt << "You are an intent classifier for an infrastructure automation system.\n"
           << "Classify the following user query into exactly one operation.\n\n"
           << "Available operations grouped by agent type:\n\n";

    // Deterministic ordering
    std::vector<std::string> agent_order = {"cloud", "devops", "sre", "openclaw", "cicd"};
    for (const auto& agent_type : agent_order) {
        auto it = grouped.find(agent_type);
        if (it == grouped.end()) continue;

        prompt << "## " << agent_type << " agent:\n";
        for (const auto* intent : it->second) {
            prompt << "- " << intent->operation << ": ";
            for (size_t i = 0; i < intent->keywords.size(); ++i) {
                if (i > 0) prompt << ", ";
                prompt << intent->keywords[i];
            }
            prompt << "\n";
        }
        prompt << "\n";
    }

    prompt << "User query: \"" << query << "\"\n\n"
           << "Respond ONLY with a JSON object in the following format:\n"
           << "{\n"
           << "  \"operation\": \"<operation_name>\",\n"
           << "  \"agent_type\": \"<agent_type>\",\n"
           << "  \"confidence\": <0.0 to 1.0>,\n"
           << "  \"params\": { <any extracted parameters> },\n"
           << "  \"reasoning\": \"<brief explanation>\"\n"
           << "}\n\n"
           << "Rules:\n"
           << "- Pick the single best matching operation\n"
           << "- Set confidence based on how well the query matches\n"
           << "- Extract any parameters mentioned in the query (hosts, regions, counts, etc.)\n"
           << "- If no operation matches well, set confidence below 0.3\n";

    return prompt.str();
}

// ─── Parse LLM Classification Response ──────────────────────────────────────

Result<IntentResult> IntentDetector::parse_llm_classification(
    const std::string& llm_response, const std::string& query) const {

    // Try to extract JSON from the response (LLM may wrap it in markdown)
    std::string json_str = llm_response;

    // Strip markdown code fences if present
    {
        std::regex json_block_re(R"(```(?:json)?\s*([\s\S]*?)\s*```)");
        std::smatch match;
        if (std::regex_search(llm_response, match, json_block_re)) {
            json_str = match[1].str();
        }
    }

    // Find first { and last } for robust extraction
    auto first_brace = json_str.find('{');
    auto last_brace  = json_str.rfind('}');
    if (first_brace == std::string::npos || last_brace == std::string::npos ||
        last_brace <= first_brace) {
        spdlog::error("[IntentDetector] LLM response contains no valid JSON: '{}'",
                      llm_response.substr(0, 200));
        return std::unexpected(
            Error::internal("Failed to parse LLM classification response — no JSON found"));
    }
    json_str = json_str.substr(first_brace, last_brace - first_brace + 1);

    json parsed;
    try {
        parsed = json::parse(json_str);
    } catch (const json::parse_error& e) {
        spdlog::error("[IntentDetector] JSON parse error: {}", e.what());
        return std::unexpected(
            Error::internal("Failed to parse LLM classification JSON: " + std::string(e.what())));
    }

    IntentResult result;
    result.original_query = query;
    result.method         = "llm";

    // Extract operation
    if (parsed.contains("operation") && parsed["operation"].is_string()) {
        result.operation = parsed["operation"].get<std::string>();
    } else {
        return std::unexpected(
            Error::internal("LLM classification missing 'operation' field"));
    }

    // Extract agent_type
    if (parsed.contains("agent_type") && parsed["agent_type"].is_string()) {
        result.agent_type = parsed["agent_type"].get<std::string>();
    } else {
        // Try to infer from operation by looking up registered intents
        for (const auto& intent : intents_) {
            if (intent.operation == result.operation) {
                result.agent_type = intent.agent_type;
                break;
            }
        }
        if (result.agent_type.empty()) {
            result.agent_type = "unknown";
        }
    }

    // Extract confidence
    if (parsed.contains("confidence") && parsed["confidence"].is_number()) {
        result.confidence = parsed["confidence"].get<double>();
        result.confidence = std::clamp(result.confidence, 0.0, 1.0);
    } else {
        result.confidence = 0.5;  // default if LLM omits
    }

    // Extract params
    if (parsed.contains("params") && parsed["params"].is_object()) {
        result.extracted_params_json = parsed["params"].dump();
    } else {
        result.extracted_params_json = "{}";
    }

    spdlog::info("[IntentDetector] LLM classification: op='{}' agent='{}' conf={:.3f}",
                 result.operation, result.agent_type, result.confidence);

    return result;
}

// ─── LLM-powered Detection ─────────────────────────────────────────────────

Result<IntentResult> IntentDetector::detect_with_llm(const std::string& query,
                                                       LLMProvider& llm,
                                                       const LLMConfig& config) {
    if (query.empty()) {
        return std::unexpected(Error::bad_request("Empty query provided to LLM intent detector"));
    }

    spdlog::info("[IntentDetector] LLM classification for: '{}'", query);

    // Build the classification prompt
    std::string prompt = build_classification_prompt(query);

    // Prepare LLM config — override for classification task
    LLMConfig llm_config = config;
    if (llm_config.system_prompt.empty()) {
        llm_config.system_prompt =
            "You are a precise intent classifier. Always respond with valid JSON only. "
            "Do not include any text outside the JSON object.";
    }
    if (llm_config.temperature > 0.3f) {
        llm_config.temperature = 0.1f;  // low temperature for deterministic classification
    }
    if (llm_config.max_tokens < 256) {
        llm_config.max_tokens = 512;
    }

    // Send to LLM
    std::vector<ChatMessage> messages;
    messages.push_back({"user", prompt, "", "", ""});

    auto llm_result = llm.chat(messages, {}, llm_config);
    if (!llm_result.has_value()) {
        spdlog::error("[IntentDetector] LLM call failed: {}", llm_result.error().message);
        return std::unexpected(
            Error::internal("LLM classification failed: " + llm_result.error().message));
    }

    const auto& response = llm_result.value();
    spdlog::debug("[IntentDetector] LLM response ({} tokens): '{}'",
                  response.output_tokens,
                  response.content.substr(0, 200));

    // Parse the structured response
    auto parse_result = parse_llm_classification(response.content, query);
    if (!parse_result.has_value()) {
        return parse_result;
    }

    // Merge LLM-extracted params with regex-extracted params as fallback
    auto& intent_result = parse_result.value();
    std::string regex_params = extract_params_from_query(query, intent_result.operation);
    if (intent_result.extracted_params_json == "{}") {
        intent_result.extracted_params_json = regex_params;
    } else {
        // Merge: regex params fill in any gaps the LLM missed
        try {
            json llm_params   = json::parse(intent_result.extracted_params_json);
            json regex_parsed  = json::parse(regex_params);
            for (auto& [key, val] : regex_parsed.items()) {
                if (!llm_params.contains(key)) {
                    llm_params[key] = val;
                }
            }
            intent_result.extracted_params_json = llm_params.dump();
        } catch (...) {
            // Keep LLM params if merge fails
        }
    }

    return parse_result;
}

// ─── Auto Detection (keyword + LLM fallback) ───────────────────────────────

Result<IntentResult> IntentDetector::detect_auto(const std::string& query,
                                                   LLMProvider* llm,
                                                   const LLMConfig& config,
                                                   double confidence_threshold) {
    if (query.empty()) {
        return std::unexpected(Error::bad_request("Empty query provided to auto intent detector"));
    }

    spdlog::debug("[IntentDetector] auto-detect for: '{}' (threshold={:.2f})",
                  query, confidence_threshold);

    // Step 1: Try keyword-based detection first (fast path)
    auto keyword_result = detect(query);

    if (keyword_result.has_value() &&
        keyword_result.value().confidence >= confidence_threshold) {
        spdlog::info("[IntentDetector] keyword detection sufficient: conf={:.3f} >= {:.2f}",
                     keyword_result.value().confidence, confidence_threshold);
        return keyword_result;
    }

    // Step 2: Fall back to LLM if available
    if (llm != nullptr) {
        double kw_conf = keyword_result.has_value() ? keyword_result.value().confidence : 0.0;
        spdlog::info("[IntentDetector] keyword conf={:.3f} < threshold={:.2f}, falling back to LLM",
                     kw_conf, confidence_threshold);

        auto llm_result = detect_with_llm(query, *llm, config);
        if (llm_result.has_value()) {
            return llm_result;
        }

        // LLM failed — return keyword result if we had one, otherwise propagate LLM error
        spdlog::warn("[IntentDetector] LLM fallback failed: {}", llm_result.error().message);
        if (keyword_result.has_value()) {
            spdlog::info("[IntentDetector] returning low-confidence keyword result as fallback");
            return keyword_result;
        }
        return llm_result;
    }

    // No LLM available — return keyword result even if low confidence
    if (keyword_result.has_value()) {
        spdlog::warn("[IntentDetector] no LLM available, returning keyword result with conf={:.3f}",
                     keyword_result.value().confidence);
        return keyword_result;
    }

    return keyword_result;  // propagate the not_found error
}

// ─── Intent Registration ────────────────────────────────────────────────────

void IntentDetector::register_intent(IntentKeywords intent) {
    spdlog::info("[IntentDetector] registering custom intent: '{}' for agent '{}'",
                 intent.operation, intent.agent_type);
    intents_.push_back(std::move(intent));
}

// ─── Query Methods ──────────────────────────────────────────────────────────

std::vector<IntentKeywords> IntentDetector::get_intents(const std::string& agent_type) const {
    std::vector<IntentKeywords> result;
    for (const auto& intent : intents_) {
        if (intent.agent_type == agent_type) {
            result.push_back(intent);
        }
    }
    return result;
}

std::vector<std::string> IntentDetector::get_agent_types() const {
    std::set<std::string> types;
    for (const auto& intent : intents_) {
        types.insert(intent.agent_type);
    }
    return {types.begin(), types.end()};
}

}  // namespace prodxcloud::ai
