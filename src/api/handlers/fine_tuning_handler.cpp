/// @file fine_tuning_handler.cpp
/// @brief Fine-tuning job endpoints — delegated to the SLM-Models Python service.
///        This application does NOT fine-tune models.

#include "api/handlers/fine_tuning_handler.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;

static json delegation_note() {
    return {
        {"service", "SLM-Models"},
        {"note", "Fine-tuning is handled exclusively by the SLM-Models Python service"},
        {"docs", "See SLM-Models/README.md for fine-tuning API documentation"}
    };
}

crow::response FineTuningHandler::list_jobs(const crow::request&) {
    return crow::response(501, json{
        {"error", "Fine-tuning is not supported in this service"},
        {"delegation", delegation_note()}
    }.dump());
}

crow::response FineTuningHandler::get_job(const crow::request&, const std::string& job_id) {
    return crow::response(501, json{
        {"error", "Fine-tuning is not supported in this service"},
        {"job_id", job_id},
        {"delegation", delegation_note()}
    }.dump());
}

crow::response FineTuningHandler::create_job(const crow::request&) {
    spdlog::warn("[fine-tuning] create_job called — fine-tuning is delegated to SLM-Models service");
    return crow::response(501, json{
        {"error", "Fine-tuning is not supported in this service"},
        {"delegation", delegation_note()}
    }.dump());
}

}  // namespace prodxcloud::api::handlers
