#pragma once

/// @file openclaw_agent.hpp
/// @brief OpenClaw Agent — open-source compliance, license scanning, vulnerability
///        assessment, dependency auditing, SBOM generation, supply-chain security,
///        and policy enforcement for software projects.
///
/// Leverages C++ performance for deep dependency graph traversal, parallel CVE
/// scanning, and real-time license compatibility analysis.

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "agents/agent_base.hpp"
#include "common/types.hpp"

namespace prodxcloud::agents::specialized {

// ─── OpenClaw Operation Types ───────────────────────────────────────────────

enum class OpenClawOperation {
    // License Scanning
    SCAN_LICENSES, CHECK_LICENSE_COMPATIBILITY, GET_LICENSE_REPORT,
    SET_LICENSE_POLICY, LIST_LICENSE_POLICIES,
    // Vulnerability Assessment
    SCAN_VULNERABILITIES, GET_CVE_DETAILS, LIST_VULNERABILITIES,
    PRIORITIZE_VULNERABILITIES, CHECK_EXPLOIT_AVAILABILITY,
    VULNERABILITY_TREND,
    // Dependency Auditing
    AUDIT_DEPENDENCIES, LIST_DEPENDENCIES, DEPENDENCY_TREE,
    CHECK_OUTDATED, CHECK_DEPRECATED, DEPENDENCY_RISK_SCORE,
    TRANSITIVE_DEPENDENCY_SCAN,
    // SBOM (Software Bill of Materials)
    GENERATE_SBOM, VALIDATE_SBOM, COMPARE_SBOM, EXPORT_SBOM,
    // Supply Chain Security
    VERIFY_SIGNATURES, CHECK_PROVENANCE, ATTESTATION_VERIFY,
    SUPPLY_CHAIN_SCORE, DETECT_TYPOSQUAT,
    // Policy Enforcement
    ENFORCE_POLICY, CREATE_POLICY, UPDATE_POLICY, LIST_POLICIES,
    POLICY_COMPLIANCE_REPORT, GATE_CHECK,
    // Code Scanning
    SCAN_SECRETS, SCAN_CODE_QUALITY, SAST_SCAN, DAST_SCAN,
    IaC_SCAN, CONTAINER_SCAN,
    // Compliance Reporting
    COMPLIANCE_REPORT, AUDIT_TRAIL, EXPORT_REPORT,
    REGULATORY_CHECK, SOC2_CHECK, HIPAA_CHECK,
    // General
    HEALTH_CHECK, FULL_AUDIT, REMEDIATION_PLAN
};

constexpr std::string_view openclaw_operation_to_string(OpenClawOperation op);

// ─── Severity Levels ────────────────────────────────────────────────────────

enum class VulnerabilitySeverity { CRITICAL, HIGH, MEDIUM, LOW, INFORMATIONAL };

constexpr std::string_view vuln_severity_to_string(VulnerabilitySeverity s) {
    switch (s) {
        case VulnerabilitySeverity::CRITICAL:      return "CRITICAL";
        case VulnerabilitySeverity::HIGH:           return "HIGH";
        case VulnerabilitySeverity::MEDIUM:         return "MEDIUM";
        case VulnerabilitySeverity::LOW:            return "LOW";
        case VulnerabilitySeverity::INFORMATIONAL:  return "INFORMATIONAL";
    }
    return "UNKNOWN";
}

// ─── License Types ──────────────────────────────────────────────────────────

enum class LicenseCategory { PERMISSIVE, WEAK_COPYLEFT, STRONG_COPYLEFT, PROPRIETARY, UNKNOWN };

constexpr std::string_view license_category_to_string(LicenseCategory c) {
    switch (c) {
        case LicenseCategory::PERMISSIVE:      return "PERMISSIVE";
        case LicenseCategory::WEAK_COPYLEFT:   return "WEAK_COPYLEFT";
        case LicenseCategory::STRONG_COPYLEFT: return "STRONG_COPYLEFT";
        case LicenseCategory::PROPRIETARY:     return "PROPRIETARY";
        case LicenseCategory::UNKNOWN:         return "UNKNOWN";
    }
    return "UNKNOWN";
}

// ─── OpenClaw Data Structures ───────────────────────────────────────────────

struct LicenseInfo {
    std::string spdx_id;           // e.g., "MIT", "Apache-2.0", "GPL-3.0-only"
    std::string name;
    LicenseCategory category = LicenseCategory::UNKNOWN;
    bool osi_approved = false;
    bool copyleft = false;
    std::vector<std::string> permissions;
    std::vector<std::string> conditions;
    std::vector<std::string> limitations;
};

struct Dependency {
    std::string name;
    std::string version;
    std::string ecosystem;          // npm, pip, cargo, go, maven, nuget
    std::string license_spdx;
    LicenseCategory license_category = LicenseCategory::UNKNOWN;
    bool direct = true;
    int32_t depth = 0;              // 0 = direct, 1+ = transitive
    int32_t vulnerability_count = 0;
    double risk_score = 0.0;        // 0.0 - 10.0
    bool deprecated = false;
    std::string latest_version;
    std::string source_url;
};

struct Vulnerability {
    std::string cve_id;
    std::string title;
    std::string description;
    VulnerabilitySeverity severity = VulnerabilitySeverity::MEDIUM;
    double cvss_score = 0.0;
    std::string cvss_vector;
    std::string affected_package;
    std::string affected_versions;
    std::string fixed_version;
    bool exploit_available = false;
    bool patch_available = false;
    std::string published_at;
    std::string references_json = "[]";
    std::string remediation;
};

struct SBOMEntry {
    std::string name;
    std::string version;
    std::string type;               // library, framework, application, os
    std::string supplier;
    std::string license;
    std::string purl;               // package URL
    std::string checksum_sha256;
    std::vector<std::string> external_refs;
};

struct SBOM {
    std::string id;
    std::string format;             // spdx, cyclonedx
    std::string spec_version;
    std::string project_name;
    std::string project_version;
    int32_t component_count = 0;
    std::vector<SBOMEntry> components;
    std::string created_at;
    std::string tool_name = "prodxcloud-openclaw";
};

struct CompliancePolicy {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> allowed_licenses;
    std::vector<std::string> denied_licenses;
    double max_cvss_score = 9.0;
    bool block_critical_vulns = true;
    bool require_sbom = false;
    bool enforce_signatures = false;
    int32_t max_dependency_depth = -1;  // -1 = unlimited
    std::string created_at;
};

struct PolicyViolation {
    std::string policy_id;
    std::string violation_type;     // license, vulnerability, deprecated, unsigned
    std::string package_name;
    std::string package_version;
    std::string detail;
    VulnerabilitySeverity severity = VulnerabilitySeverity::MEDIUM;
    std::string remediation;
    bool blocking = false;
};

struct AuditReport {
    std::string id;
    std::string project_name;
    int32_t total_dependencies = 0;
    int32_t direct_dependencies = 0;
    int32_t transitive_dependencies = 0;
    int32_t vulnerability_count = 0;
    int32_t critical_count = 0;
    int32_t high_count = 0;
    int32_t medium_count = 0;
    int32_t low_count = 0;
    int32_t license_issues = 0;
    int32_t policy_violations = 0;
    double overall_risk_score = 0.0;
    std::string compliance_status;  // pass, fail, warning
    std::vector<PolicyViolation> violations;
    std::string generated_at;
};

// ─── OpenClaw Operation Request ─────────────────────────────────────────────

struct OpenClawOperationRequest {
    OpenClawOperation operation;
    std::string project_path;
    std::string project_name;
    std::string ecosystem;          // auto-detect if empty
    // License params
    std::string license_spdx;
    std::vector<std::string> allowed_licenses;
    std::vector<std::string> denied_licenses;
    // Vulnerability params
    std::string cve_id;
    double min_cvss = 0.0;
    VulnerabilitySeverity min_severity = VulnerabilitySeverity::LOW;
    // SBOM params
    std::string sbom_format = "cyclonedx";
    std::string sbom_path;
    // Policy params
    std::string policy_id;
    std::string policy_json = "{}";
    // Scan params
    bool include_transitive = true;
    bool include_dev_deps = false;
    int32_t max_depth = -1;
    // Export params
    std::string export_format = "json";
    std::string output_path;
    // General
    bool dry_run = false;
    int32_t concurrency = 8;
    std::string config_json = "{}";
};

// ─── OpenClaw Operation Result ──────────────────────────────────────────────

struct OpenClawOperationResult {
    bool success = false;
    OpenClawOperation operation;
    std::string output;
    std::string error_message;
    double duration_ms = 0.0;
    std::string timestamp;
    // Populated depending on operation
    std::vector<Dependency> dependencies;
    std::vector<Vulnerability> vulnerabilities;
    std::vector<LicenseInfo> licenses;
    std::vector<PolicyViolation> violations;
    SBOM sbom;
    AuditReport audit;
    double risk_score = 0.0;
    std::string compliance_status;
};

// ─── OpenClaw Agent ─────────────────────────────────────────────────────────

class OpenClawAgent : public AgentBase {
public:
    explicit OpenClawAgent(AgentConfig config);
    ~OpenClawAgent() override = default;

