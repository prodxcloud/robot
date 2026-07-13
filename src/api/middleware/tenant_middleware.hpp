#pragma once
#include <string>
#include <crow.h>
#include "common/types.hpp"

namespace prodxcloud::api::middleware {

// Thread-local tenant context propagation
class TenantContext {
public:
    static void set(const std::string& tenant_id);
    static const std::string& get();
    static void clear();

private:
    static thread_local std::string current_tenant_;
};

class TenantMiddleware {
public:
    // Extract tenant from header or JWT claims
    static Result<std::string> extract_tenant(const crow::request& req);

    // RAII guard for tenant context
    struct Guard {
        explicit Guard(const std::string& tenant_id) { TenantContext::set(tenant_id); }
        ~Guard() { TenantContext::clear(); }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
    };
};

}  // namespace prodxcloud::api::middleware
