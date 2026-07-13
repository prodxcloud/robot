#pragma once
#include <crow.h>

namespace prodxcloud::api::handlers {

class EvaluationHandler {
public:
    crow::response list_evals(const crow::request& req);
    crow::response get_eval(const crow::request& req, const std::string& id);
    crow::response create_eval(const crow::request& req);
    crow::response submit_feedback(const crow::request& req);
    crow::response get_feedback(const crow::request& req, const std::string& request_id);
    crow::response feedback_stats(const crow::request& req);
};

}  // namespace prodxcloud::api::handlers
