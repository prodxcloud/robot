#ifdef HAS_GRPC

#include "api/grpc/inference_service.hpp"
#include "inference/remote_registry.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace prodxcloud::api::grpc_svc {

using json = nlohmann::json;

InferenceServiceImpl::InferenceServiceImpl(
    std::shared_ptr<inference::RemoteModelRegistry> registry)
    : registry_(std::move(registry)) {}

::grpc::Status InferenceServiceImpl::Infer(::grpc::ServerContext*,
                                            const std::string& request_json,
                                            std::string* response_json) {
    try {
        auto body = json::parse(request_json);
        const std::string model_id = body.value("model_id", "");
        const std::string slm_url  = registry_->slm_service_url();

        if (slm_url.empty())
            return ::grpc::Status(::grpc::UNAVAILABLE,
                "SLM-Models service URL not configured (set SLM_SERVICE_URL)");

        spdlog::info("[grpc] Delegating inference model='{}' to {}", model_id, slm_url);

        *response_json = json{
            {"delegated_to", slm_url + "/api/v1/inference"},
            {"model_id", model_id},
            {"message", "Forward this request to the SLM-Models service"}
        }.dump();
        return ::grpc::Status::OK;
    } catch (const std::exception& e) {
        return ::grpc::Status(::grpc::INTERNAL, e.what());
    }
}

::grpc::Status InferenceServiceImpl::InferStream(::grpc::ServerContext*,
                                                   const std::string& request_json,
                                                   ::grpc::ServerWriter<std::string>* writer) {
    try {
        auto body = json::parse(request_json);
        const std::string slm_url = registry_->slm_service_url();

        if (slm_url.empty())
            return ::grpc::Status(::grpc::UNAVAILABLE,
                "SLM-Models service URL not configured");

        json chunk = {
            {"message", "Streaming inference is handled by SLM-Models service"},
            {"slm_service_url", slm_url},
            {"finished", true}
        };
        writer->Write(chunk.dump());
        return ::grpc::Status::OK;
    } catch (const std::exception& e) {
        return ::grpc::Status(::grpc::INTERNAL, e.what());
    }
}

void InferenceServiceImpl::start(const std::string& address) {
    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(address, ::grpc::InsecureServerCredentials());
    server_ = builder.BuildAndStart();
    spdlog::info("gRPC inference proxy listening on {}", address);
}

void InferenceServiceImpl::stop() {
    if (server_) {
        server_->Shutdown();
        spdlog::info("gRPC inference proxy stopped");
    }
}

}  // namespace prodxcloud::api::grpc_svc

#endif  // HAS_GRPC
