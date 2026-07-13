#include "api/middleware/tenant_middleware.hpp"
#include <spdlog/spdlog.h>

namespace prodxcloud::api::middleware {

thread_local std::string TenantContext::current_tenant_;

void TenantContext::set(const std::string& tenant_id) {
    current_tenant_ = tenant_id;
}

const std::string& TenantContext::get() {
    return current_tenant_;
}

void TenantContext::clear() {
    current_tenant_.clear();
}

Result<std::string> TenantMiddleware::extract_tenant(const crow::request& req) {
    // Try X-Tenant-ID header first
    auto header = req.get_header_value("X-Tenant-ID");
    if (!header.empty()) return header;

    // Try query parameter
    auto url_params = crow::query_string(req.url_params);
    auto tenant_param = url_params.get("tenant_id");
    if (tenant_param) return std::string(tenant_param);

    return std::unexpected(Error::validation("Missing tenant identifier (X-Tenant-ID header or tenant_id param)"));
}

}  // namespace prodxcloud::api::middleware
