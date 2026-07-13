#pragma once

/// @file security_engine.hpp
/// @brief Zero Trust Security Engine — secrets management, policy-as-code,
///        RBAC enforcement, network policy, vulnerability scanning, and
///        compliance frameworks (SOC2, ISO 27001/28743, HIPAA).
///
/// Platform Pillar: Security & Compliance
/// Policy-as-code, least privilege by default, SOC2/ISO28743 ready.

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/types.hpp"

namespace prodxcloud::platform::security {

// ─── Secret Types ───────────────────────────────────────────────────────────

struct Secret {
    std::string id;
    std::string name;
    std::string tenant_id;
    std::string path;               // vault path: /secrets/prod/db_password
    std::string type;               // password, api_key, certificate, ssh_key, token, generic
    std::string encrypted_value;    // AES-256-GCM encrypted
    int32_t version = 1;
    std::string rotation_policy;    // never, 30d, 60d, 90d
    std::string last_rotated_at;
    std::string expires_at;
    std::vector<std::string> allowed_services;
    std::string created_by;
    std::string created_at;
    std::string updated_at;
};

struct SecretAccess {
    std::string secret_id;
    std::string accessor;           // service or user
    std::string action;             // read, write, rotate, delete
    std::string ip_address;
    bool allowed = false;
    std::string timestamp;
};

// ─── RBAC Types ─────────────────────────────────────────────────────────────

struct Role {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> permissions;  // resource:action e.g., "deployments:create"
    std::string scope;              // global, tenant, team
    bool is_system = false;         // built-in roles cannot be modified
    std::string created_at;
};

struct RoleBinding {
    std::string id;
    std::string principal;          // user or service account
    std::string principal_type;     // user, service_account, group
    std::string role_id;
    std::string scope;              // which tenant/namespace
    std::string condition_json = "{}"; // conditional access (time, IP, etc.)
    std::string created_at;
    std::string expires_at;
};

struct AccessDecision {
    bool allowed = false;
    std::string principal;
    std::string resource;
    std::string action;
    std::string matched_role;
    std::string reason;
    std::string evaluated_at;
};

// ─── Network Policy ─────────────────────────────────────────────────────────

struct NetworkPolicy {
    std::string id;
    std::string name;
    std::string tenant_id;
    std::string namespace_name;
    std::string direction;          // ingress, egress, both
    std::vector<std::string> allow_from;    // CIDR blocks or service names
    std::vector<std::string> allow_to;
    std::vector<int32_t> allow_ports;
    std::string protocol = "tcp";
    bool default_deny = true;       // zero-trust: deny by default
    bool enabled = true;
    std::string created_at;
};

// ─── Policy-as-Code ─────────────────────────────────────────────────────────

struct SecurityPolicy {
    std::string id;
    std::string name;
    std::string description;
    std::string tenant_id;
    std::string category;           // access, network, data, deployment, secret, compliance
    std::string engine;             // opa, kyverno, sentinel, custom
    std::string policy_code;        // rego, yaml, hcl
    std::string enforcement;        // enforce, audit, warn
    bool enabled = true;
    int32_t violation_count = 0;
    std::string last_evaluated_at;
    std::string created_at;
};

struct PolicyViolation {
    std::string id;
    std::string policy_id;
    std::string policy_name;
    std::string resource;
    std::string resource_type;
    std::string violation;
    std::string severity;           // low, medium, high, critical
    std::string remediation;
    bool resolved = false;
    std::string detected_at;
    std::string resolved_at;
};

// ─── Vulnerability ──────────────────────────────────────────────────────────

struct SecurityVulnerability {
    std::string id;
    std::string cve_id;
    std::string service;
    std::string component;
    std::string severity;           // low, medium, high, critical
    double cvss_score = 0.0;
    std::string description;
    std::string fix_version;
    bool exploitable = false;
    std::string status;             // open, in_progress, mitigated, resolved, accepted
    std::string discovered_at;
    std::string resolved_at;
};

// ─── Compliance Framework ───────────────────────────────────────────────────

struct ComplianceControl {
    std::string id;
    std::string framework;          // soc2, iso27001, iso28743, hipaa, pci_dss, gdpr
    std::string control_id;         // e.g., "CC6.1", "A.9.1.1"
    std::string title;
    std::string description;
    std::string category;           // access_control, encryption, logging, etc.
    std::string status;             // compliant, non_compliant, partial, not_applicable
    std::string evidence;
    double compliance_percent = 0.0;
    std::string last_assessed_at;
    std::string next_review_at;
};

struct ComplianceReport {
    std::string id;
    std::string tenant_id;
    std::string framework;
    double overall_compliance = 0.0;
    int32_t total_controls = 0;
    int32_t compliant = 0;
    int32_t non_compliant = 0;
    int32_t partial = 0;
    int32_t not_applicable = 0;
    std::vector<ComplianceControl> controls;
    std::string generated_at;
    std::string valid_until;
};

// ─── Audit Log ──────────────────────────────────────────────────────────────

struct AuditEvent {
    std::string id;
    std::string tenant_id;
    std::string actor;
    std::string action;             // create, read, update, delete, login, logout, etc.
    std::string resource_type;
    std::string resource_id;
    std::string ip_address;
    std::string user_agent;
    bool success = true;
    std::string detail;
    std::string timestamp;
};

// ─── Security Posture ───────────────────────────────────────────────────────

struct SecurityPosture {
    std::string tenant_id;
    double security_score = 0.0;    // 0-100
    std::string grade;              // A+, A, B, C, D, F
    int32_t critical_vulns = 0;
    int32_t high_vulns = 0;
    int32_t medium_vulns = 0;
    int32_t open_violations = 0;
    int32_t secrets_expiring_30d = 0;
    int32_t unrotated_secrets = 0;
    double rbac_coverage = 0.0;     // % of resources with RBAC
    double encryption_coverage = 0.0;
    double mfa_adoption = 0.0;
    bool default_deny_enabled = true;
    std::string soc2_status;
    std::string iso27001_status;
    std::string generated_at;
};

// ─── Security Engine ────────────────────────────────────────────────────────

class SecurityEngine {
public:
    SecurityEngine();
    ~SecurityEngine() = default;

