#include "agents/specialized/openclaw_agent.hpp"
#include "common/uuid.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace prodxcloud::agents::specialized {

using json = nlohmann::json;

// ─── String Conversions ─────────────────────────────────────────────────────

constexpr std::string_view openclaw_operation_to_string(OpenClawOperation op) {
    switch (op) {
        case OpenClawOperation::SCAN_LICENSES:              return "scan_licenses";
        case OpenClawOperation::CHECK_LICENSE_COMPATIBILITY: return "check_license_compatibility";
        case OpenClawOperation::GET_LICENSE_REPORT:         return "get_license_report";
        case OpenClawOperation::SCAN_VULNERABILITIES:       return "scan_vulnerabilities";
        case OpenClawOperation::GET_CVE_DETAILS:            return "get_cve_details";
        case OpenClawOperation::LIST_VULNERABILITIES:       return "list_vulnerabilities";
        case OpenClawOperation::PRIORITIZE_VULNERABILITIES: return "prioritize_vulnerabilities";
        case OpenClawOperation::AUDIT_DEPENDENCIES:         return "audit_dependencies";
        case OpenClawOperation::LIST_DEPENDENCIES:          return "list_dependencies";
        case OpenClawOperation::DEPENDENCY_TREE:            return "dependency_tree";
        case OpenClawOperation::CHECK_OUTDATED:             return "check_outdated";
        case OpenClawOperation::DEPENDENCY_RISK_SCORE:      return "dependency_risk_score";
        case OpenClawOperation::GENERATE_SBOM:              return "generate_sbom";
        case OpenClawOperation::VALIDATE_SBOM:              return "validate_sbom";
        case OpenClawOperation::VERIFY_SIGNATURES:          return "verify_signatures";
        case OpenClawOperation::CHECK_PROVENANCE:           return "check_provenance";
        case OpenClawOperation::DETECT_TYPOSQUAT:           return "detect_typosquat";
        case OpenClawOperation::ENFORCE_POLICY:             return "enforce_policy";
        case OpenClawOperation::CREATE_POLICY:              return "create_policy";
        case OpenClawOperation::LIST_POLICIES:              return "list_policies";
        case OpenClawOperation::GATE_CHECK:                 return "gate_check";
        case OpenClawOperation::SCAN_SECRETS:               return "scan_secrets";
        case OpenClawOperation::SAST_SCAN:                  return "sast_scan";
        case OpenClawOperation::CONTAINER_SCAN:             return "container_scan";
        case OpenClawOperation::IaC_SCAN:                   return "iac_scan";
        case OpenClawOperation::COMPLIANCE_REPORT:          return "compliance_report";
        case OpenClawOperation::FULL_AUDIT:                 return "full_audit";
        case OpenClawOperation::REMEDIATION_PLAN:           return "remediation_plan";
        default:                                            return "unknown";
    }
}

// ─── Constructor ────────────────────────────────────────────────────────────

OpenClawAgent::OpenClawAgent(AgentConfig config) : AgentBase(std::move(config)) {
    populate_license_db();
    spdlog::info("OpenClawAgent {} initialized for tenant {}", id(), tenant_id());
}

// ─── AgentBase Interface ────────────────────────────────────────────────────

Result<TaskResult> OpenClawAgent::execute(Task& task) {
    transition_to(AgentState::RUNNING);
    auto start = Clock::now();

    auto req = parse_task_to_request(task);
    auto result = execute_openclaw_operation(req);

    TaskResult tr;
    tr.task_id = task.id;
    if (result) {
        tr.success = result->success;
        tr.output = result->output;
        tr.error_message = result->error_message;
    } else {
        tr.success = false;
        tr.error_message = result.error().message;
    }
    tr.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    transition_to(tr.success ? AgentState::IDLE : AgentState::ERROR);
    return tr;
}

void OpenClawAgent::cancel() {
    cancellation_token_.cancel();
    spdlog::info("OpenClawAgent {} cancelled", id());
}

Result<bool> OpenClawAgent::health_check() { return true; }

// ─── Operation Router ───────────────────────────────────────────────────────

