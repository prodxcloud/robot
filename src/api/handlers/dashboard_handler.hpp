#pragma once
#include <crow.h>

namespace prodxcloud::api::handlers {

class DashboardHandler {
public:
    crow::response summary(const crow::request& req);
};

}  // namespace prodxcloud::api::handlers
