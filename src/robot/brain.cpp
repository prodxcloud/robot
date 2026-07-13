#include "robot/brain.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "common/uuid.hpp"

namespace prodxcloud::robot {

// ─── Text utilities ─────────────────────────────────────────────────────────

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string              cur;
    std::istringstream       ss(s);
    while (std::getline(ss, cur, delim)) out.push_back(cur);
    // getline drops a trailing empty field; the CSV schema is fixed-width so we
    // restore it rather than silently shifting every column left.
    if (!s.empty() && s.back() == delim) out.emplace_back();
    return out;
}

namespace {

/// Words that carry no retrieval signal. Kept small on purpose — an aggressive
/// stop-list would eat "up"/"down"/"open"/"close", which are load-bearing verbs
/// for a robot.
const std::unordered_set<std::string>& stop_words() {
    static const std::unordered_set<std::string> kStop = {
        "a", "an", "the", "is", "are", "was", "were", "be", "been", "to", "of",
        "in", "on", "at", "for", "with", "and", "or", "but", "if", "then",
        "please", "can", "could", "would", "should", "i", "you", "we", "it",
        "that", "this", "there", "here", "my", "our", "your", "me", "us",
    };
    return kStop;
}

std::string stem(std::string w) {
    // A deliberately tiny suffix stripper. Full Porter stemming would be overkill
    // for a 500-row corpus and would mangle domain terms like "axis" -> "axi".
    const auto ends_with = [&](const char* suf) {
        const size_t n = std::string(suf).size();
        return w.size() > n + 2 && w.compare(w.size() - n, n, suf) == 0;
    };
    if (ends_with("ing")) w.erase(w.size() - 3);
    else if (ends_with("ed")) w.erase(w.size() - 2);
    else if (ends_with("es")) w.erase(w.size() - 2);
    else if (ends_with("s") && !ends_with("ss")) w.erase(w.size() - 1);
    return w;
}

}  // namespace

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> terms;
    std::string              cur;

    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (std::isalnum(c)) {
            cur += static_cast<char>(std::tolower(c));
        } else if (raw == '_' || raw == '-' || raw == '.' || raw == '/') {
            // Identifiers like `move_joint`, `/api/v2/provision/vm` and `vx-arm6`
            // are indexed both whole and in pieces, so either phrasing retrieves.
            if (!cur.empty()) {
                terms.push_back(cur);
                cur.clear();
            }
        } else {
            if (!cur.empty()) {
                terms.push_back(cur);
                cur.clear();
            }
        }
    }
    if (!cur.empty()) terms.push_back(cur);

    std::vector<std::string> out;
    out.reserve(terms.size());
    for (auto& t : terms) {
        if (t.size() < 2) continue;
        if (stop_words().count(t)) continue;
        out.push_back(stem(std::move(t)));
    }
    return out;
}

// ─── KnowledgeEntry ─────────────────────────────────────────────────────────

std::unordered_map<std::string, std::string> KnowledgeEntry::param_map() const {
    std::unordered_map<std::string, std::string> m;
    if (params.empty() || params == "-") return m;

    for (const auto& kv : split(params, ';')) {
        const size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;

        std::string key = kv.substr(0, eq);
        std::string val = kv.substr(eq + 1);

        const auto trim = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        };
        trim(key);
        trim(val);

        if (!key.empty()) m[key] = val;
    }
    return m;
}

bool SkillPlan::is_provisioning() const {
    return chosen.skill.rfind("vxnode_", 0) == 0;
}

// ─── Brain ──────────────────────────────────────────────────────────────────

Brain::Brain(BrainConfig config) : config_(config) {}

