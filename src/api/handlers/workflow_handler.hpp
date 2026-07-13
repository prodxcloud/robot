#pragma once
#include <crow.h>

namespace prodxcloud::api::handlers {

class WorkflowHandler {
public:
    crow::response list_workflows(const crow::request& req);
    crow::response trigger_workflow(const crow::request& req);
};

}  // namespace prodxcloud::api::handlers