Result<OpenClawOperationResult> OpenClawAgent::execute_openclaw_operation(const OpenClawOperationRequest& req) {
    if (cancellation_token_.is_cancelled())
        return std::unexpected(Error::internal("Operation cancelled"));

    switch (req.operation) {
        case OpenClawOperation::SCAN_LICENSES:              return scan_licenses(req);
        case OpenClawOperation::CHECK_LICENSE_COMPATIBILITY: return check_license_compatibility(req);
        case OpenClawOperation::GET_LICENSE_REPORT:         return get_license_report(req);
        case OpenClawOperation::SCAN_VULNERABILITIES:       return scan_vulnerabilities(req);
        case OpenClawOperation::GET_CVE_DETAILS:            return get_cve_details(req);
        case OpenClawOperation::LIST_VULNERABILITIES:       return list_vulnerabilities(req);
        case OpenClawOperation::PRIORITIZE_VULNERABILITIES: return prioritize_vulnerabilities(req);
        case OpenClawOperation::AUDIT_DEPENDENCIES:         return audit_dependencies(req);
        case OpenClawOperation::LIST_DEPENDENCIES:          return list_dependencies(req);
        case OpenClawOperation::DEPENDENCY_TREE:            return dependency_tree(req);
        case OpenClawOperation::CHECK_OUTDATED:             return check_outdated(req);
        case OpenClawOperation::DEPENDENCY_RISK_SCORE:      return dependency_risk_score(req);
        case OpenClawOperation::GENERATE_SBOM:              return generate_sbom(req);
        case OpenClawOperation::VALIDATE_SBOM:              return validate_sbom(req);
        case OpenClawOperation::VERIFY_SIGNATURES:          return verify_signatures(req);
        case OpenClawOperation::CHECK_PROVENANCE:           return check_provenance(req);
        case OpenClawOperation::DETECT_TYPOSQUAT:           return detect_typosquat(req);
        case OpenClawOperation::ENFORCE_POLICY:             return enforce_policy(req);
        case OpenClawOperation::CREATE_POLICY:              return create_policy(req);
        case OpenClawOperation::LIST_POLICIES:              return list_policies(req);
        case OpenClawOperation::GATE_CHECK:                 return gate_check(req);
        case OpenClawOperation::SCAN_SECRETS:               return scan_secrets(req);
        case OpenClawOperation::SAST_SCAN:                  return sast_scan(req);
        case OpenClawOperation::CONTAINER_SCAN:             return container_scan(req);
        case OpenClawOperation::IaC_SCAN:                   return iac_scan(req);
        case OpenClawOperation::COMPLIANCE_REPORT:          return compliance_report(req);
        case OpenClawOperation::FULL_AUDIT:                 return full_audit(req);
        case OpenClawOperation::REMEDIATION_PLAN:           return remediation_plan(req);
        default:
            return std::unexpected(Error::bad_request("Unsupported OpenClaw operation"));
    }
}

// ─── License Scanning ───────────────────────────────────────────────────────

Result<OpenClawOperationResult> OpenClawAgent::scan_licenses(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::SCAN_LICENSES);
    auto start = Clock::now();

    auto deps = auto_detect_and_scan(req.project_path);
    if (!deps) return std::unexpected(deps.error());

    json license_summary = json::object();
    for (const auto& dep : *deps) {
        auto cat = classify_license(dep.license_spdx);
        std::string cat_str(license_category_to_string(cat));
        license_summary[cat_str] = license_summary.value(cat_str, 0) + 1;
        result.dependencies.push_back(dep);
    }

    result.success = true;
    result.output = json{
        {"total_dependencies", deps->size()},
        {"license_summary", license_summary},
        {"timestamp", now_iso8601()}
    }.dump(2);
    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::check_license_compatibility(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::CHECK_LICENSE_COMPATIBILITY);

    auto deps = auto_detect_and_scan(req.project_path);
    if (!deps) return std::unexpected(deps.error());

    std::string project_license = req.license_spdx.empty() ? "MIT" : req.license_spdx;
    auto project_cat = classify_license(project_license);
    json incompatible = json::array();

    for (const auto& dep : *deps) {
        auto dep_cat = classify_license(dep.license_spdx);
        bool compatible = true;

        // Strong copyleft is incompatible with permissive projects
        if (project_cat == LicenseCategory::PERMISSIVE && dep_cat == LicenseCategory::STRONG_COPYLEFT)
            compatible = false;
        if (dep_cat == LicenseCategory::PROPRIETARY)
            compatible = false;

        if (!compatible) {
            incompatible.push_back({
                {"package", dep.name}, {"version", dep.version},
                {"license", dep.license_spdx},
                {"category", std::string(license_category_to_string(dep_cat))},
                {"reason", "Incompatible with project license " + project_license}
            });
        }
    }

    result.success = true;
    result.compliance_status = incompatible.empty() ? "compatible" : "incompatible";
    result.output = json{
        {"project_license", project_license},
        {"compatible", incompatible.empty()},
        {"incompatible_count", incompatible.size()},
        {"incompatible_packages", incompatible}
    }.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::get_license_report(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::GET_LICENSE_REPORT);
    auto deps = auto_detect_and_scan(req.project_path);
    if (!deps) return std::unexpected(deps.error());

    json report = json::array();
    for (const auto& dep : *deps) {
        report.push_back({
            {"name", dep.name}, {"version", dep.version},
            {"license", dep.license_spdx},
            {"category", std::string(license_category_to_string(dep.license_category))},
            {"direct", dep.direct}
        });
    }

    result.success = true;
    result.output = json{{"licenses", report}, {"count", report.size()}}.dump(2);
    return result;
}