Result<size_t> Brain::load_csv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::unexpected(Error::not_found("cannot open knowledge corpus: " + path));
    }

    std::vector<KnowledgeEntry> loaded;
    std::string                 line;
    size_t                      line_no = 0;

    while (std::getline(file, line)) {
        ++line_no;

        if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF corpora
        if (line.empty()) continue;
        if (line_no == 1 && line.rfind("id,", 0) == 0) continue;    // header

        const auto f = split(line, ',');
        if (f.size() < 10) {
            return std::unexpected(Error::validation(
                "corpus line " + std::to_string(line_no) + " has " + std::to_string(f.size()) +
                " fields, expected 10 — a field probably contains a comma"));
        }

        KnowledgeEntry e;
        e.id            = f[0];
        e.domain        = f[1];
        e.intent        = f[2];
        e.utterance     = f[3];
        e.skill         = f[4];
        e.params        = f[5];
        e.preconditions = f[6];
        e.safety        = f[7];
        e.keywords      = f[8];
        e.source        = f[9];
        loaded.push_back(std::move(e));
    }

    if (loaded.empty()) {
        return std::unexpected(Error::validation("knowledge corpus is empty: " + path));
    }

    const size_t n = loaded.size();
    load_entries(std::move(loaded));
    return n;
}

void Brain::load_entries(std::vector<KnowledgeEntry> entries) {
    entries_ = std::move(entries);
    build_index();
}

void Brain::build_index() {
    index_.clear();
    doc_lengths_.assign(entries_.size(), 0.0);

    double total_length = 0.0;

    for (size_t i = 0; i < entries_.size(); ++i) {
        const KnowledgeEntry& e = entries_[i];

        // Term weights are field-dependent: a curated keyword is a much stronger
        // signal of intent than the same word appearing incidentally in prose.
        std::unordered_map<std::string, double> tf;

        const auto add = [&](const std::string& text, double weight) {
            for (const auto& term : tokenize(text)) tf[term] += weight;
        };

        add(e.keywords, config_.keyword_boost);
        add(e.intent, config_.intent_boost);
        add(e.skill, config_.intent_boost);
        add(e.utterance, 1.0);
        add(e.domain, 1.0);
        add(e.params, 0.5);

        double len = 0.0;
        for (const auto& [term, w] : tf) {
            index_[term].push_back({i, w});
            len += w;
        }

        doc_lengths_[i] = len;
        total_length += len;
    }

    avg_doc_length_ = entries_.empty() ? 0.0 : total_length / static_cast<double>(entries_.size());
}

/// BM25 over every entry. Returns per-entry scores and the terms that matched.
void Brain::score_all(const std::string&                     query,
                      std::vector<double>&                   scores,
                      std::vector<std::vector<std::string>>& hits) const {
    scores.assign(entries_.size(), 0.0);
    hits.assign(entries_.size(), {});

    const auto terms = tokenize(query);
    if (terms.empty() || entries_.empty()) return;

    const auto n_docs = static_cast<double>(entries_.size());

    for (const auto& term : terms) {
        const auto it = index_.find(term);
        if (it == index_.end()) continue;

        const auto df = static_cast<double>(it->second.size());
        // BM25 IDF. The +0.5 smoothing keeps a term that appears in *every* document
        // from going negative and actively penalising a match.
        const double idf = std::log(1.0 + (n_docs - df + 0.5) / (df + 0.5));

        for (const auto& [idx, tf] : it->second) {
            const double dl   = doc_lengths_[idx];
            const double norm = config_.k1 * (1.0 - config_.b +
                                              config_.b * (avg_doc_length_ > 0.0
                                                               ? dl / avg_doc_length_
                                                               : 1.0));

            double s = idf * (tf * (config_.k1 + 1.0)) / (tf + norm);

            // Asymmetric cost: see BrainConfig::safety_bias.
            if (entries_[idx].domain == "safety") s *= config_.safety_bias;

            scores[idx] += s;
            hits[idx].push_back(term);
        }
    }
}

std::vector<Recall> Brain::recall(const std::string& query, int k) const {
    if (k <= 0) k = config_.top_k;

    std::vector<double>                   scores;
    std::vector<std::vector<std::string>> hits;
    score_all(query, scores, hits);

    std::vector<Recall> out;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (scores[i] <= 0.0) continue;
        out.push_back({entries_[i], scores[i], std::move(hits[i])});
    }

    std::sort(out.begin(), out.end(), [](const Recall& a, const Recall& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.entry.id < b.entry.id;  // stable, deterministic tie-break
    });

    if (static_cast<int>(out.size()) > k) out.resize(static_cast<size_t>(k));
    return out;
}

