add_library(prodxcloud_robot STATIC
    kinematics.cpp
    trajectory.cpp
    safety.cpp
    simulator.cpp
    brain.cpp
    controller.cpp
)

target_include_directories(prodxcloud_robot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(prodxcloud_robot PUBLIC
    prodxcloud_common
    prodxcloud_telemetry
    prodxcloud_vxnode
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Threads::Threads
)
