#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "common/types.hpp"

namespace prodxcloud::analysis {

enum class DriftSeverity { NONE, LOW, MEDIUM, HIGH, CRITICAL };

struct DriftReport {
    std::string model_id;
    DriftSeverity severity = DriftSeverity::NONE;
    double psi_score = 0;
    double kl_divergence = 0;
    double js_divergence = 0;
    std::unordered_map<std::string, double> feature_drifts;
    std::string summary;
    TimePoint timestamp;
};

struct DriftConfig {
    std::string model_id;
    int num_bins = 10;
    double psi_low_threshold = 0.1;
    double psi_medium_threshold = 0.2;
    double psi_high_threshold = 0.5;
};

class DriftDetector {
public:
    Result<DriftReport> detect(const std::vector<double>& baseline,
                               const std::vector<double>& current,
                               const DriftConfig& config) const;

    static double compute_psi(const std::vector<double>& baseline,
                              const std::vector<double>& current, int bins = 10);
    static double compute_kl_divergence(const std::vector<double>& p, const std::vector<double>& q);
    static double compute_js_divergence(const std::vector<double>& p, const std::vector<double>& q);
    static DriftSeverity classify_psi(double psi, double low, double med, double high);
    static std::string format_report(const DriftReport& r);

private:
    static std::vector<double> histogram(const std::vector<double>& data, int bins,
                                         double min_val, double max_val);
};

}  // namespace prodxcloud::analysis