    // Secrets Vault
    Result<Secret> create_secret(const std::string& tenant_id, const std::string& name,
                                  const std::string& value, const std::string& type = "generic",
                                  const std::string& rotation_policy = "90d");
    Result<std::string> get_secret_value(const std::string& secret_id,
                                          const std::string& accessor);
    Result<Secret> rotate_secret(const std::string& secret_id, const std::string& new_value);
    Result<void> delete_secret(const std::string& secret_id);
    std::vector<Secret> list_secrets(const std::string& tenant_id) const;
    Result<std::vector<Secret>> find_expiring_secrets(int32_t within_days = 30) const;
    std::vector<SecretAccess> get_access_log(const std::string& secret_id) const;

    // RBAC
    Result<Role> create_role(const std::string& name,
                              const std::vector<std::string>& permissions,
                              const std::string& scope = "tenant");
    Result<Role> get_role(const std::string& role_id) const;
    std::vector<Role> list_roles() const;
    Result<void> delete_role(const std::string& role_id);
    Result<RoleBinding> bind_role(const std::string& principal,
                                   const std::string& principal_type,
                                   const std::string& role_id,
                                   const std::string& scope);
    Result<void> unbind_role(const std::string& binding_id);
    Result<AccessDecision> check_access(const std::string& principal,
                                         const std::string& resource,
                                         const std::string& action);
    std::vector<RoleBinding> list_bindings(const std::string& principal = "") const;

    // Network Policies (Zero Trust)
    Result<NetworkPolicy> create_network_policy(const std::string& tenant_id,
                                                 const std::string& name,
                                                 const std::string& direction,
                                                 const std::vector<std::string>& allow_from,
                                                 const std::vector<int32_t>& allow_ports,
                                                 bool default_deny = true);
    Result<NetworkPolicy> get_network_policy(const std::string& policy_id) const;
    std::vector<NetworkPolicy> list_network_policies(const std::string& tenant_id) const;
    Result<void> delete_network_policy(const std::string& policy_id);

