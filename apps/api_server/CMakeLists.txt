add_executable(prodxcloud_api_server main.cpp)

target_link_libraries(prodxcloud_api_server PRIVATE
    prodxcloud_api prodxcloud_agents
    prodxcloud_datasets prodxcloud_analysis prodxcloud_storage
    prodxcloud_telemetry prodxcloud_common
    spdlog::spdlog nlohmann_json::nlohmann_json yaml-cpp Threads::Threads
)

install(TARGETS prodxcloud_api_server RUNTIME DESTINATION bin)
