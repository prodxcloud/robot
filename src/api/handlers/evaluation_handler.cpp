#include "api/handlers/evaluation_handler.hpp"
#include "api/middleware/tenant_middleware.hpp"
#include "common/types.hpp"
#include "common/uuid.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <vector>
#include <algorithm>

namespace prodxcloud::api::handlers {

using json = nlohmann::json;

// ─── In-memory eval store ───────────────────────────────────────────────────

static std::mutex& eval_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<EvalRun>& eval_store() {
    static std::vector<EvalRun> store;
    return store;
}

static std::mutex& feedback_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::vector<Feedback>& feedback_store() {
    static std::vector<Feedback> store;
    return store;
}

static json eval_to_json(const EvalRun& e) {
    return {
        {"id", e.id},
        {"tenant_id", e.tenant_id},
        {"name", e.name},
        {"model_id", e.model_id},
        {"evaluator_name", e.evaluator_name},
        {"status", e.status},
        {"metrics", json::parse(e.metrics_json, nullptr, false)},
        {"total_prompts", e.total_prompts},
        {"completed_prompts", e.completed_prompts},
        {"created_at", e.created_at}
    };
}

static json feedback_to_json(const Feedback& f) {
    return {
        {"id", f.id},
        {"request_id", f.request_id},
        {"model_id", f.model_id},
        {"tenant_id", f.tenant_id},
        {"feedback_type", f.feedback_type},
        {"feedback_value", f.feedback_value},
        {"feedback_text", f.feedback_text},
        {"query", f.query},
        {"response", f.response},
        {"created_at", f.created_at}
    };
}

// ─── Handlers ───────────────────────────────────────────────────────────────

crow::response EvaluationHandler::list_evals(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(eval_mutex());
    json arr = json::array();

    for (const auto& e : eval_store()) {
        if (e.tenant_id == *tenant)
            arr.push_back(eval_to_json(e));
    }

    return crow::response(200, json{{"evals", arr}, {"count", arr.size()}}.dump());
}

crow::response EvaluationHandler::get_eval(const crow::request& req, const std::string& id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(eval_mutex());

    auto it = std::find_if(eval_store().begin(), eval_store().end(),
        [&](const EvalRun& e) { return e.id == id && e.tenant_id == *tenant; });

    if (it == eval_store().end())
        return crow::response(404, json{{"error", "Evaluation run not found"}}.dump());

    return crow::response(200, eval_to_json(*it).dump());
}

crow::response EvaluationHandler::create_eval(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("name") || !body.contains("model_id"))
        return crow::response(400, json{{"error", "name and model_id are required"}}.dump());

    EvalRun eval;
    eval.id = generate_uuid();
    eval.tenant_id = *tenant;
    eval.name = body.value("name", "");
    eval.model_id = body.value("model_id", "");
    eval.evaluator_name = body.value("evaluator_name", "default");
    eval.status = "pending";
    eval.total_prompts = body.value("total_prompts", 0);
    eval.created_at = now_iso8601();

    std::lock_guard<std::mutex> lock(eval_mutex());
    eval_store().push_back(eval);

    spdlog::info("Created evaluation run {} for tenant {}", eval.id, eval.tenant_id);
    return crow::response(201, eval_to_json(eval).dump());
}

crow::response EvaluationHandler::submit_feedback(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return crow::response(400, json{{"error", "Invalid JSON"}}.dump()); }

    if (!body.contains("request_id") || !body.contains("feedback_type"))
        return crow::response(400, json{{"error", "request_id and feedback_type are required"}}.dump());

    Feedback fb;
    fb.id = generate_uuid();
    fb.request_id = body.value("request_id", "");
    fb.model_id = body.value("model_id", "");
    fb.tenant_id = *tenant;
    fb.feedback_type = body.value("feedback_type", "");
    fb.feedback_value = body.value("feedback_value", "");
    fb.feedback_text = body.value("feedback_text", "");
    fb.query = body.value("query", "");
    fb.response = body.value("response", "");
    fb.created_at = now_iso8601();

    std::lock_guard<std::mutex> lock(feedback_mutex());
    feedback_store().push_back(fb);

    spdlog::info("Submitted feedback {} for request {}", fb.id, fb.request_id);
    return crow::response(201, feedback_to_json(fb).dump());
}

crow::response EvaluationHandler::get_feedback(const crow::request& req, const std::string& request_id) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(feedback_mutex());
    json arr = json::array();

    for (const auto& fb : feedback_store()) {
        if (fb.request_id == request_id && fb.tenant_id == *tenant)
            arr.push_back(feedback_to_json(fb));
    }

    return crow::response(200, json{{"feedback", arr}, {"count", arr.size()}}.dump());
}

crow::response EvaluationHandler::feedback_stats(const crow::request& req) {
    auto tenant = middleware::TenantMiddleware::extract_tenant(req);
    if (!tenant)
        return crow::response(400, json{{"error", tenant.error().message}}.dump());

    std::lock_guard<std::mutex> lock(feedback_mutex());

    int total = 0;
    int thumbs_up = 0, thumbs_down = 0, rating = 0, text = 0;
    json by_model = json::object();

    for (const auto& fb : feedback_store()) {
        if (fb.tenant_id != *tenant) continue;
        ++total;
        if (fb.feedback_type == "thumbs_up") ++thumbs_up;
        else if (fb.feedback_type == "thumbs_down") ++thumbs_down;
        else if (fb.feedback_type == "rating") ++rating;
        else if (fb.feedback_type == "text") ++text;

        if (!fb.model_id.empty())
            by_model[fb.model_id] = by_model.value(fb.model_id, 0) + 1;
    }

    return crow::response(200, json{
        {"total", total},
        {"thumbs_up", thumbs_up},
        {"thumbs_down", thumbs_down},
        {"rating", rating},
        {"text", text},
        {"by_model", by_model}
    }.dump());
}

}  // namespace prodxcloud::api::handlers
