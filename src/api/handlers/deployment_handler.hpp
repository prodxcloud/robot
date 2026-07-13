#pragma once
#include <crow.h>

namespace prodxcloud::api::handlers {

class DeploymentHandler {
public:
    crow::response list_deployments(const crow::request& req);
    crow::response deployment_summary(const crow::request& req);
    crow::response get_deployment(const crow::request& req, const std::string& id);
    crow::response create_deployment(const crow::request& req);
    crow::response update_deployment_status(const crow::request& req, const std::string& id);
};

}  // namespace prodxcloud::api::handlers
