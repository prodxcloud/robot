#pragma once
/// @brief Analysis handler — benchmark/evaluate/drift endpoints.
///        Model-dependent analysis is delegated to the SLM-Models service.
#include <crow.h>

namespace prodxcloud::api::handlers {

class AnalysisHandler {
public:
    AnalysisHandler() = default;

    crow::response benchmark(const crow::request& req);
    crow::response evaluate(const crow::request& req);
    crow::response drift_detect(const crow::request& req);
};

}  // namespace prodxcloud::api::handlers