std::vector<SkillScore> Brain::rank_skills(const std::string& query) const {
    std::vector<double>                   scores;
    std::vector<std::vector<std::string>> hits;
    score_all(query, scores, hits);

    // Gather every scoring entry under its skill.
    std::unordered_map<std::string, std::vector<std::pair<double, size_t>>> by_skill;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (scores[i] <= 0.0) continue;
        by_skill[entries_[i].skill].emplace_back(scores[i], i);
    }

    std::vector<SkillScore> out;
    out.reserve(by_skill.size());

    for (auto& [skill, matches] : by_skill) {
        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;  // deterministic
        });

        // Average the strongest few phrasings. Corroboration counts — but averaging
        // (rather than summing) stops a skill from winning on sheer row count.
        const size_t pool = std::min(matches.size(),
                                     static_cast<size_t>(std::max(1, config_.pool_size)));

        double sum = 0.0;
        for (size_t i = 0; i < pool; ++i) sum += matches[i].first;

        SkillScore s;
        s.skill      = skill;
        s.score      = sum / static_cast<double>(pool);
        s.best_entry = matches.front().second;
        s.supporting = static_cast<int>(matches.size());
        out.push_back(std::move(s));
    }

    std::sort(out.begin(), out.end(), [](const SkillScore& a, const SkillScore& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.skill < b.skill;  // deterministic
    });
    return out;
}

