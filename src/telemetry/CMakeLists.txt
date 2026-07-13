add_library(prodxcloud_telemetry STATIC
    metrics.cpp
    logger.cpp
    tracer.cpp
)

target_include_directories(prodxcloud_telemetry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(prodxcloud_telemetry PUBLIC
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(prometheus-cpp_FOUND)
    target_link_libraries(prodxcloud_telemetry PUBLIC prometheus-cpp::core prometheus-cpp::pull)
    target_compile_definitions(prodxcloud_telemetry PUBLIC PRODXCLOUD_PROMETHEUS_ENABLED)
endif()

if(opentelemetry-cpp_FOUND)
    target_link_libraries(prodxcloud_telemetry PUBLIC opentelemetry-cpp::api opentelemetry-cpp::sdk)
    target_compile_definitions(prodxcloud_telemetry PUBLIC PRODXCLOUD_OTEL_ENABLED)
endif()
