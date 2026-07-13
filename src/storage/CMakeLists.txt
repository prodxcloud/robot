add_library(prodxcloud_storage STATIC
    vector_store.cpp model_store.cpp
)
target_include_directories(prodxcloud_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(prodxcloud_storage PUBLIC
    prodxcloud_common prodxcloud_telemetry
    spdlog::spdlog nlohmann_json::nlohmann_json Threads::Threads
)

if(TARGET faiss)
    target_link_libraries(prodxcloud_storage PUBLIC faiss)
    target_compile_definitions(prodxcloud_storage PUBLIC HAS_FAISS=1)
endif()