    // Policy-as-Code
    Result<SecurityPolicy> create_security_policy(const std::string& tenant_id,
                                                    const std::string& name,
                                                    const std::string& category,
                                                    const std::string& policy_code,
                                                    const std::string& engine = "opa",
                                                    const std::string& enforcement = "enforce");
    Result<SecurityPolicy> get_security_policy(const std::string& policy_id) const;
    std::vector<SecurityPolicy> list_security_policies(const std::string& tenant_id) const;
    Result<std::vector<PolicyViolation>> evaluate_policy(const std::string& policy_id);
    Result<std::vector<PolicyViolation>> evaluate_all_policies(const std::string& tenant_id);
    std::vector<PolicyViolation> list_violations(const std::string& tenant_id,
                                                  bool unresolved_only = true) const;
    Result<void> resolve_violation(const std::string& violation_id);

    // Vulnerability Management
    Result<SecurityVulnerability> report_vulnerability(const std::string& service,
                                                        const std::string& cve_id,
                                                        const std::string& severity,
                                                        double cvss_score);
    std::vector<SecurityVulnerability> list_vulnerabilities(const std::string& service = "",
                                                             const std::string& status = "") const;
    Result<void> update_vulnerability_status(const std::string& vuln_id,
                                              const std::string& status);
    Result<void> scan_pipeline(const std::string& pipeline_id);

    // Compliance
    Result<ComplianceReport> generate_compliance_report(const std::string& tenant_id,
                                                         const std::string& framework);
    Result<ComplianceReport> get_soc2_report(const std::string& tenant_id);
    Result<ComplianceReport> get_iso27001_report(const std::string& tenant_id);
    Result<ComplianceReport> get_iso28743_report(const std::string& tenant_id);
    Result<ComplianceReport> get_hipaa_report(const std::string& tenant_id);
    std::vector<ComplianceControl> list_controls(const std::string& framework) const;
    Result<void> update_control_status(const std::string& control_id,
                                        const std::string& status,
                                        const std::string& evidence = "");

    // Audit Trail
    Result<void> log_audit_event(AuditEvent event);
    std::vector<AuditEvent> query_audit_log(const std::string& tenant_id,
                                             const std::string& actor = "",
                                             const std::string& action = "",
                                             int32_t limit = 100) const;

    // Security Posture
    Result<SecurityPosture> get_security_posture(const std::string& tenant_id);
    Result<double> get_security_score(const std::string& tenant_id);

    // Stats
    [[nodiscard]] size_t secret_count() const;
    [[nodiscard]] size_t role_count() const;
    [[nodiscard]] size_t policy_count() const;
    [[nodiscard]] size_t vulnerability_count() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Secret> secrets_;
    std::vector<SecretAccess> secret_access_log_;
    std::unordered_map<std::string, Role> roles_;
    std::vector<RoleBinding> role_bindings_;
    std::unordered_map<std::string, NetworkPolicy> network_policies_;
    std::unordered_map<std::string, SecurityPolicy> security_policies_;
    std::vector<PolicyViolation> violations_;
    std::vector<SecurityVulnerability> vulnerabilities_;
    std::unordered_map<std::string, std::vector<ComplianceControl>> compliance_controls_;
    std::vector<AuditEvent> audit_log_;

    void populate_default_roles();
    void populate_compliance_frameworks();
    std::string encrypt_value(const std::string& plaintext);
    std::string decrypt_value(const std::string& ciphertext);
    double compute_security_score(const SecurityPosture& posture);
    std::string grade_from_score(double score);
    std::vector<ComplianceControl> generate_soc2_controls();
    std::vector<ComplianceControl> generate_iso27001_controls();
    std::vector<ComplianceControl> generate_iso28743_controls();
    std::vector<ComplianceControl> generate_hipaa_controls();
};

}  // namespace prodxcloud::platform::security
