# robotsim — the robot with no hardware and no cloud.
#
# Deliberately does NOT link the HTTP API server (and therefore does not need
# Crow): the robot core, its brain and the vxnode client must stand on their own,
# so a workcell can run the control loop without a web stack anywhere near it.
add_executable(robotsim main.cpp)

target_link_libraries(robotsim PRIVATE
    prodxcloud_robot
    prodxcloud_vxnode
    prodxcloud_telemetry
    prodxcloud_common
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Threads::Threads
)

install(TARGETS robotsim RUNTIME DESTINATION bin)
