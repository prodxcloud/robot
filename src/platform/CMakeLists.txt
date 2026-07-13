# The cost engine is gone. It priced cloud resources — EC2 instance-hours, S3
# storage classes, egress — which is only meaningful in a repo that provisions
# them. This one does not: see src/vxnode.
add_library(prodxcloud_platform STATIC
    observability/observability_engine.cpp
    security/security_engine.cpp
)

target_include_directories(prodxcloud_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(prodxcloud_platform PUBLIC
    prodxcloud_common
    prodxcloud_telemetry
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Threads::Threads
)