    Result<TaskResult> execute(Task& task) override;
    void cancel() override;
    Result<bool> health_check() override;

    // Direct operation interface
    Result<OpenClawOperationResult> execute_openclaw_operation(const OpenClawOperationRequest& req);

    // License scanning
    Result<OpenClawOperationResult> scan_licenses(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> check_license_compatibility(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> get_license_report(const OpenClawOperationRequest& req);

    // Vulnerability assessment
    Result<OpenClawOperationResult> scan_vulnerabilities(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> get_cve_details(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> list_vulnerabilities(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> prioritize_vulnerabilities(const OpenClawOperationRequest& req);

    // Dependency auditing
    Result<OpenClawOperationResult> audit_dependencies(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> list_dependencies(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> dependency_tree(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> check_outdated(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> dependency_risk_score(const OpenClawOperationRequest& req);

    // SBOM operations
    Result<OpenClawOperationResult> generate_sbom(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> validate_sbom(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> compare_sbom(const OpenClawOperationRequest& req);

    // Supply chain security
    Result<OpenClawOperationResult> verify_signatures(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> check_provenance(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> detect_typosquat(const OpenClawOperationRequest& req);

    // Policy enforcement
    Result<OpenClawOperationResult> enforce_policy(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> create_policy(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> list_policies(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> gate_check(const OpenClawOperationRequest& req);

    // Code scanning
    Result<OpenClawOperationResult> scan_secrets(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> sast_scan(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> container_scan(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> iac_scan(const OpenClawOperationRequest& req);

    // Compliance
    Result<OpenClawOperationResult> compliance_report(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> full_audit(const OpenClawOperationRequest& req);
    Result<OpenClawOperationResult> remediation_plan(const OpenClawOperationRequest& req);

    // Metrics
    [[nodiscard]] size_t policy_count() const;
    [[nodiscard]] size_t cached_vuln_count() const;

private:
    mutable std::shared_mutex data_mutex_;
    std::unordered_map<std::string, CompliancePolicy> policies_;
    std::unordered_map<std::string, Vulnerability> vuln_cache_;
    std::unordered_map<std::string, LicenseInfo> license_db_;

    // Scanner backends
    Result<std::vector<Dependency>> scan_npm_dependencies(const std::string& path);
    Result<std::vector<Dependency>> scan_pip_dependencies(const std::string& path);
    Result<std::vector<Dependency>> scan_cargo_dependencies(const std::string& path);
    Result<std::vector<Dependency>> scan_go_dependencies(const std::string& path);
    Result<std::vector<Dependency>> scan_maven_dependencies(const std::string& path);
    Result<std::vector<Dependency>> auto_detect_and_scan(const std::string& path);

    // Internal helpers
    OpenClawOperationResult make_result(OpenClawOperation op);
    OpenClawOperationRequest parse_task_to_request(const Task& task);
    LicenseCategory classify_license(const std::string& spdx_id);
    double compute_risk_score(const std::vector<Dependency>& deps,
                              const std::vector<Vulnerability>& vulns);
    bool check_license_allowed(const std::string& license,
                               const CompliancePolicy& policy);
    void populate_license_db();
};

}  // namespace prodxcloud::agents::specialized
