/// @file memory.cpp
/// @brief AI Agent Memory System implementation — conversation history,
///        context building, importance-based eviction, and persistence.

#include "ai/memory.hpp"
#include "common/uuid.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <numeric>
#include <sstream>
#include <unordered_set>

using json = nlohmann::json;

namespace prodxcloud::ai {

// ─── Constructor ────────────────────────────────────────────────────────────

MemoryManager::MemoryManager(MemoryConfig config)
    : config_(std::move(config)) {
    spdlog::debug("MemoryManager created: max_context_tokens={}, max_short_term={}, max_long_term={}",
                  config_.max_context_tokens, config_.max_short_term_entries,
                  config_.max_long_term_entries);
}

// ─── Internal Helpers ───────────────────────────────────────────────────────

int32_t MemoryManager::estimate_tokens(const std::string& text) const {
    // Rough char-to-token ratio: ~4 characters per token
    return static_cast<int32_t>(text.length() / 4);
}

MemoryEntry MemoryManager::make_entry(MemoryType type, const std::string& content,
                                       const std::string& role, double importance) {
    MemoryEntry entry;
    entry.id = generate_uuid();
    entry.type = type;
    entry.content = content;
    entry.role = role;
    entry.importance = importance;
    entry.token_estimate = estimate_tokens(content);
    entry.timestamp = now_iso8601();
    entry.session_id = session_id_;
    entry.agent_id = agent_id_;
    return entry;
}

// ─── Conversation Memory ────────────────────────────────────────────────────

void MemoryManager::add_user_message(const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::debug("Adding user message ({} chars)", content.size());

    auto entry = make_entry(MemoryType::CONVERSATION, content, "user", 0.6);
    short_term_.push_back(std::move(entry));
    evict_if_needed();
}

void MemoryManager::add_assistant_message(const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::debug("Adding assistant message ({} chars)", content.size());

    auto entry = make_entry(MemoryType::CONVERSATION, content, "assistant", 0.5);
    short_term_.push_back(std::move(entry));
    evict_if_needed();
}

void MemoryManager::add_tool_call(const std::string& tool_name,
                                   const std::string& arguments_json,
                                   const std::string& call_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::debug("Adding tool call: tool={}, call_id={}", tool_name, call_id);

    json meta;
    meta["tool_name"] = tool_name;
    meta["call_id"] = call_id;
    meta["arguments"] = arguments_json;

    std::string content = "Tool call: " + tool_name + "(" + arguments_json + ")";
    auto entry = make_entry(MemoryType::TOOL_RESULT, content, "assistant", 0.6);
    entry.metadata_json = meta.dump();
    short_term_.push_back(std::move(entry));
    evict_if_needed();
}

void MemoryManager::add_tool_result(const std::string& tool_name,
                                     const std::string& result,
                                     const std::string& call_id,
                                     bool success) {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::debug("Adding tool result: tool={}, call_id={}, success={}", tool_name, call_id, success);

    json meta;
    meta["tool_name"] = tool_name;
    meta["call_id"] = call_id;
    meta["success"] = success;

    double importance = success ? 0.5 : 0.8;  // failures are more important to remember

    auto entry = make_entry(MemoryType::TOOL_RESULT, result, "tool", importance);
    entry.metadata_json = meta.dump();
    short_term_.push_back(std::move(entry));
    evict_if_needed();
}

void MemoryManager::add_observation(const std::string& observation, double importance) {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::debug("Adding observation (importance={})", importance);

    auto entry = make_entry(MemoryType::OBSERVATION, observation, "system", importance);
    short_term_.push_back(std::move(entry));
    evict_if_needed();
}

void MemoryManager::add_decision(const std::string& decision, double importance) {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::debug("Adding decision (importance={})", importance);

    auto entry = make_entry(MemoryType::DECISION, decision, "system", importance);
    short_term_.push_back(std::move(entry));
    evict_if_needed();
}

void MemoryManager::add_error(const std::string& error, double importance) {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::debug("Adding error (importance={})", importance);

    auto entry = make_entry(MemoryType::ERROR, error, "system", importance);
    short_term_.push_back(std::move(entry));
    evict_if_needed();
}

// ─── Context Building ───────────────────────────────────────────────────────

ContextWindow MemoryManager::build_context(const std::string& system_prompt,
                                            int32_t max_tokens) const {
    std::lock_guard<std::mutex> lock(mutex_);

    int32_t token_budget = (max_tokens > 0) ? max_tokens : config_.max_context_tokens;
    ContextWindow ctx;

    // 1. Add system prompt as first message
    if (!system_prompt.empty()) {
        ChatMessage sys_msg;
        sys_msg.role = "system";
        sys_msg.content = system_prompt;
        ctx.messages.push_back(std::move(sys_msg));
        ctx.system_tokens = estimate_tokens(system_prompt);
        ctx.total_tokens += ctx.system_tokens;
    }

    int32_t remaining = token_budget - ctx.total_tokens;

    // 2. Add relevant knowledge entries (pinned + search by recent query)
    // Find the most recent user message to use as search query
    std::string recent_query;
    for (auto it = short_term_.rbegin(); it != short_term_.rend(); ++it) {
        if (it->role == "user" && it->type == MemoryType::CONVERSATION) {
            recent_query = it->content;
            break;
        }
    }

    // Collect pinned long-term entries
    std::vector<const MemoryEntry*> knowledge_entries;
    for (const auto& entry : long_term_) {
        if (entry.pinned) {
            knowledge_entries.push_back(&entry);
        }
    }

    // Search knowledge by recent query if available
    if (!recent_query.empty()) {
        // Simple keyword search without holding mutex (we already hold it)
        auto search_results = [&]() {
            std::vector<const MemoryEntry*> results;
            // Tokenize query
            std::unordered_set<std::string> query_words;
            std::istringstream iss(recent_query);
            std::string word;
            while (iss >> word) {
                std::transform(word.begin(), word.end(), word.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (word.size() > 2) {
                    query_words.insert(word);
                }
            }

            for (const auto& entry : long_term_) {
                if (entry.pinned) continue;  // already added

                std::string lower_content = entry.content;
                std::transform(lower_content.begin(), lower_content.end(),
                               lower_content.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                int overlap = 0;
                for (const auto& qw : query_words) {
                    if (lower_content.find(qw) != std::string::npos) {
                        ++overlap;
                    }
                }
                if (overlap > 0) {
                    results.push_back(&entry);
                }
            }

            // Sort by number of matching keywords (descending) — approximate by importance
            std::sort(results.begin(), results.end(),
                      [](const MemoryEntry* a, const MemoryEntry* b) {
                          return a->importance > b->importance;
                      });

            if (results.size() > 5) results.resize(5);
            return results;
        }();

        for (const auto* e : search_results) {
            knowledge_entries.push_back(e);
        }
    }

    // Add knowledge entries as system messages
    for (const auto* entry : knowledge_entries) {
        int32_t tokens = entry->token_estimate;
        if (tokens > remaining) continue;

        ChatMessage msg;
        msg.role = "system";
        msg.content = "[Knowledge] " + entry->content;
        ctx.messages.push_back(std::move(msg));
        ctx.knowledge_tokens += tokens;
        ctx.total_tokens += tokens;
        remaining -= tokens;
    }

    // 3. Add conversation entries from short_term_ (newest first, respecting token limit)
    //    We collect from the back, then reverse to maintain chronological order
    std::vector<ChatMessage> conv_messages;
    for (auto it = short_term_.rbegin(); it != short_term_.rend(); ++it) {
        int32_t tokens = it->token_estimate;
        if (tokens > remaining) break;

        ChatMessage msg;
        msg.role = it->role;
        msg.content = it->content;

        if (it->type == MemoryType::TOOL_RESULT && it->role == "tool") {
            // Parse metadata for tool result messages
            auto meta = json::parse(it->metadata_json, nullptr, false);
            if (!meta.is_discarded()) {
                if (meta.contains("tool_name")) msg.name = meta["tool_name"].get<std::string>();
                if (meta.contains("call_id")) msg.tool_call_id = meta["call_id"].get<std::string>();
            }
        } else if (it->type == MemoryType::TOOL_RESULT && it->role == "assistant") {
            // Tool call from assistant — attach tool_calls_json
            auto meta = json::parse(it->metadata_json, nullptr, false);
            if (!meta.is_discarded()) {
                json tc_array = json::array();
                json tc;
                tc["id"] = meta.value("call_id", "");
                tc["type"] = "function";
                json func;
                func["name"] = meta.value("tool_name", "");
                func["arguments"] = meta.value("arguments", "");
                tc["function"] = func;
                tc_array.push_back(tc);
                msg.tool_calls_json = tc_array.dump();
            }
        }

        // Track tokens by category
        if (it->type == MemoryType::TOOL_RESULT) {
            ctx.tool_tokens += tokens;
        } else {
            ctx.conversation_tokens += tokens;
        }

        ctx.total_tokens += tokens;
        remaining -= tokens;
        conv_messages.push_back(std::move(msg));
    }

    // Reverse to chronological order and append
    std::reverse(conv_messages.begin(), conv_messages.end());
    for (auto& msg : conv_messages) {
        ctx.messages.push_back(std::move(msg));
    }

    spdlog::debug("Built context: {} messages, {} total tokens "
                  "(system={}, knowledge={}, conversation={}, tool={})",
                  ctx.messages.size(), ctx.total_tokens,
                  ctx.system_tokens, ctx.knowledge_tokens,
                  ctx.conversation_tokens, ctx.tool_tokens);

    return ctx;
}

std::vector<ChatMessage> MemoryManager::get_recent_messages(int32_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ChatMessage> messages;
    int32_t n = std::min(count, static_cast<int32_t>(short_term_.size()));

    auto start = short_term_.end() - n;
    for (auto it = start; it != short_term_.end(); ++it) {
        ChatMessage msg;
        msg.role = it->role;
        msg.content = it->content;

        if (it->type == MemoryType::TOOL_RESULT && it->role == "tool") {
            auto meta = json::parse(it->metadata_json, nullptr, false);
            if (!meta.is_discarded()) {
                if (meta.contains("tool_name")) msg.name = meta["tool_name"].get<std::string>();
                if (meta.contains("call_id")) msg.tool_call_id = meta["call_id"].get<std::string>();
            }
        }

        messages.push_back(std::move(msg));
    }

    return messages;
}

std::vector<MemoryEntry> MemoryManager::get_recent_tool_results(int32_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<MemoryEntry> results;
    for (auto it = short_term_.rbegin(); it != short_term_.rend(); ++it) {
        if (it->type == MemoryType::TOOL_RESULT) {
            results.push_back(*it);
            if (static_cast<int32_t>(results.size()) >= count) break;
        }
    }

    std::reverse(results.begin(), results.end());
    return results;
}

// ─── Long-Term Knowledge ────────────────────────────────────────────────────

void MemoryManager::store_knowledge(const std::string& key, const std::string& knowledge,
                                     double importance) {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::debug("Storing knowledge: key={}", key);

    // Update or insert in knowledge index
    knowledge_index_[key] = knowledge;

    // Check if an entry with this key already exists in long_term_
    for (auto& entry : long_term_) {
        if (entry.type == MemoryType::KNOWLEDGE) {
            auto meta = json::parse(entry.metadata_json, nullptr, false);
            if (!meta.is_discarded() && meta.value("key", "") == key) {
                entry.content = knowledge;
                entry.importance = importance;
                entry.token_estimate = estimate_tokens(knowledge);
                entry.timestamp = now_iso8601();
                return;
            }
        }
    }

    // New entry
    auto entry = make_entry(MemoryType::KNOWLEDGE, knowledge, "system", importance);
    json meta;
    meta["key"] = key;
    entry.metadata_json = meta.dump();
    long_term_.push_back(std::move(entry));

    // Enforce long-term limit
    while (static_cast<int32_t>(long_term_.size()) > config_.max_long_term_entries) {
        // Remove lowest importance non-pinned entry
        auto worst = long_term_.end();
        double worst_importance = 2.0;
        for (auto it = long_term_.begin(); it != long_term_.end(); ++it) {
            if (!it->pinned && it->importance < worst_importance) {
                worst_importance = it->importance;
                worst = it;
            }
        }
        if (worst != long_term_.end()) {
            long_term_.erase(worst);
        } else {
            break;  // all entries are pinned
        }
    }
}

std::string MemoryManager::get_knowledge(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = knowledge_index_.find(key);
    if (it != knowledge_index_.end()) {
        return it->second;
    }
    return {};
}

std::vector<MemoryEntry> MemoryManager::search_knowledge(const std::string& query,
                                                          int32_t top_k) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Tokenize query into words
    std::unordered_set<std::string> query_words;
    std::istringstream iss(query);
    std::string word;
    while (iss >> word) {
        std::transform(word.begin(), word.end(), word.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (word.size() > 2) {
            query_words.insert(word);
        }
    }

    if (query_words.empty()) return {};

    // Score each long-term entry by keyword overlap
    struct ScoredEntry {
        const MemoryEntry* entry;
        int score;
    };

    std::vector<ScoredEntry> scored;
    for (const auto& entry : long_term_) {
        std::string lower_content = entry.content;
        std::transform(lower_content.begin(), lower_content.end(),
                       lower_content.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        int overlap = 0;
        for (const auto& qw : query_words) {
            if (lower_content.find(qw) != std::string::npos) {
                ++overlap;
            }
        }

        if (overlap > 0) {
            scored.push_back({&entry, overlap});
        }
    }

    // Sort by score descending, then by importance descending
    std::sort(scored.begin(), scored.end(),
              [](const ScoredEntry& a, const ScoredEntry& b) {
                  if (a.score != b.score) return a.score > b.score;
                  return a.entry->importance > b.entry->importance;
              });

    std::vector<MemoryEntry> results;
    for (size_t i = 0; i < scored.size() && static_cast<int32_t>(results.size()) < top_k; ++i) {
        results.push_back(*scored[i].entry);
    }

    return results;
}

void MemoryManager::store_preference(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::debug("Storing preference: key={}", key);

    preferences_[key] = value;

    // Also store as long-term entry if not already present
    for (auto& entry : long_term_) {
        if (entry.type == MemoryType::USER_PREFERENCE) {
            auto meta = json::parse(entry.metadata_json, nullptr, false);
            if (!meta.is_discarded() && meta.value("key", "") == key) {
                entry.content = value;
                entry.timestamp = now_iso8601();
                return;
            }
        }
    }

    auto entry = make_entry(MemoryType::USER_PREFERENCE, value, "system", 0.7);
    json meta;
    meta["key"] = key;
    entry.metadata_json = meta.dump();
    long_term_.push_back(std::move(entry));
}

std::string MemoryManager::get_preference(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = preferences_.find(key);
    if (it != preferences_.end()) {
        return it->second;
    }
    return {};
}

// ─── Memory Management ─────────────────────────────────────────────────────

std::string MemoryManager::build_summary_prompt(const std::vector<MemoryEntry>& entries) const {
    std::ostringstream oss;
    oss << "Summarize the following conversation history into a concise summary "
        << "that preserves key facts, decisions, and outcomes. "
        << "Focus on information that would be useful for future interactions.\n\n";

    for (const auto& entry : entries) {
        oss << "[" << memory_type_to_string(entry.type) << "] "
            << entry.role << ": " << entry.content << "\n";
    }

    oss << "\nProvide a concise summary:";
    return oss.str();
}

Result<void> MemoryManager::summarize(LLMProvider& llm, const LLMConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!config_.enable_summarization) {
        return {};
    }

    if (static_cast<int32_t>(short_term_.size()) < config_.summary_threshold) {
        spdlog::debug("Not enough entries to summarize ({} < {})",
                      short_term_.size(), config_.summary_threshold);
        return {};
    }

    // Take older half of short_term_ entries for summarization
    int32_t summarize_count = static_cast<int32_t>(short_term_.size()) / 2;
    std::vector<MemoryEntry> old_entries(short_term_.begin(),
                                          short_term_.begin() + summarize_count);

    // Build summarization prompt
    std::string prompt = build_summary_prompt(old_entries);

    // Ask LLM to summarize
    std::vector<ChatMessage> messages;
    ChatMessage msg;
    msg.role = "user";
    msg.content = prompt;
    messages.push_back(std::move(msg));

    spdlog::info("Summarizing {} old memory entries via LLM", summarize_count);

    auto response = llm.chat(messages, {}, config);
    if (!response.has_value()) {
        spdlog::error("Failed to summarize memory: {}", response.error().message);
        return std::unexpected(response.error());
    }

    // Replace old entries with a single SUMMARY entry
    short_term_.erase(short_term_.begin(), short_term_.begin() + summarize_count);

    auto summary_entry = make_entry(MemoryType::SUMMARY, response->content, "system", 0.9);
    summary_entry.pinned = true;  // summaries should be retained
    short_term_.push_front(std::move(summary_entry));

    spdlog::info("Memory summarized: replaced {} entries with summary ({} tokens)",
                 summarize_count, estimate_tokens(response->content));

    return {};
}

void MemoryManager::evict_if_needed() {
    // Already called under lock

    // Check entry count limit
    while (static_cast<int32_t>(short_term_.size()) > config_.max_short_term_entries) {
        // Find lowest importance non-pinned entry
        auto worst = short_term_.end();
        double worst_importance = 2.0;
        for (auto it = short_term_.begin(); it != short_term_.end(); ++it) {
            if (!it->pinned && it->importance < worst_importance) {
                worst_importance = it->importance;
                worst = it;
            }
        }

        if (worst != short_term_.end()) {
            spdlog::debug("Evicting memory entry: id={}, type={}, importance={}",
                          worst->id, std::string(memory_type_to_string(worst->type)),
                          worst->importance);
            short_term_.erase(worst);
        } else {
            break;  // all entries are pinned, cannot evict
        }
    }

    // Check token limit
    int32_t total = 0;
    for (const auto& entry : short_term_) {
        total += entry.token_estimate;
    }

    while (total > config_.max_context_tokens && !short_term_.empty()) {
        // Find lowest importance non-pinned entry
        auto worst = short_term_.end();
        double worst_importance = 2.0;
        for (auto it = short_term_.begin(); it != short_term_.end(); ++it) {
            if (!it->pinned && it->importance < worst_importance) {
                worst_importance = it->importance;
                worst = it;
            }
        }

        if (worst != short_term_.end()) {
            total -= worst->token_estimate;
            spdlog::debug("Evicting entry for token budget: id={}, tokens={}",
                          worst->id, worst->token_estimate);
            short_term_.erase(worst);
        } else {
            break;
        }
    }
}

void MemoryManager::apply_importance_decay() {
    // Called under lock
    constexpr double decay_factor = 0.95;

    for (auto& entry : short_term_) {
        if (!entry.pinned) {
            entry.importance *= decay_factor;
        }
    }

    for (auto& entry : long_term_) {
        if (!entry.pinned) {
            entry.importance *= decay_factor;
        }
    }

    spdlog::debug("Applied importance decay (factor={})", decay_factor);
}

void MemoryManager::clear_conversation() {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::info("Clearing conversation memory ({} entries)", short_term_.size());
    short_term_.clear();
}

void MemoryManager::clear_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::info("Clearing all memory: {} short-term, {} long-term",
                 short_term_.size(), long_term_.size());
    short_term_.clear();
    long_term_.clear();
    knowledge_index_.clear();
    preferences_.clear();
}

size_t MemoryManager::entry_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return short_term_.size() + long_term_.size();
}

int32_t MemoryManager::estimated_tokens() const {
    std::lock_guard<std::mutex> lock(mutex_);

    int32_t total = 0;
    for (const auto& entry : short_term_) {
        total += entry.token_estimate;
    }
    for (const auto& entry : long_term_) {
        total += entry.token_estimate;
    }
    return total;
}

void MemoryManager::set_session(const std::string& session_id, const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_id_ = session_id;
    agent_id_ = agent_id;
    spdlog::debug("Session set: session_id={}, agent_id={}", session_id, agent_id);
}

// ─── Persistence ────────────────────────────────────────────────────────────

Result<void> MemoryManager::save() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (config_.persistence_path.empty()) {
        return std::unexpected(Error::bad_request("No persistence path configured"));
    }