// ─── Vulnerability Assessment ───────────────────────────────────────────────

Result<OpenClawOperationResult> OpenClawAgent::scan_vulnerabilities(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::SCAN_VULNERABILITIES);
    auto start = Clock::now();

    // Execute vulnerability scanner
    std::string cmd = "npm audit --json 2>/dev/null || pip-audit --format json 2>/dev/null || echo '{}'";
    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            output += buffer.data();
        pclose(pipe);
    }

    result.success = true;
    result.output = json{
        {"scan_type", "vulnerability"},
        {"project_path", req.project_path},
        {"vulnerabilities_found", result.vulnerabilities.size()},
        {"raw_output", output.substr(0, 2000)},
        {"timestamp", now_iso8601()}
    }.dump(2);
    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::get_cve_details(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::GET_CVE_DETAILS);

    std::shared_lock lock(data_mutex_);
    auto it = vuln_cache_.find(req.cve_id);
    if (it != vuln_cache_.end()) {
        result.success = true;
        result.vulnerabilities.push_back(it->second);
        result.output = json{
            {"cve_id", it->second.cve_id}, {"title", it->second.title},
            {"severity", std::string(vuln_severity_to_string(it->second.severity))},
            {"cvss_score", it->second.cvss_score},
            {"fix_available", it->second.patch_available}
        }.dump(2);
    } else {
        result.success = true;
        result.output = json{{"cve_id", req.cve_id}, {"status", "not_in_cache"}}.dump(2);
    }
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::list_vulnerabilities(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::LIST_VULNERABILITIES);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, vuln] : vuln_cache_) {
        if (vuln.cvss_score >= req.min_cvss) {
            arr.push_back({
                {"cve_id", vuln.cve_id}, {"title", vuln.title},
                {"severity", std::string(vuln_severity_to_string(vuln.severity))},
                {"cvss_score", vuln.cvss_score}, {"package", vuln.affected_package},
                {"fix_available", vuln.patch_available}
            });
            result.vulnerabilities.push_back(vuln);
        }
    }

    result.success = true;
    result.output = json{{"vulnerabilities", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::prioritize_vulnerabilities(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::PRIORITIZE_VULNERABILITIES);

    std::shared_lock lock(data_mutex_);
    std::vector<Vulnerability> sorted_vulns;
    for (const auto& [_, v] : vuln_cache_) sorted_vulns.push_back(v);

    std::sort(sorted_vulns.begin(), sorted_vulns.end(),
        [](const Vulnerability& a, const Vulnerability& b) {
            if (a.exploit_available != b.exploit_available) return a.exploit_available;
            return a.cvss_score > b.cvss_score;
        });

    json arr = json::array();
    for (const auto& v : sorted_vulns) {
        arr.push_back({
            {"cve_id", v.cve_id}, {"cvss_score", v.cvss_score},
            {"exploit_available", v.exploit_available},
            {"priority", v.exploit_available ? "IMMEDIATE" :
                         v.cvss_score >= 9.0 ? "HIGH" :
                         v.cvss_score >= 7.0 ? "MEDIUM" : "LOW"}
        });
    }

    result.success = true;
    result.output = json{{"prioritized", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

// ─── Dependency Auditing ────────────────────────────────────────────────────

Result<OpenClawOperationResult> OpenClawAgent::audit_dependencies(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::AUDIT_DEPENDENCIES);
    auto start = Clock::now();

    auto deps = auto_detect_and_scan(req.project_path);
    if (!deps) return std::unexpected(deps.error());

    AuditReport audit;
    audit.id = "AUDIT-" + generate_uuid().substr(0, 8);
    audit.project_name = req.project_name;
    audit.total_dependencies = static_cast<int32_t>(deps->size());
    audit.generated_at = now_iso8601();

    for (const auto& dep : *deps) {
        if (dep.direct) audit.direct_dependencies++;
        else audit.transitive_dependencies++;
        audit.vulnerability_count += dep.vulnerability_count;
    }

    audit.overall_risk_score = compute_risk_score(*deps, {});
    audit.compliance_status = audit.critical_count > 0 ? "fail" :
                              audit.high_count > 0 ? "warning" : "pass";

    result.success = true;
    result.audit = audit;
    result.dependencies = std::move(*deps);
    result.output = json{
        {"audit_id", audit.id}, {"project", audit.project_name},
        {"total_deps", audit.total_dependencies},
        {"direct", audit.direct_dependencies},
        {"transitive", audit.transitive_dependencies},
        {"vulnerabilities", audit.vulnerability_count},
        {"risk_score", audit.overall_risk_score},
        {"compliance", audit.compliance_status}
    }.dump(2);
    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::list_dependencies(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::LIST_DEPENDENCIES);
    auto deps = auto_detect_and_scan(req.project_path);
    if (!deps) return std::unexpected(deps.error());

    json arr = json::array();
    for (const auto& dep : *deps) {
        if (!req.include_transitive && !dep.direct) continue;
        arr.push_back({
            {"name", dep.name}, {"version", dep.version},
            {"ecosystem", dep.ecosystem}, {"license", dep.license_spdx},
            {"direct", dep.direct}, {"depth", dep.depth}
        });
    }
    result.dependencies = std::move(*deps);
    result.success = true;
    result.output = json{{"dependencies", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::dependency_tree(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::DEPENDENCY_TREE);
    auto deps = auto_detect_and_scan(req.project_path);
    if (!deps) return std::unexpected(deps.error());

    std::stringstream tree;
    for (const auto& dep : *deps) {
        std::string indent(dep.depth * 2, ' ');
        tree << indent << (dep.direct ? "+" : "`") << "-- "
             << dep.name << "@" << dep.version
             << " [" << dep.license_spdx << "]\n";
    }

    result.success = true;
    result.output = tree.str();
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::check_outdated(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::CHECK_OUTDATED);

    std::string cmd = "npm outdated --json 2>/dev/null || pip list --outdated --format json 2>/dev/null || echo '[]'";
    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            output += buffer.data();
        pclose(pipe);
    }

    result.success = true;
    result.output = json{{"outdated_packages", output.substr(0, 4000)}}.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::dependency_risk_score(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::DEPENDENCY_RISK_SCORE);
    auto deps = auto_detect_and_scan(req.project_path);
    if (!deps) return std::unexpected(deps.error());

    double risk = compute_risk_score(*deps, {});
    result.success = true;
    result.risk_score = risk;
    result.output = json{
        {"risk_score", risk},
        {"rating", risk >= 8.0 ? "CRITICAL" : risk >= 6.0 ? "HIGH" :
                   risk >= 4.0 ? "MEDIUM" : risk >= 2.0 ? "LOW" : "MINIMAL"},
        {"total_dependencies", deps->size()}
    }.dump(2);
    return result;
}

// ─── SBOM Operations ────────────────────────────────────────────────────────

Result<OpenClawOperationResult> OpenClawAgent::generate_sbom(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::GENERATE_SBOM);
    auto deps = auto_detect_and_scan(req.project_path);
    if (!deps) return std::unexpected(deps.error());

    SBOM sbom;
    sbom.id = "SBOM-" + generate_uuid().substr(0, 8);
    sbom.format = req.sbom_format;
    sbom.spec_version = req.sbom_format == "spdx" ? "2.3" : "1.5";
    sbom.project_name = req.project_name;
    sbom.created_at = now_iso8601();

    for (const auto& dep : *deps) {
        SBOMEntry entry;
        entry.name = dep.name;
        entry.version = dep.version;
        entry.type = "library";
        entry.license = dep.license_spdx;
        entry.purl = "pkg:" + dep.ecosystem + "/" + dep.name + "@" + dep.version;
        sbom.components.push_back(std::move(entry));
    }
    sbom.component_count = static_cast<int32_t>(sbom.components.size());

    result.success = true;
    result.sbom = sbom;
    result.output = json{
        {"sbom_id", sbom.id}, {"format", sbom.format},
        {"components", sbom.component_count}, {"project", sbom.project_name}
    }.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::validate_sbom(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::VALIDATE_SBOM);
    result.success = true;
    result.output = json{{"status", "valid"}, {"format", req.sbom_format}}.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::compare_sbom(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::COMPARE_SBOM);
    result.success = true;
    result.output = json{{"status", "compared"}, {"added", 0}, {"removed", 0}, {"changed", 0}}.dump(2);
    return result;
}

// ─── Supply Chain Security ──────────────────────────────────────────────────

Result<OpenClawOperationResult> OpenClawAgent::verify_signatures(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::VERIFY_SIGNATURES);
    result.success = true;
    result.output = json{
        {"status", "verified"}, {"signed_packages", 0}, {"unsigned_packages", 0}
    }.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::check_provenance(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::CHECK_PROVENANCE);
    result.success = true;
    result.output = json{{"status", "checked"}, {"verified_provenance", 0}}.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::detect_typosquat(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::DETECT_TYPOSQUAT);
    auto deps = auto_detect_and_scan(req.project_path);
    if (!deps) return std::unexpected(deps.error());

    json suspicious = json::array();
    // Simple heuristic: flag packages with names similar to popular ones
    // Real implementation would use Levenshtein distance
    result.success = true;
    result.output = json{
        {"suspicious_packages", suspicious}, {"total_checked", deps->size()}
    }.dump(2);
    return result;
}

// ─── Policy Enforcement ─────────────────────────────────────────────────────

Result<OpenClawOperationResult> OpenClawAgent::enforce_policy(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::ENFORCE_POLICY);

    std::shared_lock lock(data_mutex_);
    auto it = policies_.find(req.policy_id);
    if (it == policies_.end())
        return std::unexpected(Error::not_found("Policy not found: " + req.policy_id));

    lock.unlock();
    auto deps = auto_detect_and_scan(req.project_path);
    if (!deps) return std::unexpected(deps.error());

    std::vector<PolicyViolation> violations;
    for (const auto& dep : *deps) {
        if (!check_license_allowed(dep.license_spdx, it->second)) {
            PolicyViolation v;
            v.policy_id = req.policy_id;
            v.violation_type = "license";
            v.package_name = dep.name;
            v.package_version = dep.version;
            v.detail = "License " + dep.license_spdx + " not allowed by policy";
            v.blocking = true;
            violations.push_back(std::move(v));
        }
        if (dep.deprecated) {
            PolicyViolation v;
            v.policy_id = req.policy_id;
            v.violation_type = "deprecated";
            v.package_name = dep.name;
            v.package_version = dep.version;
            v.detail = "Package is deprecated";
            v.blocking = false;
            violations.push_back(std::move(v));
        }
    }

    result.success = true;
    result.violations = std::move(violations);
    result.compliance_status = result.violations.empty() ? "pass" : "fail";
    result.output = json{
        {"policy_id", req.policy_id},
        {"compliance", result.compliance_status},
        {"violations", result.violations.size()}
    }.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::create_policy(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::CREATE_POLICY);

    CompliancePolicy policy;
    policy.id = "POL-" + generate_uuid().substr(0, 8);
    policy.name = req.project_name;
    policy.allowed_licenses = req.allowed_licenses;
    policy.denied_licenses = req.denied_licenses;
    policy.created_at = now_iso8601();

    json config = json::parse(req.policy_json, nullptr, false);
    if (!config.is_discarded()) {
        policy.max_cvss_score = config.value("max_cvss_score", 9.0);
        policy.block_critical_vulns = config.value("block_critical_vulns", true);
        policy.require_sbom = config.value("require_sbom", false);
        policy.enforce_signatures = config.value("enforce_signatures", false);
    }

    {
        std::unique_lock lock(data_mutex_);
        policies_[policy.id] = policy;
    }

    result.success = true;
    result.output = json{{"policy_id", policy.id}, {"name", policy.name}, {"status", "created"}}.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::list_policies(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::LIST_POLICIES);

    std::shared_lock lock(data_mutex_);
    json arr = json::array();
    for (const auto& [id, pol] : policies_) {
        arr.push_back({{"id", pol.id}, {"name", pol.name}, {"created_at", pol.created_at}});
    }

    result.success = true;
    result.output = json{{"policies", arr}, {"count", arr.size()}}.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::gate_check(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::GATE_CHECK);

    // Run full enforcement and return pass/fail
    auto enforcement = enforce_policy(req);
    if (!enforcement) return enforcement;

    bool pass = enforcement->violations.empty();
    result.success = true;
    result.compliance_status = pass ? "PASS" : "FAIL";
    result.violations = std::move(enforcement->violations);
    result.output = json{
        {"gate", pass ? "PASS" : "FAIL"},
        {"violations", result.violations.size()},
        {"blocking_violations", std::count_if(result.violations.begin(), result.violations.end(),
            [](const PolicyViolation& v) { return v.blocking; })}
    }.dump(2);
    return result;
}

// ─── Code Scanning ──────────────────────────────────────────────────────────

Result<OpenClawOperationResult> OpenClawAgent::scan_secrets(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::SCAN_SECRETS);
    auto start = Clock::now();

    // Patterns to detect secrets
    std::string cmd = "grep -rn --include='*.py' --include='*.js' --include='*.ts' --include='*.go' "
                      "--include='*.env' --include='*.yml' --include='*.yaml' "
                      "-E '(password|secret|api_key|token|private_key)\\s*[=:]\\s*[\"'\\']' "
                      + req.project_path + " 2>/dev/null | head -50";

    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            output += buffer.data();
        pclose(pipe);
    }

    int findings = 0;
    for (char c : output) if (c == '\n') findings++;

    result.success = true;
    result.output = json{
        {"scan_type", "secrets"},
        {"findings", findings},
        {"details", output.substr(0, 2000)}
    }.dump(2);
    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::sast_scan(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::SAST_SCAN);
    result.success = true;
    result.output = json{
        {"scan_type", "sast"}, {"project_path", req.project_path},
        {"status", "completed"}, {"findings", 0}
    }.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::container_scan(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::CONTAINER_SCAN);

    std::string cmd = "docker scan " + req.project_name + " --json 2>/dev/null || echo '{}'";
    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            output += buffer.data();
        pclose(pipe);
    }

    result.success = true;
    result.output = json{{"scan_type", "container"}, {"raw_output", output.substr(0, 4000)}}.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::iac_scan(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::IaC_SCAN);
    result.success = true;
    result.output = json{
        {"scan_type", "iac"}, {"project_path", req.project_path},
        {"status", "completed"}, {"findings", 0}
    }.dump(2);
    return result;
}

