add_library(prodxcloud_analysis STATIC
    model_analyzer.cpp benchmark.cpp evaluator.cpp drift_detector.cpp
)
target_include_directories(prodxcloud_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(prodxcloud_analysis PUBLIC
    prodxcloud_common prodxcloud_telemetry
    spdlog::spdlog nlohmann_json::nlohmann_json Threads::Threads
)