    try {
        json data;

        // Serialize long-term entries
        json lt_array = json::array();
        for (const auto& entry : long_term_) {
            json e;
            e["id"] = entry.id;
            e["type"] = std::string(memory_type_to_string(entry.type));
            e["content"] = entry.content;
            e["role"] = entry.role;
            e["metadata_json"] = entry.metadata_json;
            e["importance"] = entry.importance;
            e["token_estimate"] = entry.token_estimate;
            e["timestamp"] = entry.timestamp;
            e["session_id"] = entry.session_id;
            e["agent_id"] = entry.agent_id;
            e["pinned"] = entry.pinned;
            lt_array.push_back(std::move(e));
        }
        data["long_term"] = lt_array;

        // Serialize preferences
        json prefs = json::object();
        for (const auto& [key, value] : preferences_) {
            prefs[key] = value;
        }
        data["preferences"] = prefs;

        // Write to file
        std::ofstream ofs(config_.persistence_path);
        if (!ofs.is_open()) {
            return std::unexpected(
                Error::internal("Failed to open file for writing: " + config_.persistence_path));
        }

        ofs << data.dump(2);
        ofs.close();

        spdlog::info("Memory saved to {}: {} long-term entries, {} preferences",
                     config_.persistence_path, long_term_.size(), preferences_.size());
        return {};
    } catch (const std::exception& ex) {
        return std::unexpected(Error::internal(std::string("Failed to save memory: ") + ex.what()));
    }
}

