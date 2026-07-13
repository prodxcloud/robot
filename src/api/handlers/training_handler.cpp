/// @file training_handler.cpp
/// @brief Training job endpoints — delegated to the SLM-Models Python service.
///        This application does NOT train or fine-tune models.

#include "api/handlers/training_handler.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;

static const json delegation_note() {
    return {
        {"service", "SLM-Models"},
        {"note", "Model training is handled exclusively by the SLM-Models Python service"},
        {"docs", "See SLM-Models/README.md for training API documentation"}
    };
}

crow::response TrainingHandler::list_jobs(const crow::request&) {
    return crow::response(501, json{
        {"error", "Training is not supported in this service"},
        {"delegation", delegation_note()}
    }.dump());
}

crow::response TrainingHandler::get_job(const crow::request&, const std::string& job_id) {
    return crow::response(501, json{
        {"error", "Training is not supported in this service"},
        {"job_id", job_id},
        {"delegation", delegation_note()}
    }.dump());
}

crow::response TrainingHandler::create_job(const crow::request&) {
    spdlog::warn("[training] create_job called — training is delegated to SLM-Models service");
    return crow::response(501, json{
        {"error", "Training is not supported in this service"},
        {"delegation", delegation_note()}
    }.dump());
}

}  // namespace prodxcloud::api::handlers
