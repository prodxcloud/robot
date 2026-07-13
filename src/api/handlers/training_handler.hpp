#pragma once
#include <crow.h>

namespace prodxcloud::api::handlers {

class TrainingHandler {
public:
    crow::response list_jobs(const crow::request& req);
    crow::response get_job(const crow::request& req, const std::string& job_id);
    crow::response create_job(const crow::request& req);
};

}  // namespace prodxcloud::api::handlers
