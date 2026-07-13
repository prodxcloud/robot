#include "analysis/drift_detector.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <spdlog/spdlog.h>

namespace prodxcloud::analysis {

Result<DriftReport> DriftDetector::detect(const std::vector<double>& baseline,
                                           const std::vector<double>& current,
                                           const DriftConfig& config) const {
    if (baseline.empty() || current.empty())
        return std::unexpected(Error::validation("Empty distribution provided"));

    DriftReport report;
    report.model_id = config.model_id;
    report.timestamp = Clock::now();

    report.psi_score = compute_psi(baseline, current, config.num_bins);
    report.severity = classify_psi(report.psi_score,
                                   config.psi_low_threshold,
                                   config.psi_medium_threshold,
                                   config.psi_high_threshold);

    // Compute KL and JS on histogrammed distributions
    double all_min = std::min(*std::min_element(baseline.begin(), baseline.end()),
                              *std::min_element(current.begin(), current.end()));
    double all_max = std::max(*std::max_element(baseline.begin(), baseline.end()),
                              *std::max_element(current.begin(), current.end()));
    auto base_hist = histogram(baseline, config.num_bins, all_min, all_max);
    auto curr_hist = histogram(current, config.num_bins, all_min, all_max);
    report.kl_divergence = compute_kl_divergence(base_hist, curr_hist);
    report.js_divergence = compute_js_divergence(base_hist, curr_hist);

    report.summary = format_report(report);
    spdlog::info("Drift detection for {}: PSI={:.4f} severity={}",
                 config.model_id, report.psi_score, static_cast<int>(report.severity));
    return report;
}

std::vector<double> DriftDetector::histogram(const std::vector<double>& data, int bins,
                                              double min_val, double max_val) {
    std::vector<double> hist(bins, 0.0);
    double range = max_val - min_val;
    if (range <= 0) { hist[0] = 1.0; return hist; }

    for (double v : data) {
        int idx = static_cast<int>((v - min_val) / range * bins);
        idx = std::clamp(idx, 0, bins - 1);
        hist[idx] += 1.0;
    }
    // Normalize to probability distribution with epsilon smoothing
    double total = data.size();
    constexpr double eps = 1e-10;
    for (auto& h : hist) h = (h / total) + eps;
    double sum = std::accumulate(hist.begin(), hist.end(), 0.0);
    for (auto& h : hist) h /= sum;
    return hist;
}

double DriftDetector::compute_psi(const std::vector<double>& baseline,
                                   const std::vector<double>& current, int bins) {
    double all_min = std::min(*std::min_element(baseline.begin(), baseline.end()),
                              *std::min_element(current.begin(), current.end()));
    double all_max = std::max(*std::max_element(baseline.begin(), baseline.end()),
                              *std::max_element(current.begin(), current.end()));
    auto base_h = histogram(baseline, bins, all_min, all_max);
    auto curr_h = histogram(current, bins, all_min, all_max);

    double psi = 0;
    for (int i = 0; i < bins; ++i)
        psi += (curr_h[i] - base_h[i]) * std::log(curr_h[i] / base_h[i]);
    return psi;
}

double DriftDetector::compute_kl_divergence(const std::vector<double>& p,
                                             const std::vector<double>& q) {
    double kl = 0;
    for (size_t i = 0; i < p.size(); ++i)
        if (p[i] > 0 && q[i] > 0) kl += p[i] * std::log(p[i] / q[i]);
    return kl;
}

double DriftDetector::compute_js_divergence(const std::vector<double>& p,
                                             const std::vector<double>& q) {
    std::vector<double> m(p.size());
    for (size_t i = 0; i < p.size(); ++i) m[i] = 0.5 * (p[i] + q[i]);
    return 0.5 * compute_kl_divergence(p, m) + 0.5 * compute_kl_divergence(q, m);
}

DriftSeverity DriftDetector::classify_psi(double psi, double low, double med, double high) {
    if (psi >= high) return DriftSeverity::CRITICAL;
    if (psi >= med)  return DriftSeverity::HIGH;
    if (psi >= low)  return DriftSeverity::MEDIUM;
    if (psi > 0.01)  return DriftSeverity::LOW;
    return DriftSeverity::NONE;
}

std::string DriftDetector::format_report(const DriftReport& r) {
    std::ostringstream os;
    const char* sev_names[] = {"NONE", "LOW", "MEDIUM", "HIGH", "CRITICAL"};
    os << std::fixed << std::setprecision(4)
       << "Drift[" << r.model_id << "]: PSI=" << r.psi_score
       << " KL=" << r.kl_divergence << " JS=" << r.js_divergence
       << " severity=" << sev_names[static_cast<int>(r.severity)];
    return os.str();
}

}  // namespace prodxcloud::analysis
