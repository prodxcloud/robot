#pragma once

/// @file intent_detector.hpp
/// @brief Intent detection engine — maps natural language queries to structured
///        operations using keyword scoring + LLM-powered classification.
///
/// Two-tier detection:
///   1. Fast keyword-based scoring (no LLM call, <1ms)
///   2. LLM-powered classification for ambiguous queries (uses tool-use)

#include <string>
#include <unordered_map>
#include <vector>

#include "ai/llm_provider.hpp"
#include "common/types.hpp"

namespace prodxcloud::ai {

// ─── Intent Result ──────────────────────────────────────────────────────────

struct IntentResult {
    std::string operation;          // e.g., "provision_vm", "restart_container"
    std::string agent_type;         // cloud, devops, sre, openclaw, cicd
    double confidence = 0.0;        // 0.0 - 1.0
    std::string method;             // "keyword" or "llm"
    std::string extracted_params_json = "{}";  // parameters extracted from query
    std::string original_query;
};

// ─── Intent Keywords ────────────────────────────────────────────────────────

struct IntentKeywords {
    std::string operation;
    std::string agent_type;
    std::vector<std::string> keywords;
    std::vector<std::string> phrases;   // multi-word patterns
    double base_weight = 1.0;
};

// ─── Intent Detector ────────────────────────────────────────────────────────

class IntentDetector {
public:
    IntentDetector();
    ~IntentDetector() = default;

    /// Fast keyword-based detection (no LLM call)
    Result<IntentResult> detect(const std::string& query);

    /// LLM-powered detection for ambiguous queries
    Result<IntentResult> detect_with_llm(const std::string& query,
                                          LLMProvider& llm,
                                          const LLMConfig& config = {});

    /// Auto: tries keyword first, falls back to LLM if confidence < threshold
    Result<IntentResult> detect_auto(const std::string& query,
                                      LLMProvider* llm = nullptr,
                                      const LLMConfig& config = {},
                                      double confidence_threshold = 0.6);

    /// Register custom intent keywords
    void register_intent(IntentKeywords intent);

    /// Get all registered intents for an agent type
    std::vector<IntentKeywords> get_intents(const std::string& agent_type) const;

    /// Get all registered agent types
    std::vector<std::string> get_agent_types() const;

private:
    std::vector<IntentKeywords> intents_;

    void populate_default_intents();

    // Keyword scoring
    double score_keywords(const std::string& query_lower,
                          const IntentKeywords& intent) const;
    std::string extract_params_from_query(const std::string& query,
                                           const std::string& operation) const;
    std::string normalize_query(const std::string& query) const;

    // LLM classification
    std::string build_classification_prompt(const std::string& query) const;
    Result<IntentResult> parse_llm_classification(const std::string& llm_response,
                                                   const std::string& query) const;
};

}  // namespace prodxcloud::ai