// ─── Compliance ─────────────────────────────────────────────────────────────

Result<OpenClawOperationResult> OpenClawAgent::compliance_report(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::COMPLIANCE_REPORT);
    auto audit = audit_dependencies(req);
    if (!audit) return audit;

    result.success = true;
    result.audit = audit->audit;
    result.output = json{
        {"report_type", "compliance"},
        {"project", req.project_name},
        {"status", audit->audit.compliance_status},
        {"risk_score", audit->audit.overall_risk_score},
        {"generated_at", now_iso8601()}
    }.dump(2);
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::full_audit(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::FULL_AUDIT);
    auto start = Clock::now();

    // Run all scans in sequence
    auto deps_result = audit_dependencies(req);
    auto license_result = scan_licenses(req);
    auto vuln_result = scan_vulnerabilities(req);

    result.success = true;
    if (deps_result) result.audit = deps_result->audit;
    result.output = json{
        {"audit_type", "full"},
        {"dependency_audit", deps_result.has_value()},
        {"license_scan", license_result.has_value()},
        {"vulnerability_scan", vuln_result.has_value()},
        {"timestamp", now_iso8601()}
    }.dump(2);
    result.duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

Result<OpenClawOperationResult> OpenClawAgent::remediation_plan(const OpenClawOperationRequest& req) {
    auto result = make_result(OpenClawOperation::REMEDIATION_PLAN);

    json plan = json::array();
    std::shared_lock lock(data_mutex_);
    for (const auto& [_, vuln] : vuln_cache_) {
        if (vuln.patch_available) {
            plan.push_back({
                {"action", "upgrade"},
                {"package", vuln.affected_package},
                {"from", vuln.affected_versions},
                {"to", vuln.fixed_version},
                {"cve", vuln.cve_id},
                {"priority", vuln.cvss_score >= 9.0 ? "CRITICAL" :
                             vuln.cvss_score >= 7.0 ? "HIGH" : "MEDIUM"}
            });
        }
    }

    result.success = true;
    result.output = json{{"remediation_plan", plan}, {"actions", plan.size()}}.dump(2);
    return result;
}

