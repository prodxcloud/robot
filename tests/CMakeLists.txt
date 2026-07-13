# The robot suite is self-contained — no GoogleTest to install, so `ctest` works on
# a bare toolchain. It runs from the repo root so it can find the brain corpus.
add_executable(test_robot test_robot.cpp)

target_link_libraries(test_robot PRIVATE
    prodxcloud_robot
    prodxcloud_vxnode
    prodxcloud_telemetry
    prodxcloud_common
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Threads::Threads
)

add_test(NAME robot COMMAND test_robot)
set_tests_properties(robot PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "ROBOT_BRAIN_CSV=${CMAKE_SOURCE_DIR}/datasets/brain/robot_brain.csv"
)
