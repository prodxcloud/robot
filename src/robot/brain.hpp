#pragma once

/// @file brain.hpp
/// @brief The robot's onboard brain — offline knowledge, retrieval and planning.
///
/// The brain is the reason this robot can think with the network unplugged. It
/// loads a CSV knowledge corpus (robot skills + computer-usage knowledge) into an
/// inverted index, scores an operator's request against it with BM25, and turns
/// the winning entries into an executable skill plan.
///
/// There is no model server in this path, no embedding API, no token budget: a
/// request in, a ranked plan out, in microseconds, deterministically, on the
/// device. An LLM (see src/ai) can be layered *on top* to paraphrase or to handle
/// requests the corpus misses — but the robot is never dependent on one to act.

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"
#include "robot/types.hpp"

namespace prodxcloud::robot {

/// One row of the knowledge corpus.
struct KnowledgeEntry {
    std::string id;
    std::string domain;         ///< manipulation, shell, provisioning, ...
    std::string intent;         ///< canonical snake_case intent
    std::string utterance;      ///< how a human phrased it
    std::string skill;          ///< the executable primitive
    std::string params;         ///< k=v;k=v  (| separates values inside a v)
    std::string preconditions;
    std::string safety;
    std::string keywords;       ///< pipe-separated
    std::string source;

    /// Parse `params` into a map. Values keep their `|` separators.
    [[nodiscard]] std::unordered_map<std::string, std::string> param_map() const;
};

/// A retrieval hit: the entry plus why it won.
struct Recall {
    KnowledgeEntry entry;
    double         score = 0.0;
    std::vector<std::string> matched_terms;
};

/// What the brain decided to do, and its reasoning — every plan is auditable.
struct SkillPlan {
    std::string          query;
    bool                 understood = false;
    double               confidence = 0.0;   ///< 0..1, normalised top score
    KnowledgeEntry       chosen;
    std::vector<Recall>  alternatives;       ///< runners-up, for explainability
    std::vector<Command> commands;           ///< executable steps, may be empty for
                                             ///< non-motion skills (shell, vxnode, ...)
    std::string          rationale;

    /// True when the plan's skill is a provisioning skill — the controller must
    /// route these to vxnode instead of to a device.
    [[nodiscard]] bool is_provisioning() const;
};

/// A skill, and the pooled evidence for it across the corpus.
struct SkillScore {
    std::string skill;
    double      score       = 0.0;
    size_t      best_entry  = 0;  ///< index into entries(), the strongest phrasing
    int         supporting  = 0;  ///< how many entries backed this skill
};

/// Retrieval knobs. BM25's k1/b are the standard defaults; they work well here
/// because corpus documents are short and roughly uniform in length.
struct BrainConfig {
    double k1            = 1.5;
    double b             = 0.75;
    int    top_k         = 5;
    double keyword_boost = 2.0;  ///< curated keywords outweigh incidental prose
    double intent_boost  = 1.5;

    /// Absolute evidence floor. Below this the brain says "I don't know" instead of
    /// dispatching its least-bad guess.
    double min_score = 3.0;

    /// How many phrasings of a skill are pooled as evidence for it. Corroboration
    /// should count — but a skill with fifty weak matches must not out-vote one with
    /// three strong ones, so the pool is capped and averaged rather than summed.
    int pool_size = 3;

    /// Safety-domain entries are scored up.
    ///
    /// This is not a thumb on the scale; it is the correct response to an asymmetric
    /// cost. If the brain hears "stop!" and runs a diagnostic, someone gets hurt. If
    /// it hears "run a diagnostic" and stops the arm, a technician is mildly annoyed.
    /// When the evidence is close, stopping is the right way to be wrong.
    double safety_bias = 1.6;
};

class Brain {
public:
    explicit Brain(BrainConfig config = {});

    /// Load the corpus from a CSV file with the 10-column schema. Returns the
    /// number of entries indexed.
    Result<size_t> load_csv(const std::string& path);

    /// Index an in-memory corpus (used by tests and by embedded builds).
    void load_entries(std::vector<KnowledgeEntry> entries);

    [[nodiscard]] size_t size() const { return entries_.size(); }
    [[nodiscard]] const std::vector<KnowledgeEntry>& entries() const { return entries_; }

    /// Rank the corpus against @p query and return the best @p k entries.
    [[nodiscard]] std::vector<Recall> recall(const std::string& query, int k = 0) const;

    /// Rank *skills* rather than rows, pooling the evidence of every phrasing.
    ///
    /// Ranking rows alone is subtly wrong on a corpus like this one. BM25 discounts a
    /// term by how many documents contain it, so the more thoroughly the corpus covers
    /// a domain, the *weaker* every one of its rows scores on that domain's own
    /// vocabulary — "provision" appearing in a hundred provisioning rows is treated as
    /// nearly uninformative, and a rare word in an unrelated row wins. Pooling the
    /// phrasings back together is what undoes that.
    [[nodiscard]] std::vector<SkillScore> rank_skills(const std::string& query) const;

    /// Retrieve, then compile the winner into executable commands.
    /// @p device gives the plan the joint count and limits it needs to build a
    /// valid Command; pass nullptr for pure computer-use / provisioning queries.
    [[nodiscard]] SkillPlan plan(const std::string& query,
                                 const DeviceSpec*  device = nullptr) const;

    /// Domain histogram of the loaded corpus — what this brain actually knows.
    [[nodiscard]] std::unordered_map<std::string, int> domain_counts() const;

    /// Every distinct skill the brain can dispatch.
    [[nodiscard]] std::vector<std::string> known_skills() const;

private:
    struct Posting {
        size_t entry_index;
        double weight;  ///< term frequency, already boosted by field
    };

    void build_index();

    /// BM25-score every entry against @p query, filling @p scores and @p hits.
    void score_all(const std::string&                     query,
                   std::vector<double>&                   scores,
                   std::vector<std::vector<std::string>>& hits) const;

    BrainConfig                 config_;
    std::vector<KnowledgeEntry> entries_;

    /// term -> postings
    std::unordered_map<std::string, std::vector<Posting>> index_;
    /// per-entry total weighted length, for BM25 normalisation
    std::vector<double> doc_lengths_;
    double              avg_doc_length_ = 0.0;
};

/// Split a string on @p delim. Exposed for the CSV parser and its tests.
std::vector<std::string> split(const std::string& s, char delim);

/// Lowercase, strip punctuation, drop stop-words, and split into terms.
std::vector<std::string> tokenize(const std::string& text);

}  // namespace prodxcloud::robot