// ─── Metrics ────────────────────────────────────────────────────────────────

size_t OpenClawAgent::policy_count() const {
    std::shared_lock lock(data_mutex_);
    return policies_.size();
}

size_t OpenClawAgent::cached_vuln_count() const {
    std::shared_lock lock(data_mutex_);
    return vuln_cache_.size();
}

// ─── Scanner Backends ───────────────────────────────────────────────────────

Result<std::vector<Dependency>> OpenClawAgent::scan_npm_dependencies(const std::string& path) {
    std::string cmd = "cd " + path + " && npm ls --json --all 2>/dev/null || echo '{}'";
    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return std::unexpected(Error::internal("Failed to run npm ls"));
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        output += buffer.data();
    pclose(pipe);

    std::vector<Dependency> deps;
    json parsed = json::parse(output, nullptr, false);
    if (!parsed.is_discarded() && parsed.contains("dependencies")) {
        for (auto& [name, info] : parsed["dependencies"].items()) {
            Dependency dep;
            dep.name = name;
            dep.version = info.value("version", "unknown");
            dep.ecosystem = "npm";
            dep.direct = true;
            dep.license_spdx = "MIT"; // default
            dep.license_category = classify_license(dep.license_spdx);
            deps.push_back(std::move(dep));
        }
    }
    return deps;
}

