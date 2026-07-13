/// @file security_engine.cpp
/// @brief Implementation of the Zero Trust Security Engine.
///
/// Covers: secrets vault, RBAC, network policies, policy-as-code,
/// vulnerability management, compliance frameworks, audit trail,
/// and security posture scoring.

#include "platform/security/security_engine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "common/uuid.hpp"

using json = nlohmann::json;

namespace prodxcloud::platform::security {

using prodxcloud::generate_uuid;
using prodxcloud::now_iso8601;
using prodxcloud::Error;

// ─── Helpers ────────────────────────────────────────────────────────────────

static const std::string k_xor_key = "prodxcloud-security-engine-key-v1";  // placeholder key

static std::string base64_encode(const std::string& input) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

static std::string base64_decode(const std::string& input) {
    static constexpr int table[] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    std::string out;
    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        if (c >= 128 || table[c] == -1) continue;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// ─── Constructor ────────────────────────────────────────────────────────────

SecurityEngine::SecurityEngine() {
    spdlog::info("[SecurityEngine] Initializing Zero Trust Security Engine");
    populate_default_roles();
    populate_compliance_frameworks();
    spdlog::info("[SecurityEngine] Initialized with {} roles, {} compliance frameworks",
                 roles_.size(), compliance_controls_.size());
}

// ─── Encrypt / Decrypt (XOR + Base64 placeholder) ──────────────────────────

std::string SecurityEngine::encrypt_value(const std::string& plaintext) {
    // NOTE: Production would use AES-256-GCM via OpenSSL/libsodium.
    // This is a simple XOR + base64 placeholder for development/testing.
    std::string xored;
    xored.reserve(plaintext.size());
    for (size_t i = 0; i < plaintext.size(); ++i) {
        xored.push_back(
            static_cast<char>(plaintext[i] ^ k_xor_key[i % k_xor_key.size()]));
    }
    return base64_encode(xored);
}

std::string SecurityEngine::decrypt_value(const std::string& ciphertext) {
    std::string xored = base64_decode(ciphertext);
    std::string plain;
    plain.reserve(xored.size());
    for (size_t i = 0; i < xored.size(); ++i) {
        plain.push_back(
            static_cast<char>(xored[i] ^ k_xor_key[i % k_xor_key.size()]));
    }
    return plain;
}

// ─── Secrets Vault ──────────────────────────────────────────────────────────

Result<Secret> SecurityEngine::create_secret(const std::string& tenant_id,
                                              const std::string& name,
                                              const std::string& value,
                                              const std::string& type,
                                              const std::string& rotation_policy) {
    if (name.empty()) {
        return std::unexpected(Error::bad_request("Secret name must not be empty"));
    }
    if (value.empty()) {
        return std::unexpected(Error::bad_request("Secret value must not be empty"));
    }

    std::unique_lock lock(mutex_);

    // Check for duplicate name within tenant
    for (const auto& [_, s] : secrets_) {
        if (s.tenant_id == tenant_id && s.name == name) {
            return std::unexpected(Error::bad_request(
                "Secret '" + name + "' already exists in tenant " + tenant_id));
        }
    }

    Secret secret;
    secret.id              = generate_uuid();
    secret.name            = name;
    secret.tenant_id       = tenant_id;
    secret.path            = "/secrets/" + tenant_id + "/" + name;
    secret.type            = type;
    secret.encrypted_value = encrypt_value(value);
    secret.version         = 1;
    secret.rotation_policy = rotation_policy;
    secret.last_rotated_at = now_iso8601();
    secret.created_by      = "system";
    secret.created_at      = now_iso8601();
    secret.updated_at      = secret.created_at;

    // Compute expiration based on rotation policy
    // (simplified: just store the timestamp as a note)
    if (rotation_policy != "never") {
        secret.expires_at = "auto:" + rotation_policy;
    }

    const std::string id = secret.id;
    secrets_.emplace(id, secret);

    spdlog::info("[SecurityEngine] Created secret '{}' (id={}) for tenant {}",
                 name, id, tenant_id);
    return secret;
}

Result<std::string> SecurityEngine::get_secret_value(const std::string& secret_id,
                                                      const std::string& accessor) {
    std::shared_lock lock(mutex_);

    auto it = secrets_.find(secret_id);
    if (it == secrets_.end()) {
        SecretAccess log_entry;
        log_entry.secret_id  = secret_id;
        log_entry.accessor   = accessor;
        log_entry.action     = "read";
        log_entry.allowed    = false;
        log_entry.timestamp  = now_iso8601();
        // need unique_lock to write to access log
        lock.unlock();
        std::unique_lock wlock(mutex_);
        secret_access_log_.push_back(std::move(log_entry));
        return std::unexpected(Error::not_found("Secret not found: " + secret_id));
    }

    const auto& secret = it->second;

    // Check allowed services if any are configured
    if (!secret.allowed_services.empty()) {
        bool found = std::find(secret.allowed_services.begin(),
                               secret.allowed_services.end(),
                               accessor) != secret.allowed_services.end();
        if (!found) {
            lock.unlock();
            std::unique_lock wlock(mutex_);
            SecretAccess log_entry;
            log_entry.secret_id = secret_id;
            log_entry.accessor  = accessor;
            log_entry.action    = "read";
            log_entry.allowed   = false;
            log_entry.timestamp = now_iso8601();
            secret_access_log_.push_back(std::move(log_entry));
            return std::unexpected(Error::unauthorized(
                "Accessor '" + accessor + "' not in allowed services for secret " + secret_id));
        }
    }

    std::string decrypted = decrypt_value(secret.encrypted_value);

    // Log successful access
    lock.unlock();
    std::unique_lock wlock(mutex_);
    SecretAccess log_entry;
    log_entry.secret_id = secret_id;
    log_entry.accessor  = accessor;
    log_entry.action    = "read";
    log_entry.allowed   = true;
    log_entry.timestamp = now_iso8601();
    secret_access_log_.push_back(std::move(log_entry));

    spdlog::debug("[SecurityEngine] Secret {} accessed by {}", secret_id, accessor);
    return decrypted;
}

Result<Secret> SecurityEngine::rotate_secret(const std::string& secret_id,
                                              const std::string& new_value) {
    if (new_value.empty()) {
        return std::unexpected(Error::bad_request("New secret value must not be empty"));
    }

    std::unique_lock lock(mutex_);

    auto it = secrets_.find(secret_id);
    if (it == secrets_.end()) {
        return std::unexpected(Error::not_found("Secret not found: " + secret_id));
    }

    auto& secret          = it->second;
    secret.encrypted_value = encrypt_value(new_value);
    secret.version        += 1;
    secret.last_rotated_at = now_iso8601();
    secret.updated_at      = now_iso8601();

    SecretAccess log_entry;
    log_entry.secret_id = secret_id;
    log_entry.accessor  = "system";
    log_entry.action    = "rotate";
    log_entry.allowed   = true;
    log_entry.timestamp = now_iso8601();
    secret_access_log_.push_back(std::move(log_entry));

    spdlog::info("[SecurityEngine] Rotated secret {} to version {}", secret_id, secret.version);
    return secret;
}

Result<void> SecurityEngine::delete_secret(const std::string& secret_id) {
    std::unique_lock lock(mutex_);

    auto it = secrets_.find(secret_id);
    if (it == secrets_.end()) {
        return std::unexpected(Error::not_found("Secret not found: " + secret_id));
    }

    SecretAccess log_entry;
    log_entry.secret_id = secret_id;
    log_entry.accessor  = "system";
    log_entry.action    = "delete";
    log_entry.allowed   = true;
    log_entry.timestamp = now_iso8601();
    secret_access_log_.push_back(std::move(log_entry));

    spdlog::info("[SecurityEngine] Deleted secret {} ('{}')", secret_id, it->second.name);
    secrets_.erase(it);
    return {};
}

std::vector<Secret> SecurityEngine::list_secrets(const std::string& tenant_id) const {
    std::shared_lock lock(mutex_);
    std::vector<Secret> result;
    for (const auto& [_, s] : secrets_) {
        if (s.tenant_id == tenant_id) {
            Secret safe_copy = s;
            safe_copy.encrypted_value = "***REDACTED***";
            result.push_back(std::move(safe_copy));
        }
    }
    return result;
}

Result<std::vector<Secret>> SecurityEngine::find_expiring_secrets(int32_t within_days) const {
    std::shared_lock lock(mutex_);
    std::vector<Secret> expiring;

    for (const auto& [_, s] : secrets_) {
        // Check rotation policy to see if secret needs rotation
        bool needs_attention = false;
        if (s.rotation_policy == "30d" && within_days >= 30) {
            needs_attention = true;
        } else if (s.rotation_policy == "60d" && within_days >= 30) {
            needs_attention = true;
        } else if (s.rotation_policy == "90d" && within_days >= 60) {
            needs_attention = true;
        }

        // Also flag if expires_at is set and non-empty
        if (!s.expires_at.empty() && s.expires_at != "never") {
            needs_attention = true;
        }

        if (needs_attention) {
            Secret safe_copy = s;
            safe_copy.encrypted_value = "***REDACTED***";
            expiring.push_back(std::move(safe_copy));
        }
    }

    spdlog::debug("[SecurityEngine] Found {} expiring secrets within {} days",
                  expiring.size(), within_days);
    return expiring;
}

std::vector<SecretAccess> SecurityEngine::get_access_log(const std::string& secret_id) const {
    std::shared_lock lock(mutex_);
    std::vector<SecretAccess> result;
    for (const auto& entry : secret_access_log_) {
        if (entry.secret_id == secret_id) {
            result.push_back(entry);
        }
    }
    return result;
}

// ─── RBAC ───────────────────────────────────────────────────────────────────

void SecurityEngine::populate_default_roles() {
    auto make_role = [&](const std::string& name,
                         const std::string& description,
                         const std::vector<std::string>& permissions,
                         const std::string& scope) {
        Role role;
        role.id          = generate_uuid();
        role.name        = name;
        role.description = description;
        role.permissions = permissions;
        role.scope       = scope;
        role.is_system   = true;
        role.created_at  = now_iso8601();
        roles_.emplace(role.id, role);
    };

    // admin: full access to everything
    make_role("admin",
              "Full administrative access to all resources and operations",
              {"*:*",
               "deployments:create", "deployments:read", "deployments:update", "deployments:delete",
               "models:create", "models:read", "models:update", "models:delete",
               "secrets:create", "secrets:read", "secrets:update", "secrets:delete", "secrets:rotate",
               "policies:create", "policies:read", "policies:update", "policies:delete",
               "roles:create", "roles:read", "roles:update", "roles:delete",
               "network:create", "network:read", "network:update", "network:delete",
               "compliance:read", "compliance:manage",
               "audit:read",
               "tenants:create", "tenants:read", "tenants:update", "tenants:delete",
               "pipelines:create", "pipelines:read", "pipelines:update", "pipelines:delete",
               "agents:create", "agents:read", "agents:update", "agents:delete"},
              "global");

    // editor: create and modify resources, but not manage security or roles
    make_role("editor",
              "Create and modify resources, read security policies, no role management",
              {"deployments:create", "deployments:read", "deployments:update",
               "models:create", "models:read", "models:update",
               "pipelines:create", "pipelines:read", "pipelines:update",
               "agents:create", "agents:read", "agents:update",
               "secrets:read",
               "policies:read",
               "compliance:read",
               "audit:read",
               "tenants:read"},
              "tenant");

    // viewer: read-only access
    make_role("viewer",
              "Read-only access to resources, deployments, and compliance reports",
              {"deployments:read",
               "models:read",
               "pipelines:read",
               "agents:read",
               "secrets:read",
               "policies:read",
               "compliance:read",
               "audit:read",
               "tenants:read",
               "network:read",
               "roles:read"},
              "tenant");

    // deployer: deploy and manage deployments and pipelines
    make_role("deployer",
              "Deploy, manage, and monitor deployments and CI/CD pipelines",
              {"deployments:create", "deployments:read", "deployments:update", "deployments:delete",
               "pipelines:create", "pipelines:read", "pipelines:update", "pipelines:delete",
               "models:read",
               "agents:read",
               "secrets:read",
               "policies:read",
               "compliance:read",
               "network:read"},
              "tenant");

    // security_admin: manage security policies, secrets, RBAC, compliance
    make_role("security_admin",
              "Manage security policies, secrets, roles, network policies, and compliance",
              {"secrets:create", "secrets:read", "secrets:update", "secrets:delete", "secrets:rotate",
               "policies:create", "policies:read", "policies:update", "policies:delete",
               "roles:create", "roles:read", "roles:update", "roles:delete",
               "network:create", "network:read", "network:update", "network:delete",
               "compliance:read", "compliance:manage",
               "audit:read",
               "tenants:read",
               "deployments:read",
               "models:read",
               "pipelines:read"},
              "global");

    spdlog::info("[SecurityEngine] Populated {} default system roles", roles_.size());
}

Result<Role> SecurityEngine::create_role(const std::string& name,
                                          const std::vector<std::string>& permissions,
                                          const std::string& scope) {
    if (name.empty()) {
        return std::unexpected(Error::bad_request("Role name must not be empty"));
    }
    if (permissions.empty()) {
        return std::unexpected(Error::bad_request("Role must have at least one permission"));
    }

    std::unique_lock lock(mutex_);

    // Check for duplicate name
    for (const auto& [_, r] : roles_) {
        if (r.name == name) {
            return std::unexpected(Error::bad_request("Role '" + name + "' already exists"));
        }
    }

    Role role;
    role.id          = generate_uuid();
    role.name        = name;
    role.description = "Custom role: " + name;
    role.permissions = permissions;
    role.scope       = scope;
    role.is_system   = false;
    role.created_at  = now_iso8601();

    const std::string id = role.id;
    roles_.emplace(id, role);

    spdlog::info("[SecurityEngine] Created role '{}' (id={}) with {} permissions",
                 name, id, permissions.size());
    return role;
}

Result<Role> SecurityEngine::get_role(const std::string& role_id) const {
    std::shared_lock lock(mutex_);
    auto it = roles_.find(role_id);
    if (it == roles_.end()) {
        return std::unexpected(Error::not_found("Role not found: " + role_id));
    }
    return it->second;
}

std::vector<Role> SecurityEngine::list_roles() const {
    std::shared_lock lock(mutex_);
    std::vector<Role> result;
    result.reserve(roles_.size());
    for (const auto& [_, r] : roles_) {
        result.push_back(r);
    }
    return result;
}

Result<void> SecurityEngine::delete_role(const std::string& role_id) {
    std::unique_lock lock(mutex_);
    auto it = roles_.find(role_id);
    if (it == roles_.end()) {
        return std::unexpected(Error::not_found("Role not found: " + role_id));
    }
    if (it->second.is_system) {
        return std::unexpected(Error::bad_request(
            "Cannot delete system role '" + it->second.name + "'"));
    }

    // Remove any bindings referencing this role
    role_bindings_.erase(
        std::remove_if(role_bindings_.begin(), role_bindings_.end(),
                        [&](const RoleBinding& b) { return b.role_id == role_id; }),
        role_bindings_.end());

    spdlog::info("[SecurityEngine] Deleted role '{}' (id={})", it->second.name, role_id);
    roles_.erase(it);
    return {};
}

Result<RoleBinding> SecurityEngine::bind_role(const std::string& principal,
                                               const std::string& principal_type,
                                               const std::string& role_id,
                                               const std::string& scope) {
    if (principal.empty()) {
        return std::unexpected(Error::bad_request("Principal must not be empty"));
    }

    std::unique_lock lock(mutex_);

    // Verify role exists
    if (roles_.find(role_id) == roles_.end()) {
        return std::unexpected(Error::not_found("Role not found: " + role_id));
    }

    // Check for duplicate binding
    for (const auto& b : role_bindings_) {
        if (b.principal == principal && b.role_id == role_id && b.scope == scope) {
            return std::unexpected(Error::bad_request(
                "Binding already exists for principal '" + principal +
                "' with role " + role_id + " in scope " + scope));
        }
    }

    RoleBinding binding;
    binding.id             = generate_uuid();
    binding.principal      = principal;
    binding.principal_type = principal_type;
    binding.role_id        = role_id;
    binding.scope          = scope;
    binding.created_at     = now_iso8601();

    role_bindings_.push_back(binding);

    spdlog::info("[SecurityEngine] Bound principal '{}' ({}) to role {} in scope '{}'",
                 principal, principal_type, role_id, scope);
    return binding;
}

Result<void> SecurityEngine::unbind_role(const std::string& binding_id) {
    std::unique_lock lock(mutex_);

    auto it = std::find_if(role_bindings_.begin(), role_bindings_.end(),
                            [&](const RoleBinding& b) { return b.id == binding_id; });
    if (it == role_bindings_.end()) {
        return std::unexpected(Error::not_found("Role binding not found: " + binding_id));
    }

    spdlog::info("[SecurityEngine] Unbound role binding {} (principal='{}')",
                 binding_id, it->principal);
    role_bindings_.erase(it);
    return {};
}

Result<AccessDecision> SecurityEngine::check_access(const std::string& principal,
                                                     const std::string& resource,
                                                     const std::string& action) {
    std::shared_lock lock(mutex_);

    AccessDecision decision;
    decision.principal    = principal;
    decision.resource     = resource;
    decision.action       = action;
    decision.evaluated_at = now_iso8601();

    // Construct the required permission string: "resource:action"
    std::string required_perm = resource + ":" + action;

    // Iterate role bindings for this principal
    for (const auto& binding : role_bindings_) {
        if (binding.principal != principal) {
            continue;
        }

        // Look up the bound role
        auto role_it = roles_.find(binding.role_id);
        if (role_it == roles_.end()) {
            continue;
        }

        const auto& role = role_it->second;

        // Check if any permission on this role matches
        for (const auto& perm : role.permissions) {
            // Wildcard match: "*:*" grants everything
            if (perm == "*:*") {
                decision.allowed      = true;
                decision.matched_role = role.name;
                decision.reason       = "Granted via wildcard permission on role '" +
                                        role.name + "'";
                spdlog::debug("[SecurityEngine] Access GRANTED: {} -> {} ({}) via role '{}'",
                              principal, resource, action, role.name);
                return decision;
            }

            // Exact match
            if (perm == required_perm) {
                decision.allowed      = true;
                decision.matched_role = role.name;
                decision.reason       = "Granted via permission '" + perm +
                                        "' on role '" + role.name + "'";
                spdlog::debug("[SecurityEngine] Access GRANTED: {} -> {} ({}) via role '{}'",
                              principal, resource, action, role.name);
                return decision;
            }

            // Resource wildcard: "resource:*" matches any action on resource
            std::string resource_wildcard = resource + ":*";
            if (perm == resource_wildcard) {
                decision.allowed      = true;
                decision.matched_role = role.name;
                decision.reason       = "Granted via resource wildcard '" + perm +
                                        "' on role '" + role.name + "'";
                spdlog::debug("[SecurityEngine] Access GRANTED: {} -> {} ({}) via role '{}'",
                              principal, resource, action, role.name);
                return decision;
            }
        }
    }

    // Default deny (zero-trust)
    decision.allowed = false;
    decision.reason  = "No matching role binding grants permission '" +
                       required_perm + "' for principal '" + principal + "'";
    spdlog::warn("[SecurityEngine] Access DENIED: {} -> {} ({})", principal, resource, action);
    return decision;
}

std::vector<RoleBinding> SecurityEngine::list_bindings(const std::string& principal) const {
    std::shared_lock lock(mutex_);
    if (principal.empty()) {
        return role_bindings_;
    }
    std::vector<RoleBinding> result;
    for (const auto& b : role_bindings_) {
        if (b.principal == principal) {
            result.push_back(b);
        }
    }
    return result;
}

// ─── Network Policies (Zero Trust) ─────────────────────────────────────────

Result<NetworkPolicy> SecurityEngine::create_network_policy(
    const std::string& tenant_id,
    const std::string& name,
    const std::string& direction,
    const std::vector<std::string>& allow_from,
    const std::vector<int32_t>& allow_ports,
    bool default_deny) {

    if (name.empty()) {
        return std::unexpected(Error::bad_request("Network policy name must not be empty"));
    }
    if (direction != "ingress" && direction != "egress" && direction != "both") {
        return std::unexpected(Error::bad_request(
            "Direction must be 'ingress', 'egress', or 'both'"));
    }

    std::unique_lock lock(mutex_);

    NetworkPolicy policy;
    policy.id            = generate_uuid();
    policy.name          = name;
    policy.tenant_id     = tenant_id;
    policy.direction     = direction;
    policy.allow_from    = allow_from;
    policy.allow_ports   = allow_ports;
    policy.default_deny  = default_deny;  // zero-trust: deny by default
    policy.enabled       = true;
    policy.protocol      = "tcp";
    policy.created_at    = now_iso8601();

    const std::string id = policy.id;
    network_policies_.emplace(id, policy);

    spdlog::info("[SecurityEngine] Created network policy '{}' (id={}) direction={} "
                 "default_deny={} allow_from={} ports={}",
                 name, id, direction, default_deny, allow_from.size(), allow_ports.size());
    return policy;
}

Result<NetworkPolicy> SecurityEngine::get_network_policy(const std::string& policy_id) const {
    std::shared_lock lock(mutex_);
    auto it = network_policies_.find(policy_id);
    if (it == network_policies_.end()) {
        return std::unexpected(Error::not_found("Network policy not found: " + policy_id));
    }
    return it->second;
}

std::vector<NetworkPolicy> SecurityEngine::list_network_policies(
    const std::string& tenant_id) const {
    std::shared_lock lock(mutex_);
    std::vector<NetworkPolicy> result;
    for (const auto& [_, p] : network_policies_) {
        if (p.tenant_id == tenant_id) {
            result.push_back(p);
        }
    }
    return result;
}

Result<void> SecurityEngine::delete_network_policy(const std::string& policy_id) {
    std::unique_lock lock(mutex_);
    auto it = network_policies_.find(policy_id);
    if (it == network_policies_.end()) {
        return std::unexpected(Error::not_found("Network policy not found: " + policy_id));
    }
    spdlog::info("[SecurityEngine] Deleted network policy '{}' (id={})",
                 it->second.name, policy_id);
    network_policies_.erase(it);
    return {};
}

// ─── Policy-as-Code ─────────────────────────────────────────────────────────

Result<SecurityPolicy> SecurityEngine::create_security_policy(
    const std::string& tenant_id,
    const std::string& name,
    const std::string& category,
    const std::string& policy_code,
    const std::string& engine,
    const std::string& enforcement) {

    if (name.empty()) {
        return std::unexpected(Error::bad_request("Policy name must not be empty"));
    }
    if (policy_code.empty()) {
        return std::unexpected(Error::bad_request("Policy code must not be empty"));
    }

    std::unique_lock lock(mutex_);

    SecurityPolicy policy;
    policy.id               = generate_uuid();
    policy.name             = name;
    policy.description      = "Security policy: " + name;
    policy.tenant_id        = tenant_id;
    policy.category         = category;
    policy.engine           = engine;
    policy.policy_code      = policy_code;
    policy.enforcement      = enforcement;
    policy.enabled          = true;
    policy.violation_count  = 0;
    policy.created_at       = now_iso8601();

    const std::string id = policy.id;
    security_policies_.emplace(id, policy);

    spdlog::info("[SecurityEngine] Created security policy '{}' (id={}) category={} engine={}",
                 name, id, category, engine);
    return policy;
}

Result<SecurityPolicy> SecurityEngine::get_security_policy(const std::string& policy_id) const {
    std::shared_lock lock(mutex_);
    auto it = security_policies_.find(policy_id);
    if (it == security_policies_.end()) {
        return std::unexpected(Error::not_found("Security policy not found: " + policy_id));
    }
    return it->second;
}

std::vector<SecurityPolicy> SecurityEngine::list_security_policies(
    const std::string& tenant_id) const {
    std::shared_lock lock(mutex_);
    std::vector<SecurityPolicy> result;
    for (const auto& [_, p] : security_policies_) {
        if (p.tenant_id == tenant_id) {
            result.push_back(p);
        }
    }
    return result;
}

Result<std::vector<PolicyViolation>> SecurityEngine::evaluate_policy(
    const std::string& policy_id) {
    std::unique_lock lock(mutex_);

    auto it = security_policies_.find(policy_id);
    if (it == security_policies_.end()) {
        return std::unexpected(Error::not_found("Security policy not found: " + policy_id));
    }

    auto& policy = it->second;
    policy.last_evaluated_at = now_iso8601();

    // Simulated policy evaluation: in production this would call OPA/Kyverno/etc.
    // For now we generate a sample violation if the policy is in enforce mode.
    std::vector<PolicyViolation> new_violations;

    if (policy.enforcement == "enforce" || policy.enforcement == "audit") {
        PolicyViolation violation;
        violation.id            = generate_uuid();
        violation.policy_id     = policy_id;
        violation.policy_name   = policy.name;
        violation.resource      = "simulated-resource-" + generate_uuid().substr(0, 8);
        violation.resource_type = policy.category;
        violation.violation     = "Policy '" + policy.name + "' evaluation detected non-compliance";
        violation.severity      = (policy.enforcement == "enforce") ? "high" : "medium";
        violation.remediation   = "Review resource configuration against policy requirements";
        violation.resolved      = false;
        violation.detected_at   = now_iso8601();

        violations_.push_back(violation);
        new_violations.push_back(violation);
        policy.violation_count += 1;
    }

    spdlog::info("[SecurityEngine] Evaluated policy '{}' (id={}) - {} violations found",
                 policy.name, policy_id, new_violations.size());
    return new_violations;
}

Result<std::vector<PolicyViolation>> SecurityEngine::evaluate_all_policies(
    const std::string& tenant_id) {
    // Collect policy IDs for this tenant first (to avoid holding lock during evaluate)
    std::vector<std::string> policy_ids;
    {
        std::shared_lock lock(mutex_);
        for (const auto& [id, p] : security_policies_) {
            if (p.tenant_id == tenant_id && p.enabled) {
                policy_ids.push_back(id);
            }
        }
    }

    std::vector<PolicyViolation> all_violations;
    for (const auto& pid : policy_ids) {
        auto result = evaluate_policy(pid);
        if (result.has_value()) {
            for (auto& v : result.value()) {
                all_violations.push_back(std::move(v));
            }
        }
    }

    spdlog::info("[SecurityEngine] Evaluated all {} policies for tenant {} - {} total violations",
                 policy_ids.size(), tenant_id, all_violations.size());
    return all_violations;
}

std::vector<PolicyViolation> SecurityEngine::list_violations(
    const std::string& tenant_id, bool unresolved_only) const {
    std::shared_lock lock(mutex_);
    std::vector<PolicyViolation> result;

    for (const auto& v : violations_) {
        // Match violations by looking up the policy's tenant
        auto pol_it = security_policies_.find(v.policy_id);
        if (pol_it == security_policies_.end()) continue;
        if (pol_it->second.tenant_id != tenant_id) continue;

        if (unresolved_only && v.resolved) continue;
        result.push_back(v);
    }
    return result;
}

Result<void> SecurityEngine::resolve_violation(const std::string& violation_id) {
    std::unique_lock lock(mutex_);

    for (auto& v : violations_) {
        if (v.id == violation_id) {
            if (v.resolved) {
                return std::unexpected(Error::bad_request(
                    "Violation " + violation_id + " is already resolved"));
            }
            v.resolved    = true;
            v.resolved_at = now_iso8601();
            spdlog::info("[SecurityEngine] Resolved violation {}", violation_id);
            return {};
        }
    }
    return std::unexpected(Error::not_found("Violation not found: " + violation_id));
}

// ─── Vulnerability Management ───────────────────────────────────────────────

Result<SecurityVulnerability> SecurityEngine::report_vulnerability(
    const std::string& service,
    const std::string& cve_id,
    const std::string& severity,
    double cvss_score) {

    if (service.empty()) {
        return std::unexpected(Error::bad_request("Service name must not be empty"));
    }
    if (severity != "low" && severity != "medium" && severity != "high" && severity != "critical") {
        return std::unexpected(Error::bad_request(
            "Severity must be 'low', 'medium', 'high', or 'critical'"));
    }
    if (cvss_score < 0.0 || cvss_score > 10.0) {
        return std::unexpected(Error::bad_request("CVSS score must be between 0.0 and 10.0"));
    }

    std::unique_lock lock(mutex_);

    SecurityVulnerability vuln;
    vuln.id            = generate_uuid();
    vuln.cve_id        = cve_id;
    vuln.service       = service;
    vuln.component     = service;
    vuln.severity      = severity;
    vuln.cvss_score    = cvss_score;
    vuln.description   = "Vulnerability " + cve_id + " in " + service;
    vuln.exploitable   = (cvss_score >= 7.0);
    vuln.status        = "open";
    vuln.discovered_at = now_iso8601();

    vulnerabilities_.push_back(vuln);

    spdlog::info("[SecurityEngine] Reported vulnerability {} (CVE={}) in service '{}' "
                 "severity={} cvss={}",
                 vuln.id, cve_id, service, severity, cvss_score);
    return vuln;
}

std::vector<SecurityVulnerability> SecurityEngine::list_vulnerabilities(
    const std::string& service, const std::string& status) const {
    std::shared_lock lock(mutex_);
    std::vector<SecurityVulnerability> result;

    for (const auto& v : vulnerabilities_) {
        if (!service.empty() && v.service != service) continue;
        if (!status.empty() && v.status != status) continue;
        result.push_back(v);
    }
    return result;
}

Result<void> SecurityEngine::update_vulnerability_status(const std::string& vuln_id,
                                                          const std::string& status) {
    if (status != "open" && status != "in_progress" && status != "mitigated" &&
        status != "resolved" && status != "accepted") {
        return std::unexpected(Error::bad_request(
            "Status must be 'open', 'in_progress', 'mitigated', 'resolved', or 'accepted'"));
    }

    std::unique_lock lock(mutex_);

    for (auto& v : vulnerabilities_) {
        if (v.id == vuln_id) {
            v.status = status;
            if (status == "resolved" || status == "mitigated") {
                v.resolved_at = now_iso8601();
            }
            spdlog::info("[SecurityEngine] Updated vulnerability {} status to '{}'",
                         vuln_id, status);
            return {};
        }
    }
    return std::unexpected(Error::not_found("Vulnerability not found: " + vuln_id));
}

Result<void> SecurityEngine::scan_pipeline(const std::string& pipeline_id) {
    if (pipeline_id.empty()) {
        return std::unexpected(Error::bad_request("Pipeline ID must not be empty"));
    }

    // Simulated pipeline security scan
    spdlog::info("[SecurityEngine] Scanning pipeline {} for vulnerabilities", pipeline_id);

    // In production this would invoke container scanning, SAST/DAST, dependency checks, etc.
    // For now we log the scan and report a simulated finding.
    auto result = report_vulnerability(
        "pipeline:" + pipeline_id,
        "CVE-0000-0000",
        "low",
        2.5);

    if (!result.has_value()) {
        return std::unexpected(result.error());
    }

    spdlog::info("[SecurityEngine] Pipeline {} scan complete", pipeline_id);
    return {};
}

// ─── Compliance Frameworks ──────────────────────────────────────────────────

void SecurityEngine::populate_compliance_frameworks() {
    compliance_controls_["soc2"]     = generate_soc2_controls();
    compliance_controls_["iso27001"] = generate_iso27001_controls();
    compliance_controls_["iso28743"] = generate_iso28743_controls();
    compliance_controls_["hipaa"]    = generate_hipaa_controls();

    spdlog::info("[SecurityEngine] Populated compliance frameworks: "
                 "SOC2={} controls, ISO27001={} controls, ISO28743={} controls, HIPAA={} controls",
                 compliance_controls_["soc2"].size(),
                 compliance_controls_["iso27001"].size(),
                 compliance_controls_["iso28743"].size(),
                 compliance_controls_["hipaa"].size());
}

std::vector<ComplianceControl> SecurityEngine::generate_soc2_controls() {
    std::vector<ComplianceControl> controls;

    auto add = [&](const std::string& control_id, const std::string& title,
                   const std::string& description, const std::string& category) {
        ComplianceControl c;
        c.id                = generate_uuid();
        c.framework         = "soc2";
        c.control_id        = control_id;
        c.title             = title;
        c.description       = description;
        c.category          = category;
        c.status            = "partial";
        c.compliance_percent = 50.0;
        c.last_assessed_at  = now_iso8601();
        c.next_review_at    = "2026-06-30T00:00:00Z";
        controls.push_back(std::move(c));
    };

    // CC1 - Control Environment (Common Criteria)
    add("CC1.1", "COSO Principle 1: Integrity and Ethical Values",
        "The entity demonstrates a commitment to integrity and ethical values",
        "control_environment");
    add("CC1.2", "COSO Principle 2: Board Oversight",
        "The board of directors demonstrates independence and exercises oversight",
        "control_environment");
    add("CC1.3", "COSO Principle 3: Management Structure",
        "Management establishes structures, reporting lines, and responsibilities",
        "control_environment");

    // CC2 - Communication and Information
    add("CC2.1", "COSO Principle 13: Relevant Information",
        "The entity obtains and uses relevant quality information to support internal controls",
        "communication");
    add("CC2.2", "COSO Principle 14: Internal Communication",
        "The entity internally communicates information necessary for internal controls",
        "communication");

    // CC3 - Risk Assessment
    add("CC3.1", "COSO Principle 6: Risk Assessment Objectives",
        "The entity specifies objectives to identify and assess risks",
        "risk_assessment");
    add("CC3.2", "COSO Principle 7: Fraud Risk",
        "The entity identifies and assesses fraud risk",
        "risk_assessment");

    // CC4 - Monitoring Activities
    add("CC4.1", "COSO Principle 16: Ongoing Monitoring",
        "The entity selects and performs ongoing evaluations of internal controls",
        "monitoring");

    // CC5 - Control Activities
    add("CC5.1", "COSO Principle 10: Control Activities for Risk Mitigation",
        "The entity selects and develops control activities that mitigate risks",
        "control_activities");

    // CC6 - Logical and Physical Access Controls
    add("CC6.1", "Logical Access Security",
        "Logical access security controls are implemented to protect against unauthorized access",
        "access_control");
    add("CC6.2", "System Credentials and Authentication",
        "System credentials are created, maintained, and deprovisioned appropriately",
        "access_control");
    add("CC6.3", "Access Authorization and Modification",
        "Access to data, software, functions, and infrastructure is authorized and modified",
        "access_control");

    // CC7 - System Operations
    add("CC7.1", "Vulnerability Management",
        "Infrastructure and software are monitored for vulnerabilities and anomalies",
        "operations");

    // CC8 - Change Management
    add("CC8.1", "Change Management Process",
        "Changes to infrastructure and software are authorized, designed, developed, and implemented",
        "change_management");

    // CC9 - Risk Mitigation
    add("CC9.1", "Risk Mitigation Through Business Continuity",
        "The entity identifies and mitigates risks through business continuity planning",
        "risk_mitigation");

    return controls;
}

std::vector<ComplianceControl> SecurityEngine::generate_iso27001_controls() {
    std::vector<ComplianceControl> controls;

    auto add = [&](const std::string& control_id, const std::string& title,
                   const std::string& description, const std::string& category) {
        ComplianceControl c;
        c.id                = generate_uuid();
        c.framework         = "iso27001";
        c.control_id        = control_id;
        c.title             = title;
        c.description       = description;
        c.category          = category;
        c.status            = "partial";
        c.compliance_percent = 60.0;
        c.last_assessed_at  = now_iso8601();
        c.next_review_at    = "2026-06-30T00:00:00Z";
        controls.push_back(std::move(c));
    };

    // Annex A controls (ISO 27001:2022)
    add("A.5.1", "Policies for Information Security",
        "Information security policy and topic-specific policies shall be defined and approved",
        "organizational");
    add("A.5.2", "Information Security Roles and Responsibilities",
        "Information security roles and responsibilities shall be defined and allocated",
        "organizational");
    add("A.5.3", "Segregation of Duties",
        "Conflicting duties and areas of responsibility shall be segregated",
        "organizational");

    add("A.6.1", "Screening",
        "Background verification checks on candidates shall be carried out prior to joining",
        "people");
    add("A.6.2", "Terms and Conditions of Employment",
        "Employment agreements shall state responsibilities for information security",
        "people");

    add("A.7.1", "Physical Security Perimeters",
        "Security perimeters shall be defined to protect areas containing information",
        "physical");
    add("A.7.2", "Physical Entry Controls",
        "Secure areas shall be protected by appropriate entry controls",
        "physical");

    add("A.8.1", "User Endpoint Devices",
        "Information on user endpoint devices shall be protected",
        "technological");
    add("A.8.2", "Privileged Access Rights",
        "Allocation and use of privileged access rights shall be restricted and managed",
        "technological");
    add("A.8.3", "Information Access Restriction",
        "Access to information shall be restricted in accordance with access control policy",
        "technological");
    add("A.8.4", "Access to Source Code",
        "Read and write access to source code and development tools shall be managed",
        "technological");
    add("A.8.5", "Secure Authentication",
        "Secure authentication technologies and procedures shall be established",
        "technological");
    add("A.8.6", "Capacity Management",
        "Use of resources shall be monitored and adjusted for capacity requirements",
        "technological");
    add("A.8.7", "Protection Against Malware",
        "Protection against malware shall be implemented with user awareness",
        "technological");
    add("A.8.8", "Management of Technical Vulnerabilities",
        "Information about technical vulnerabilities shall be obtained and evaluated",
        "technological");

    return controls;
}

std::vector<ComplianceControl> SecurityEngine::generate_iso28743_controls() {
    std::vector<ComplianceControl> controls;

    auto add = [&](const std::string& control_id, const std::string& title,
                   const std::string& description, const std::string& category) {
        ComplianceControl c;
        c.id                = generate_uuid();
        c.framework         = "iso28743";
        c.control_id        = control_id;
        c.title             = title;
        c.description       = description;
        c.category          = category;
        c.status            = "partial";
        c.compliance_percent = 55.0;
        c.last_assessed_at  = now_iso8601();
        c.next_review_at    = "2026-06-30T00:00:00Z";
        controls.push_back(std::move(c));
    };

    // ISO 28743 - Safety and security management of AI systems
    add("AI.4.1", "AI System Governance Framework",
        "Establish governance structures for oversight of AI system development and deployment",
        "governance");
    add("AI.4.2", "Risk Management for AI Systems",
        "Identify, assess, and treat risks specific to AI system lifecycle",
        "governance");
    add("AI.4.3", "Accountability and Transparency",
        "Ensure clear accountability structures and transparency in AI decision-making",
        "governance");

    add("AI.5.1", "Data Quality and Integrity",
        "Ensure training and operational data meets quality, integrity, and provenance requirements",
        "data_management");
    add("AI.5.2", "Data Privacy and Protection",
        "Protect personal and sensitive data used in AI systems according to applicable laws",
        "data_management");
    add("AI.5.3", "Data Lineage and Traceability",
        "Maintain complete data lineage records for all datasets used in AI training and inference",
        "data_management");

    add("AI.6.1", "Model Validation and Testing",
        "Validate AI models for accuracy, fairness, robustness, and safety before deployment",
        "model_lifecycle");
    add("AI.6.2", "Adversarial Robustness",
        "Assess and mitigate adversarial attack vectors against AI models",
        "model_lifecycle");
    add("AI.6.3", "Model Monitoring and Drift Detection",
        "Continuously monitor deployed models for performance degradation and concept drift",
        "model_lifecycle");

    add("AI.7.1", "Explainability Requirements",
        "AI systems shall provide explanations proportionate to the risk level of decisions",
        "transparency");
    add("AI.7.2", "Bias Detection and Mitigation",
        "Implement systematic bias detection and mitigation throughout the AI lifecycle",
        "transparency");

    add("AI.8.1", "Incident Response for AI Systems",
        "Establish incident response procedures specific to AI system failures and misuse",
        "operations");
    add("AI.8.2", "Secure AI Deployment Pipelines",
        "Ensure CI/CD pipelines for AI models enforce security scanning and approval gates",
        "operations");
    add("AI.8.3", "Human Oversight Controls",
        "Maintain human-in-the-loop mechanisms for high-risk AI system decisions",
        "operations");

    return controls;
}

std::vector<ComplianceControl> SecurityEngine::generate_hipaa_controls() {
    std::vector<ComplianceControl> controls;

    auto add = [&](const std::string& control_id, const std::string& title,
                   const std::string& description, const std::string& category) {
        ComplianceControl c;
        c.id                = generate_uuid();
        c.framework         = "hipaa";
        c.control_id        = control_id;
        c.title             = title;
        c.description       = description;
        c.category          = category;
        c.status            = "partial";
        c.compliance_percent = 45.0;
        c.last_assessed_at  = now_iso8601();
        c.next_review_at    = "2026-06-30T00:00:00Z";
        controls.push_back(std::move(c));
    };

    // HIPAA Security Rule - Administrative Safeguards (164.308)
    add("164.308(a)(1)", "Security Management Process",
        "Implement policies and procedures to prevent, detect, contain, and correct security violations",
        "administrative");
    add("164.308(a)(2)", "Assigned Security Responsibility",
        "Identify the security official responsible for developing and implementing security policies",
        "administrative");
    add("164.308(a)(3)", "Workforce Security",
        "Implement policies to ensure workforce members have appropriate access to ePHI",
        "administrative");
    add("164.308(a)(4)", "Information Access Management",
        "Implement policies for authorizing access to ePHI consistent with the Privacy Rule",
        "administrative");
    add("164.308(a)(5)", "Security Awareness and Training",
        "Implement security awareness and training program for all workforce members",
        "administrative");

    // HIPAA Security Rule - Physical Safeguards (164.310)
    add("164.310(a)(1)", "Facility Access Controls",
        "Implement policies to limit physical access to electronic information systems",
        "physical");
    add("164.310(b)", "Workstation Use",
        "Implement policies for proper workstation use and access to ePHI",
        "physical");
    add("164.310(c)", "Workstation Security",
        "Implement physical safeguards for all workstations that access ePHI",
        "physical");

    // HIPAA Security Rule - Technical Safeguards (164.312)
    add("164.312(a)(1)", "Access Control",
        "Implement technical policies to allow access only to authorized persons or software programs",
        "technical");
    add("164.312(b)", "Audit Controls",
        "Implement hardware, software, and procedural mechanisms to record and examine system activity",
        "technical");
    add("164.312(c)(1)", "Integrity Controls",
        "Implement policies to protect ePHI from improper alteration or destruction",
        "technical");
    add("164.312(d)", "Person or Entity Authentication",
        "Implement procedures to verify that persons seeking access to ePHI are who they claim to be",
        "technical");
    add("164.312(e)(1)", "Transmission Security",
        "Implement technical security measures to guard against unauthorized access to ePHI during transmission",
        "technical");

    return controls;
}

Result<ComplianceReport> SecurityEngine::generate_compliance_report(
    const std::string& tenant_id,
    const std::string& framework) {

    std::shared_lock lock(mutex_);

    auto it = compliance_controls_.find(framework);
    if (it == compliance_controls_.end()) {
        return std::unexpected(Error::not_found(
            "Compliance framework not found: " + framework));
    }

    const auto& controls = it->second;

    ComplianceReport report;
    report.id              = generate_uuid();
    report.tenant_id       = tenant_id;
    report.framework       = framework;
    report.total_controls  = static_cast<int32_t>(controls.size());
    report.controls        = controls;
    report.generated_at    = now_iso8601();
    report.valid_until     = "2026-06-30T00:00:00Z";

    // Tally control statuses
    for (const auto& c : controls) {
        if (c.status == "compliant") {
            report.compliant++;
        } else if (c.status == "non_compliant") {
            report.non_compliant++;
        } else if (c.status == "partial") {
            report.partial++;
        } else if (c.status == "not_applicable") {
            report.not_applicable++;
        }
    }

    // Compute overall compliance percentage
    if (report.total_controls > 0) {
        int applicable = report.total_controls - report.not_applicable;
        if (applicable > 0) {
            double total_pct = 0.0;
            for (const auto& c : controls) {
                if (c.status != "not_applicable") {
                    total_pct += c.compliance_percent;
                }
            }
            report.overall_compliance = total_pct / static_cast<double>(applicable);
        }
    }

    spdlog::info("[SecurityEngine] Generated {} compliance report for tenant {}: "
                 "{:.1f}% overall ({} compliant, {} non-compliant, {} partial, {} N/A out of {})",
                 framework, tenant_id, report.overall_compliance,
                 report.compliant, report.non_compliant, report.partial,
                 report.not_applicable, report.total_controls);
    return report;
}

Result<ComplianceReport> SecurityEngine::get_soc2_report(const std::string& tenant_id) {
    return generate_compliance_report(tenant_id, "soc2");
}

Result<ComplianceReport> SecurityEngine::get_iso27001_report(const std::string& tenant_id) {
    return generate_compliance_report(tenant_id, "iso27001");
}

Result<ComplianceReport> SecurityEngine::get_iso28743_report(const std::string& tenant_id) {
    return generate_compliance_report(tenant_id, "iso28743");
}

Result<ComplianceReport> SecurityEngine::get_hipaa_report(const std::string& tenant_id) {
    return generate_compliance_report(tenant_id, "hipaa");
}

std::vector<ComplianceControl> SecurityEngine::list_controls(
    const std::string& framework) const {
    std::shared_lock lock(mutex_);
    auto it = compliance_controls_.find(framework);
    if (it == compliance_controls_.end()) {
        return {};
    }
    return it->second;
}

Result<void> SecurityEngine::update_control_status(const std::string& control_id,
                                                    const std::string& status,
                                                    const std::string& evidence) {
    if (status != "compliant" && status != "non_compliant" &&
        status != "partial" && status != "not_applicable") {
        return std::unexpected(Error::bad_request(
            "Status must be 'compliant', 'non_compliant', 'partial', or 'not_applicable'"));
    }

    std::unique_lock lock(mutex_);

    for (auto& [framework, controls] : compliance_controls_) {
        for (auto& c : controls) {
            if (c.id == control_id) {
                c.status           = status;
                c.last_assessed_at = now_iso8601();
                if (!evidence.empty()) {
                    c.evidence = evidence;
                }

                // Update compliance_percent based on status
                if (status == "compliant") {
                    c.compliance_percent = 100.0;
                } else if (status == "non_compliant") {
                    c.compliance_percent = 0.0;
                } else if (status == "partial") {
                    c.compliance_percent = 50.0;
                } else if (status == "not_applicable") {
                    c.compliance_percent = 100.0;
                }

                spdlog::info("[SecurityEngine] Updated control {} ({}) to status '{}' "
                             "in framework '{}'",
                             c.control_id, control_id, status, framework);
                return {};
            }
        }
    }

    return std::unexpected(Error::not_found("Compliance control not found: " + control_id));
}

// ─── Audit Trail ────────────────────────────────────────────────────────────

Result<void> SecurityEngine::log_audit_event(AuditEvent event) {
    if (event.actor.empty()) {
        return std::unexpected(Error::bad_request("Audit event must have an actor"));
    }
    if (event.action.empty()) {
        return std::unexpected(Error::bad_request("Audit event must have an action"));
    }

    std::unique_lock lock(mutex_);

    if (event.id.empty()) {
        event.id = generate_uuid();
    }
    if (event.timestamp.empty()) {
        event.timestamp = now_iso8601();
    }

    audit_log_.push_back(std::move(event));

    spdlog::debug("[SecurityEngine] Audit event logged: actor={} action={} resource={}",
                  audit_log_.back().actor, audit_log_.back().action,
                  audit_log_.back().resource_id);
    return {};
}

std::vector<AuditEvent> SecurityEngine::query_audit_log(
    const std::string& tenant_id,
    const std::string& actor,
    const std::string& action,
    int32_t limit) const {

    std::shared_lock lock(mutex_);
    std::vector<AuditEvent> result;

    // Iterate in reverse to get most recent first
    for (auto it = audit_log_.rbegin(); it != audit_log_.rend(); ++it) {
        if (static_cast<int32_t>(result.size()) >= limit) break;

        if (!tenant_id.empty() && it->tenant_id != tenant_id) continue;
        if (!actor.empty() && it->actor != actor) continue;
        if (!action.empty() && it->action != action) continue;

        result.push_back(*it);
    }
    return result;
}

// ─── Security Posture & Scoring ─────────────────────────────────────────────

double SecurityEngine::compute_security_score(const SecurityPosture& posture) {
    // Weighted scoring:
    //   Vulnerabilities:    30%
    //   Policy violations:  25%
    //   Secret hygiene:     20%
    //   RBAC coverage:      15%
    //   Encryption:         10%

    // --- Vulnerability score (30%) ---
    // Deduct points per vulnerability by severity
    double vuln_score = 100.0;
    vuln_score -= posture.critical_vulns * 20.0;
    vuln_score -= posture.high_vulns * 10.0;
    vuln_score -= posture.medium_vulns * 3.0;
    if (vuln_score < 0.0) vuln_score = 0.0;

    // --- Policy violation score (25%) ---
    double policy_score = 100.0;
    policy_score -= posture.open_violations * 8.0;
    if (policy_score < 0.0) policy_score = 0.0;

    // --- Secret hygiene score (20%) ---
    double secret_score = 100.0;
    secret_score -= posture.secrets_expiring_30d * 10.0;
    secret_score -= posture.unrotated_secrets * 15.0;
    if (secret_score < 0.0) secret_score = 0.0;

    // --- RBAC coverage (15%) ---
    double rbac_score = posture.rbac_coverage;  // already 0-100

    // --- Encryption coverage (10%) ---
    double encryption_score = posture.encryption_coverage;  // already 0-100

    double weighted =
        (vuln_score * 0.30) +
        (policy_score * 0.25) +
        (secret_score * 0.20) +
        (rbac_score * 0.15) +
        (encryption_score * 0.10);

    // Clamp to [0, 100]
    if (weighted > 100.0) weighted = 100.0;
    if (weighted < 0.0)   weighted = 0.0;

    return std::round(weighted * 10.0) / 10.0;  // one decimal place
}

std::string SecurityEngine::grade_from_score(double score) {
    if (score >= 95.0) return "A+";
    if (score >= 90.0) return "A";
    if (score >= 80.0) return "B";
    if (score >= 70.0) return "C";
    if (score >= 60.0) return "D";
    return "F";
}

Result<SecurityPosture> SecurityEngine::get_security_posture(const std::string& tenant_id) {
    std::shared_lock lock(mutex_);

    SecurityPosture posture;
    posture.tenant_id = tenant_id;

    // Count vulnerabilities by severity
    for (const auto& v : vulnerabilities_) {
        if (v.status == "resolved" || v.status == "mitigated") continue;
        if (v.severity == "critical") posture.critical_vulns++;
        else if (v.severity == "high") posture.high_vulns++;
        else if (v.severity == "medium") posture.medium_vulns++;
    }

    // Count open policy violations for this tenant
    for (const auto& v : violations_) {
        if (v.resolved) continue;
        auto pol_it = security_policies_.find(v.policy_id);
        if (pol_it != security_policies_.end() && pol_it->second.tenant_id == tenant_id) {
            posture.open_violations++;
        }
    }

    // Secret hygiene
    int32_t total_secrets = 0;
    for (const auto& [_, s] : secrets_) {
        if (s.tenant_id == tenant_id) {
            total_secrets++;
            if (!s.expires_at.empty() && s.expires_at != "never") {
                posture.secrets_expiring_30d++;
            }
            if (s.rotation_policy == "never") {
                posture.unrotated_secrets++;
            }
        }
    }

    // RBAC coverage: percentage of principals with at least one role binding
    // Simplified: if there are bindings, consider coverage proportional
    if (!role_bindings_.empty()) {
        std::unordered_set<std::string> bound_principals;
        for (const auto& b : role_bindings_) {
            bound_principals.insert(b.principal);
        }
        // Assume a baseline of known principals being bound = good coverage
        // In production this would compare against actual user/service count
        posture.rbac_coverage = std::min(100.0,
            static_cast<double>(bound_principals.size()) * 20.0);
    } else {
        posture.rbac_coverage = 0.0;
    }

    // Encryption coverage: based on secret encryption (all secrets are encrypted)
    if (total_secrets > 0) {
        posture.encryption_coverage = 100.0;  // all secrets use encrypt_value
    } else {
        posture.encryption_coverage = 80.0;   // no secrets means no data to encrypt
    }

    // MFA adoption (simulated)
    posture.mfa_adoption = 75.0;

    // Default deny
    posture.default_deny_enabled = true;
    for (const auto& [_, np] : network_policies_) {
        if (np.tenant_id == tenant_id && !np.default_deny) {
            posture.default_deny_enabled = false;
            break;
        }
    }

    // SOC2 / ISO27001 status
    auto soc2_it = compliance_controls_.find("soc2");
    if (soc2_it != compliance_controls_.end() && !soc2_it->second.empty()) {
        double sum = 0.0;
        for (const auto& c : soc2_it->second) sum += c.compliance_percent;
        double avg = sum / static_cast<double>(soc2_it->second.size());
        posture.soc2_status = (avg >= 80.0) ? "compliant" : (avg >= 50.0 ? "partial" : "non_compliant");
    } else {
        posture.soc2_status = "not_assessed";
    }

    auto iso_it = compliance_controls_.find("iso27001");
    if (iso_it != compliance_controls_.end() && !iso_it->second.empty()) {
        double sum = 0.0;
        for (const auto& c : iso_it->second) sum += c.compliance_percent;
        double avg = sum / static_cast<double>(iso_it->second.size());
        posture.iso27001_status = (avg >= 80.0) ? "compliant" : (avg >= 50.0 ? "partial" : "non_compliant");
    } else {
        posture.iso27001_status = "not_assessed";
    }

    // Compute final score and grade
    posture.security_score = compute_security_score(posture);
    posture.grade          = grade_from_score(posture.security_score);
    posture.generated_at   = now_iso8601();

    spdlog::info("[SecurityEngine] Security posture for tenant {}: score={:.1f} grade={} "
                 "critical={} high={} medium={} violations={} expiring_secrets={} "
                 "rbac={:.0f}% encryption={:.0f}%",
                 tenant_id, posture.security_score, posture.grade,
                 posture.critical_vulns, posture.high_vulns, posture.medium_vulns,
                 posture.open_violations, posture.secrets_expiring_30d,
                 posture.rbac_coverage, posture.encryption_coverage);

    return posture;
}

Result<double> SecurityEngine::get_security_score(const std::string& tenant_id) {
    auto posture = get_security_posture(tenant_id);
    if (!posture.has_value()) {
        return std::unexpected(posture.error());
    }
    return posture->security_score;
}

// ─── Stats ──────────────────────────────────────────────────────────────────

size_t SecurityEngine::secret_count() const {
    std::shared_lock lock(mutex_);
    return secrets_.size();
}

size_t SecurityEngine::role_count() const {
    std::shared_lock lock(mutex_);
    return roles_.size();
}

size_t SecurityEngine::policy_count() const {
    std::shared_lock lock(mutex_);
    return security_policies_.size();
}

size_t SecurityEngine::vulnerability_count() const {
    std::shared_lock lock(mutex_);
    return vulnerabilities_.size();
}

}  // namespace prodxcloud::platform::security
