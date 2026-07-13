add_library(prodxcloud_api STATIC
    server.cpp
    middleware/tenant_middleware.cpp
    middleware/rate_limiter.cpp
    handlers/inference_handler.cpp
    handlers/agent_handler.cpp
    handlers/specialized_agent_handler.cpp
    handlers/ai_chat_handler.cpp
    handlers/dataset_handler.cpp
    handlers/analysis_handler.cpp
    handlers/catalog_handler.cpp
    handlers/deployment_handler.cpp
    handlers/knowledge_handler.cpp
    handlers/fine_tuning_handler.cpp
    handlers/training_handler.cpp
    handlers/evaluation_handler.cpp
    handlers/tools_handler.cpp
    handlers/web_assets_handler.cpp
    handlers/workflow_handler.cpp
    handlers/dashboard_handler.cpp
)
target_include_directories(prodxcloud_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(prodxcloud_api PUBLIC
    prodxcloud_common prodxcloud_telemetry prodxcloud_agents prodxcloud_ai
    prodxcloud_datasets prodxcloud_analysis prodxcloud_storage
    spdlog::spdlog nlohmann_json::nlohmann_json Threads::Threads
)

# Crow
if(TARGET Crow::Crow)
    target_link_libraries(prodxcloud_api PUBLIC Crow::Crow)
elseif(TARGET crow::crow)
    target_link_libraries(prodxcloud_api PUBLIC crow::crow)
endif()

# gRPC service (optional)
if(TARGET gRPC::grpc++)
    target_sources(prodxcloud_api PRIVATE grpc/inference_service.cpp)
    target_link_libraries(prodxcloud_api PUBLIC gRPC::grpc++ protobuf::libprotobuf)
    target_compile_definitions(prodxcloud_api PUBLIC HAS_GRPC=1)
endif()