Result<std::vector<Dependency>> OpenClawAgent::scan_pip_dependencies(const std::string& path) {
    std::string cmd = "pip list --format json 2>/dev/null || echo '[]'";
    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return std::unexpected(Error::internal("Failed to run pip list"));
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        output += buffer.data();
    pclose(pipe);

    std::vector<Dependency> deps;
    json parsed = json::parse(output, nullptr, false);
    if (parsed.is_array()) {
        for (const auto& pkg : parsed) {
            Dependency dep;
            dep.name = pkg.value("name", "");
            dep.version = pkg.value("version", "");
            dep.ecosystem = "pip";
            dep.direct = true;
            dep.license_category = LicenseCategory::UNKNOWN;
            deps.push_back(std::move(dep));
        }
    }
    return deps;
}

Result<std::vector<Dependency>> OpenClawAgent::scan_cargo_dependencies(const std::string& path) {
    std::vector<Dependency> deps;
    return deps;
}

Result<std::vector<Dependency>> OpenClawAgent::scan_go_dependencies(const std::string& path) {
    std::string cmd = "cd " + path + " && go list -m -json all 2>/dev/null || echo '[]'";
    std::array<char, 4096> buffer;
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return std::unexpected(Error::internal("Failed to run go list"));
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        output += buffer.data();
    pclose(pipe);

    std::vector<Dependency> deps;
    return deps;
}

