#pragma once

#ifdef HAS_GRPC

#include <memory>
#include <grpcpp/grpcpp.h>

namespace prodxcloud::inference { class RemoteModelRegistry; }

namespace prodxcloud::api::grpc_svc {

/// gRPC inference service — proxies to SLM-Models service.
/// No local model execution.
class InferenceServiceImpl final {
public:
    explicit InferenceServiceImpl(std::shared_ptr<inference::RemoteModelRegistry> registry);

    ::grpc::Status Infer(::grpc::ServerContext* context,
                         const std::string& request_json,
                         std::string* response_json);

    ::grpc::Status InferStream(::grpc::ServerContext* context,
                               const std::string& request_json,
                               ::grpc::ServerWriter<std::string>* writer);

    void start(const std::string& address);
    void stop();

private:
    std::shared_ptr<inference::RemoteModelRegistry> registry_;
    std::unique_ptr<::grpc::Server> server_;
};

}  // namespace prodxcloud::api::grpc_svc

#endif  // HAS_GRPC
