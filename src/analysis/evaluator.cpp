#include "analysis/evaluator.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <spdlog/spdlog.h>

namespace prodxcloud::analysis {

Result<EvaluationMetrics> ModelEvaluator::evaluate(const EvaluationConfig& config) const {
    spdlog::warn("[evaluator] evaluate() called for '{}' — delegate to SLM-Models service",
                 config.model_id);
    return std::unexpected(Error::bad_request(
        "Model evaluation requires inference — use the SLM-Models service"));
}

std::vector<std::string> ModelEvaluator::tokenize_words(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        std::transform(word.begin(), word.end(), word.begin(), ::tolower);
        tokens.push_back(std::move(word));
    }
    return tokens;
}

std::unordered_map<std::string, int> ModelEvaluator::ngram_counts(
        const std::vector<std::string>& tokens, int n) {
    std::unordered_map<std::string, int> counts;
    if (static_cast<int>(tokens.size()) < n) return counts;
    for (size_t i = 0; i <= tokens.size() - n; ++i) {
        std::string ng;
        for (int j = 0; j < n; ++j) { if (j > 0) ng += ' '; ng += tokens[i + j]; }
        ++counts[ng];
    }
    return counts;
}

double ModelEvaluator::compute_bleu(const std::string& reference,
                                     const std::string& hypothesis, int max_n) {
    auto ref_tok = tokenize_words(reference);
    auto hyp_tok = tokenize_words(hypothesis);
    if (hyp_tok.empty()) return 0.0;
    double log_bleu = 0.0; int valid_n = 0;
    for (int n = 1; n <= max_n; ++n) {
        auto ref_ng = ngram_counts(ref_tok, n);
        auto hyp_ng = ngram_counts(hyp_tok, n);
        int clipped = 0, total = 0;
        for (const auto& [ng, cnt] : hyp_ng) {
            total += cnt;
            auto it = ref_ng.find(ng);
            clipped += std::min(cnt, it != ref_ng.end() ? it->second : 0);
        }
        if (total == 0) continue;
        double precision = static_cast<double>(clipped) / total;
        if (precision <= 0) return 0.0;
        log_bleu += std::log(precision); ++valid_n;
    }
    if (valid_n == 0) return 0.0;
    log_bleu /= valid_n;
    double bp = 1.0;
    if (hyp_tok.size() < ref_tok.size())
        bp = std::exp(1.0 - static_cast<double>(ref_tok.size()) / hyp_tok.size());
    return bp * std::exp(log_bleu);
}

double ModelEvaluator::compute_perplexity(const std::vector<double>& log_probs) {
    if (log_probs.empty()) return std::numeric_limits<double>::infinity();
    double avg = std::accumulate(log_probs.begin(), log_probs.end(), 0.0) / log_probs.size();
    return std::exp(-avg);
}

double ModelEvaluator::compute_f1(const std::vector<int>& predicted,
                                   const std::vector<int>& actual) {
    if (predicted.size() != actual.size() || predicted.empty()) return 0.0;
    int tp = 0, fp = 0, fn = 0;
    for (size_t i = 0; i < predicted.size(); ++i) {
        if (predicted[i] == 1 && actual[i] == 1) ++tp;
        else if (predicted[i] == 1 && actual[i] == 0) ++fp;
        else if (predicted[i] == 0 && actual[i] == 1) ++fn;
    }
    if (tp == 0) return 0.0;
    double precision = static_cast<double>(tp) / (tp + fp);
    double recall    = static_cast<double>(tp) / (tp + fn);
    return 2.0 * precision * recall / (precision + recall);
}

std::string ModelEvaluator::format_metrics(const EvaluationMetrics& m) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4)
       << "BLEU=" << m.bleu_score << " PPL=" << m.perplexity
       << " F1=" << m.f1_score << " acc=" << m.accuracy;
    return os.str();
}

}  // namespace prodxcloud::analysis