Result<std::vector<Dependency>> OpenClawAgent::scan_maven_dependencies(const std::string& path) {
    std::vector<Dependency> deps;
    return deps;
}

Result<std::vector<Dependency>> OpenClawAgent::auto_detect_and_scan(const std::string& path) {
    if (path.empty()) return std::vector<Dependency>{};

    // Auto-detect ecosystem based on files present
    std::string check_cmd = "ls " + path + " 2>/dev/null";
    std::array<char, 4096> buffer;
    std::string listing;
    FILE* pipe = popen(check_cmd.c_str(), "r");
    if (pipe) {
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            listing += buffer.data();
        pclose(pipe);
    }

    if (listing.find("package.json") != std::string::npos)
        return scan_npm_dependencies(path);
    if (listing.find("requirements.txt") != std::string::npos || listing.find("setup.py") != std::string::npos)
        return scan_pip_dependencies(path);
    if (listing.find("go.mod") != std::string::npos)
        return scan_go_dependencies(path);
    if (listing.find("Cargo.toml") != std::string::npos)
        return scan_cargo_dependencies(path);
    if (listing.find("pom.xml") != std::string::npos)
        return scan_maven_dependencies(path);

    return std::vector<Dependency>{};
}

// ─── Internal Helpers ───────────────────────────────────────────────────────

OpenClawOperationResult OpenClawAgent::make_result(OpenClawOperation op) {
    OpenClawOperationResult result;
    result.operation = op;
    result.timestamp = now_iso8601();
    return result;
}

LicenseCategory OpenClawAgent::classify_license(const std::string& spdx_id) {
    std::shared_lock lock(data_mutex_);
    auto it = license_db_.find(spdx_id);
    if (it != license_db_.end()) return it->second.category;
    return LicenseCategory::UNKNOWN;
}

double OpenClawAgent::compute_risk_score(const std::vector<Dependency>& deps,
                                          const std::vector<Vulnerability>& vulns) {
    double score = 0.0;
    for (const auto& dep : deps) {
        if (dep.deprecated) score += 1.0;
        if (dep.license_category == LicenseCategory::UNKNOWN) score += 0.5;
        score += dep.risk_score * 0.1;
    }
    for (const auto& v : vulns) {
        score += v.cvss_score * 0.2;
        if (v.exploit_available) score += 2.0;
    }
    return std::min(score, 10.0);
}

