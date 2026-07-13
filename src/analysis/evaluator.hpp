#pragma once
/// @brief Model evaluator — model-dependent evaluation is delegated to SLM-Models.
#include <string>
#include <vector>
#include <unordered_map>
#include "common/types.hpp"

namespace prodxcloud::analysis {

struct EvaluationMetrics {
    double bleu_score = 0, perplexity = 0, f1_score = 0;
    double precision = 0, recall = 0, accuracy = 0;
    std::unordered_map<std::string, double> custom_metrics;
};

struct EvaluationConfig {
    std::string model_id;
    std::vector<std::pair<std::string, std::string>> test_pairs;
    bool compute_bleu = true, compute_perplexity = true, compute_f1 = true;
    int max_ngram = 4;
};

class ModelEvaluator {
public:
    /// Always returns an error — evaluation requires SLM-Models service.
    Result<EvaluationMetrics> evaluate(const EvaluationConfig& config) const;

    // Pure math helpers — usable locally
    static double compute_bleu(const std::string& reference, const std::string& hypothesis, int max_n = 4);
    static double compute_perplexity(const std::vector<double>& log_probs);
    static double compute_f1(const std::vector<int>& predicted, const std::vector<int>& actual);
    static std::string format_metrics(const EvaluationMetrics& m);

private:
    static std::vector<std::string> tokenize_words(const std::string& text);
    static std::unordered_map<std::string, int> ngram_counts(const std::vector<std::string>& tokens, int n);
};

}  // namespace prodxcloud::analysis