namespace {

/// Parse `1.0|-1.57|0.3` into a joint vector.
JointVector parse_joint_list(const std::string& v) {
    JointVector out;
    for (const auto& piece : split(v, '|')) {
        if (piece.empty()) continue;
        try {
            out.push_back(std::stod(piece));
        } catch (const std::exception&) {
            return {};  // a malformed list yields nothing rather than a partial pose
        }
    }
    return out;
}

double param_double(const std::unordered_map<std::string, std::string>& p,
                    const std::string&                                  key,
                    double                                              fallback) {
    const auto it = p.find(key);
    if (it == p.end()) return fallback;
    try {
        return std::stod(it->second);
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

SkillPlan Brain::plan(const std::string& query, const DeviceSpec* device) const {
    SkillPlan plan;
    plan.query = query;

    const auto skills = rank_skills(query);
    if (skills.empty()) {
        plan.rationale = "no corpus entry matched any term in the request";
        return plan;
    }

    const SkillScore& winner = skills.front();

    // The winning skill's strongest phrasing becomes the entry we act on — it carries
    // the params, preconditions and safety note.
    plan.chosen = entries_[winner.best_entry];

    // Keep the top entries as alternatives, for explainability.
    plan.alternatives = recall(query, config_.top_k);
    if (!plan.alternatives.empty() && plan.alternatives.front().entry.id == plan.chosen.id) {
        plan.alternatives.erase(plan.alternatives.begin());
    }

    // Two things must hold before the robot acts on a sentence.
    //
    // First, absolute evidence: the request has to actually match the corpus, not
    // merely match it better than nothing. A brain with no floor will always dispatch
    // *something*, and on a robot that is how a garbled instruction becomes a motion.
    if (winner.score < config_.min_score) {
        plan.rationale = "best match '" + plan.chosen.intent + "' scored only " +
                         std::to_string(winner.score) + " (floor " +
                         std::to_string(config_.min_score) + ") — refusing to guess";
        return plan;
    }

    // Second, decisiveness: how far clear of the runner-up is it? Reported, not
    // gated — an ambiguous request is still worth acting on, but the caller deserves
    // to know the brain was torn.
    const double second = skills.size() > 1 ? skills[1].score : 0.0;
    plan.confidence     = winner.score > 0.0 ? (winner.score - second) / winner.score : 0.0;

    plan.understood = true;

    // Bind the vector to a named local before iterating a subobject of it.
    //
    //   for (auto& t : recall(query, 1).front().matched_terms)   // DANGLES
    //
    // would not extend the returned vector's lifetime: the range binds to a
    // subobject reached *through a function call* (front()), which lifetime
    // extension does not see through. The vector would be destroyed before the first
    // iteration and the loop would walk freed memory. C++23's P2718R0 fixes this,
    // but GCC 13 does not implement it yet — and the corruption surfaces as a
    // bad_alloc thousands of lines away, so it is worth never writing at all.
    const auto top = recall(query, 1);

    std::string matched;
    if (!top.empty()) {
        for (const auto& t : top.front().matched_terms) {
            if (!matched.empty()) matched += ", ";
            matched += t;
        }
    }

    plan.rationale = "matched " + plan.chosen.id + " (" + plan.chosen.domain + "/" +
                     plan.chosen.intent + ") on terms [" + matched + "]; skill '" +
                     plan.chosen.skill + "' pooled " + std::to_string(winner.supporting) +
                     " corroborating entries (score " + std::to_string(winner.score) +
                     " vs runner-up " + std::to_string(second) + ")";

    // Compile the skill into commands. Non-motion skills (shell, computer-use,
    // vxnode) produce no Command — the controller routes them by skill name.
    const auto params = plan.chosen.param_map();
    const std::string& skill = plan.chosen.skill;

    if (skill == "move_joint" || skill == "jog") {
        Command c;
        c.id          = generate_uuid();
        c.device_id   = device ? device->id : "";
        c.type        = CommandType::MOVE_JOINT;
        c.speed_scale = param_double(params, "speed", 0.5);
        c.accel_scale = param_double(params, "accel", 0.5);

        if (const auto it = params.find("joints"); it != params.end()) {
            c.joint_goal = parse_joint_list(it->second);
        }
        // Without an explicit goal the skill is a template, not a motion — the
        // caller must fill in the target. Emitting a command with an empty goal
        // would sail into safety and be rejected for the wrong reason.
        if (!c.joint_goal.empty()) plan.commands.push_back(std::move(c));

    } else if (skill == "move_linear") {
        Command c;
        c.id          = generate_uuid();
        c.device_id   = device ? device->id : "";
        c.type        = CommandType::MOVE_LINEAR;
        c.speed_scale = param_double(params, "speed", 0.5);
        c.accel_scale = param_double(params, "accel", 0.5);

        c.pose_goal.position = {param_double(params, "x", 0.0),
                                param_double(params, "y", 0.0),
                                param_double(params, "z", 0.0)};
        c.pose_goal.roll  = param_double(params, "roll", 0.0);
        c.pose_goal.pitch = param_double(params, "pitch", 0.0);
        c.pose_goal.yaw   = param_double(params, "yaw", 0.0);

        if (params.count("x") || params.count("y") || params.count("z")) {
            plan.commands.push_back(std::move(c));
        }

    } else if (skill == "home") {
        Command c;
        c.id        = generate_uuid();
        c.device_id = device ? device->id : "";
        c.type      = CommandType::HOME;
        plan.commands.push_back(std::move(c));

    } else if (skill == "grip" || skill == "release") {
        Command c;
        c.id            = generate_uuid();
        c.device_id     = device ? device->id : "";
        c.type          = CommandType::GRIP;
        c.grip_width_m  = param_double(params, "width", skill == "release" ? 0.085 : 0.0);
        plan.commands.push_back(std::move(c));

    } else if (skill == "dwell") {
        Command c;
        c.id        = generate_uuid();
        c.device_id = device ? device->id : "";
        c.type      = CommandType::DWELL;
        c.dwell_s   = param_double(params, "seconds", 1.0);
        plan.commands.push_back(std::move(c));

    } else if (skill == "estop") {
        Command c;
        c.id        = generate_uuid();
        c.device_id = device ? device->id : "";
        c.type      = CommandType::ESTOP;
        plan.commands.push_back(std::move(c));

    } else if (skill == "reset") {
        Command c;
        c.id        = generate_uuid();
        c.device_id = device ? device->id : "";
        c.type      = CommandType::RESET;
        plan.commands.push_back(std::move(c));
    }

    return plan;
}

std::unordered_map<std::string, int> Brain::domain_counts() const {
    std::unordered_map<std::string, int> counts;
    for (const auto& e : entries_) ++counts[e.domain];
    return counts;
}

std::vector<std::string> Brain::known_skills() const {
    std::unordered_set<std::string> set;
    for (const auto& e : entries_) set.insert(e.skill);

    std::vector<std::string> out(set.begin(), set.end());
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace prodxcloud::robot