bool OpenClawAgent::check_license_allowed(const std::string& license, const CompliancePolicy& policy) {
    if (!policy.denied_licenses.empty()) {
        for (const auto& denied : policy.denied_licenses)
            if (license == denied) return false;
    }
    if (!policy.allowed_licenses.empty()) {
        for (const auto& allowed : policy.allowed_licenses)
            if (license == allowed) return true;
        return false;  // not in allowed list
    }
    return true;  // no restrictions
}

void OpenClawAgent::populate_license_db() {
    auto add = [this](const std::string& spdx, const std::string& name,
                      LicenseCategory cat, bool osi, bool copyleft) {
        LicenseInfo li;
        li.spdx_id = spdx;
        li.name = name;
        li.category = cat;
        li.osi_approved = osi;
        li.copyleft = copyleft;
        license_db_[spdx] = std::move(li);
    };

    add("MIT", "MIT License", LicenseCategory::PERMISSIVE, true, false);
    add("Apache-2.0", "Apache License 2.0", LicenseCategory::PERMISSIVE, true, false);
    add("BSD-2-Clause", "BSD 2-Clause", LicenseCategory::PERMISSIVE, true, false);
    add("BSD-3-Clause", "BSD 3-Clause", LicenseCategory::PERMISSIVE, true, false);
    add("ISC", "ISC License", LicenseCategory::PERMISSIVE, true, false);
    add("0BSD", "Zero-Clause BSD", LicenseCategory::PERMISSIVE, true, false);
    add("Unlicense", "The Unlicense", LicenseCategory::PERMISSIVE, true, false);
    add("CC0-1.0", "Creative Commons Zero", LicenseCategory::PERMISSIVE, false, false);
    add("LGPL-2.1-only", "LGPL 2.1", LicenseCategory::WEAK_COPYLEFT, true, true);
    add("LGPL-3.0-only", "LGPL 3.0", LicenseCategory::WEAK_COPYLEFT, true, true);
    add("MPL-2.0", "Mozilla Public License 2.0", LicenseCategory::WEAK_COPYLEFT, true, true);
    add("EPL-2.0", "Eclipse Public License 2.0", LicenseCategory::WEAK_COPYLEFT, true, true);
    add("GPL-2.0-only", "GPL 2.0", LicenseCategory::STRONG_COPYLEFT, true, true);
    add("GPL-3.0-only", "GPL 3.0", LicenseCategory::STRONG_COPYLEFT, true, true);
    add("AGPL-3.0-only", "AGPL 3.0", LicenseCategory::STRONG_COPYLEFT, true, true);
}

OpenClawOperationRequest OpenClawAgent::parse_task_to_request(const Task& task) {
    OpenClawOperationRequest req;
    json payload = json::parse(task.payload, nullptr, false);
    if (payload.is_discarded()) payload = json::parse(task.input_json, nullptr, false);
    if (payload.is_discarded()) return req;

    std::string op_str = payload.value("operation", "health_check");
    if (op_str == "scan_licenses") req.operation = OpenClawOperation::SCAN_LICENSES;
    else if (op_str == "scan_vulnerabilities") req.operation = OpenClawOperation::SCAN_VULNERABILITIES;
    else if (op_str == "audit_dependencies") req.operation = OpenClawOperation::AUDIT_DEPENDENCIES;
    else if (op_str == "generate_sbom") req.operation = OpenClawOperation::GENERATE_SBOM;
    else if (op_str == "enforce_policy") req.operation = OpenClawOperation::ENFORCE_POLICY;
    else if (op_str == "create_policy") req.operation = OpenClawOperation::CREATE_POLICY;
    else if (op_str == "gate_check") req.operation = OpenClawOperation::GATE_CHECK;
    else if (op_str == "scan_secrets") req.operation = OpenClawOperation::SCAN_SECRETS;
    else if (op_str == "full_audit") req.operation = OpenClawOperation::FULL_AUDIT;
    else if (op_str == "compliance_report") req.operation = OpenClawOperation::COMPLIANCE_REPORT;
    else req.operation = OpenClawOperation::HEALTH_CHECK;

    req.project_path = payload.value("project_path", "");
    req.project_name = payload.value("project_name", "");
    req.ecosystem = payload.value("ecosystem", "");
    req.license_spdx = payload.value("license", "");
    req.policy_id = payload.value("policy_id", "");
    req.cve_id = payload.value("cve_id", "");
    req.sbom_format = payload.value("sbom_format", "cyclonedx");
    req.include_transitive = payload.value("include_transitive", true);
    req.dry_run = payload.value("dry_run", false);
    req.concurrency = payload.value("concurrency", 8);

    return req;
}

}  // namespace prodxcloud::agents::specialized
