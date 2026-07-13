#pragma once

/// @file memory.hpp
/// @brief AI Agent Memory System — conversation history, operation context,
///        short-term working memory, and long-term knowledge persistence.
///
/// Implements a sliding window context with importance-based retention,
/// operation result summarization, and cross-session memory.

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/llm_provider.hpp"
#include "common/types.hpp"

namespace prodxcloud::ai {

// ─── Memory Entry Types ─────────────────────────────────────────────────────

enum class MemoryType {
    CONVERSATION,       // user/assistant messages
    TOOL_RESULT,        // operation execution results
    OBSERVATION,        // agent observations about the environment
    DECISION,           // reasoning decisions the agent made
    ERROR,              // errors encountered
    SUMMARY,            // compressed summaries of older context
    KNOWLEDGE,          // long-term learned facts
    USER_PREFERENCE     // user preferences and patterns
};

constexpr std::string_view memory_type_to_string(MemoryType t) {
    switch (t) {
        case MemoryType::CONVERSATION:    return "conversation";
        case MemoryType::TOOL_RESULT:     return "tool_result";
        case MemoryType::OBSERVATION:     return "observation";
        case MemoryType::DECISION:        return "decision";
        case MemoryType::ERROR:           return "error";
        case MemoryType::SUMMARY:         return "summary";
        case MemoryType::KNOWLEDGE:       return "knowledge";
        case MemoryType::USER_PREFERENCE: return "user_preference";
    }
    return "unknown";
}

// ─── Memory Entry ───────────────────────────────────────────────────────────

struct MemoryEntry {
    std::string id;
    MemoryType type;
    std::string content;
    std::string role;               // user, assistant, system, tool
    std::string metadata_json = "{}";
    double importance = 0.5;        // 0.0-1.0, affects retention priority
    int32_t token_estimate = 0;     // approximate token count
    std::string timestamp;
    std::string session_id;
    std::string agent_id;
    bool pinned = false;            // pinned entries are never evicted
};

// ─── Memory Config ──────────────────────────────────────────────────────────

struct MemoryConfig {
    int32_t max_context_tokens = 8192;      // sliding window size
    int32_t max_short_term_entries = 50;     // recent conversation entries
    int32_t max_long_term_entries = 200;     // knowledge base entries
    int32_t summary_threshold = 20;         // summarize after N entries
    double eviction_importance_threshold = 0.3;
    bool enable_summarization = true;
    bool enable_long_term = true;
    std::string persistence_path;           // file path for persistence (empty = in-memory only)
};

// ─── Context Window ─────────────────────────────────────────────────────────

struct ContextWindow {
    std::vector<ChatMessage> messages;      // formatted for LLM
    int32_t total_tokens = 0;
    int32_t system_tokens = 0;
    int32_t conversation_tokens = 0;
    int32_t tool_tokens = 0;
    int32_t knowledge_tokens = 0;
};

// ─── Memory Manager ─────────────────────────────────────────────────────────

class MemoryManager {
public:
    explicit MemoryManager(MemoryConfig config = {});
    ~MemoryManager() = default;

    // ─── Conversation Memory ────────────────────────────────────────────────

    /// Add a user message
    void add_user_message(const std::string& content);

    /// Add an assistant response
    void add_assistant_message(const std::string& content);

    /// Add an assistant tool-use response
    void add_tool_call(const std::string& tool_name, const std::string& arguments_json,
                       const std::string& call_id);

    /// Add a tool result
    void add_tool_result(const std::string& tool_name, const std::string& result,
                         const std::string& call_id, bool success = true);

    /// Add a system observation
    void add_observation(const std::string& observation, double importance = 0.5);

    /// Add a reasoning decision
    void add_decision(const std::string& decision, double importance = 0.7);

    /// Add an error
    void add_error(const std::string& error, double importance = 0.8);

    // ─── Context Building ───────────────────────────────────────────────────

    /// Build the context window for LLM consumption
    ContextWindow build_context(const std::string& system_prompt,
                                 int32_t max_tokens = 0) const;

    /// Get recent messages as ChatMessage vector
    std::vector<ChatMessage> get_recent_messages(int32_t count = 20) const;

    /// Get the last N tool results
    std::vector<MemoryEntry> get_recent_tool_results(int32_t count = 5) const;

    // ─── Long-Term Knowledge ────────────────────────────────────────────────

    /// Store a learned fact in long-term memory
    void store_knowledge(const std::string& key, const std::string& knowledge,
                         double importance = 0.6);

    /// Retrieve knowledge by key
    std::string get_knowledge(const std::string& key) const;

    /// Search knowledge by relevance to a query (simple keyword match)
    std::vector<MemoryEntry> search_knowledge(const std::string& query,
                                               int32_t top_k = 5) const;

    /// Store a user preference
    void store_preference(const std::string& key, const std::string& value);

    /// Get a user preference
    std::string get_preference(const std::string& key) const;

    // ─── Memory Management ──────────────────────────────────────────────────

    /// Summarize old conversation entries using LLM
    Result<void> summarize(LLMProvider& llm, const LLMConfig& config = {});

    /// Clear all short-term memory (keeps long-term)
    void clear_conversation();

    /// Clear everything
    void clear_all();

    /// Get total entry count
    [[nodiscard]] size_t entry_count() const;

    /// Get estimated total tokens in memory
    [[nodiscard]] int32_t estimated_tokens() const;

    /// Set session ID for tracking
    void set_session(const std::string& session_id, const std::string& agent_id);

    // ─── Persistence ────────────────────────────────────────────────────────

    /// Save long-term memory to disk
    Result<void> save() const;

    /// Load long-term memory from disk
    Result<void> load();

private:
    MemoryConfig config_;
    std::string session_id_;
    std::string agent_id_;
    mutable std::mutex mutex_;

    std::deque<MemoryEntry> short_term_;     // conversation + tool results
    std::vector<MemoryEntry> long_term_;     // knowledge + preferences
    std::unordered_map<std::string, std::string> knowledge_index_;  // key -> content
    std::unordered_map<std::string, std::string> preferences_;

    // Internal helpers
    MemoryEntry make_entry(MemoryType type, const std::string& content,
                           const std::string& role, double importance);
    int32_t estimate_tokens(const std::string& text) const;
    void evict_if_needed();
    void apply_importance_decay();
    std::string build_summary_prompt(const std::vector<MemoryEntry>& entries) const;
};

}  // namespace prodxcloud::ai