static MemoryType string_to_memory_type(const std::string& s) {
    if (s == "conversation")    return MemoryType::CONVERSATION;
    if (s == "tool_result")     return MemoryType::TOOL_RESULT;
    if (s == "observation")     return MemoryType::OBSERVATION;
    if (s == "decision")        return MemoryType::DECISION;
    if (s == "error")           return MemoryType::ERROR;
    if (s == "summary")         return MemoryType::SUMMARY;
    if (s == "knowledge")       return MemoryType::KNOWLEDGE;
    if (s == "user_preference") return MemoryType::USER_PREFERENCE;
    return MemoryType::KNOWLEDGE;  // fallback
}

Result<void> MemoryManager::load() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (config_.persistence_path.empty()) {
        return std::unexpected(Error::bad_request("No persistence path configured"));
    }

    try {
        std::ifstream ifs(config_.persistence_path);
        if (!ifs.is_open()) {
            spdlog::warn("Memory file not found: {}", config_.persistence_path);
            return {};  // Not an error — file may not exist yet
        }

        json data = json::parse(ifs);
        ifs.close();

        // Deserialize long-term entries
        long_term_.clear();
        knowledge_index_.clear();
        if (data.contains("long_term") && data["long_term"].is_array()) {
            for (const auto& e : data["long_term"]) {
                MemoryEntry entry;
                entry.id = e.value("id", "");
                entry.type = string_to_memory_type(e.value("type", "knowledge"));
                entry.content = e.value("content", "");
                entry.role = e.value("role", "system");
                entry.metadata_json = e.value("metadata_json", "{}");
                entry.importance = e.value("importance", 0.5);
                entry.token_estimate = e.value("token_estimate", 0);
                entry.timestamp = e.value("timestamp", "");
                entry.session_id = e.value("session_id", "");
                entry.agent_id = e.value("agent_id", "");
                entry.pinned = e.value("pinned", false);

                // Rebuild knowledge index
                if (entry.type == MemoryType::KNOWLEDGE) {
                    auto meta = json::parse(entry.metadata_json, nullptr, false);
                    if (!meta.is_discarded() && meta.contains("key")) {
                        knowledge_index_[meta["key"].get<std::string>()] = entry.content;
                    }
                }

                long_term_.push_back(std::move(entry));
            }
        }

        // Deserialize preferences
        preferences_.clear();
        if (data.contains("preferences") && data["preferences"].is_object()) {
            for (auto& [key, value] : data["preferences"].items()) {
                preferences_[key] = value.get<std::string>();
            }
        }

        spdlog::info("Memory loaded from {}: {} long-term entries, {} preferences",
                     config_.persistence_path, long_term_.size(), preferences_.size());
        return {};
    } catch (const std::exception& ex) {
        return std::unexpected(Error::internal(std::string("Failed to load memory: ") + ex.what()));
    }
}

}  // namespace prodxcloud::ai
