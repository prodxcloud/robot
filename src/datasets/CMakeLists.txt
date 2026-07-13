add_library(prodxcloud_datasets STATIC
    cleaner.cpp
    validators/schema_validator.cpp
    validators/outlier_detector.cpp
    validators/dedup_engine.cpp
    formats/parquet_reader.cpp
    formats/jsonl_reader.cpp
)

target_include_directories(prodxcloud_datasets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(prodxcloud_datasets PUBLIC
    prodxcloud_common prodxcloud_telemetry spdlog::spdlog nlohmann_json::nlohmann_json Threads::Threads
)

if(Arrow_FOUND AND Parquet_FOUND)
    target_link_libraries(prodxcloud_datasets PUBLIC Arrow::arrow_shared Parquet::parquet_shared)
    target_compile_definitions(prodxcloud_datasets PUBLIC PRODXCLOUD_ARROW_ENABLED)
endif()
