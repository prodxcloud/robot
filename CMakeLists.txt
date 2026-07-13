cmake_minimum_required(VERSION 3.20)
project(ProdxCloudRobot VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_TESTS  "Build the test suite" ON)
option(ENABLE_SERVER "Build the HTTP API server (needs Crow)" ON)

# ── Required ──────────────────────────────────────────────────────────────────
# The robot core needs exactly two libraries. That is the point: a control loop
# should not need a web framework, a cloud SDK or a model runtime in order to run.
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

# ── Optional ──────────────────────────────────────────────────────────────────
find_package(yaml-cpp CONFIG QUIET)
find_package(gRPC     CONFIG QUIET)
find_package(protobuf CONFIG QUIET)
find_package(faiss    CONFIG QUIET)
find_package(Arrow    CONFIG QUIET)
find_package(Parquet  CONFIG QUIET)

# Crow — header-only HTTP framework, needed only by the REST server.
find_package(Crow CONFIG QUIET)
if(NOT Crow_FOUND)
    find_path(CROW_INCLUDE_DIR crow.h PATHS /usr/local/include /usr/include PATH_SUFFIXES crow)
    if(CROW_INCLUDE_DIR)
        add_library(crow_headers INTERFACE)
        target_include_directories(crow_headers INTERFACE ${CROW_INCLUDE_DIR}/..)
        add_library(crow::crow ALIAS crow_headers)
        set(Crow_FOUND TRUE)
        message(STATUS "Crow headers: ${CROW_INCLUDE_DIR}")
    endif()
endif()

if(ENABLE_SERVER AND NOT Crow_FOUND)
    message(STATUS "Crow not found — skipping the HTTP server. "
                   "The robot, its brain and the vxnode client still build and run.")
    set(ENABLE_SERVER OFF)
endif()

# ── Modules ───────────────────────────────────────────────────────────────────
add_subdirectory(src/common)
add_subdirectory(src/telemetry)

# vxnode: the ONLY path from this repo to infrastructure. There is no cloud SDK,
# no Terraform and no provider credential anywhere in this tree — when the robot
# needs a machine, it asks its node for one.
add_subdirectory(src/vxnode)

# robot: kinematics, trajectories, safety, the device simulator, the onboard brain.
add_subdirectory(src/robot)

add_subdirectory(src/agents)
add_subdirectory(src/datasets)
add_subdirectory(src/analysis)
add_subdirectory(src/storage)
add_subdirectory(src/ai)
add_subdirectory(src/platform)

if(ENABLE_SERVER)
    add_subdirectory(src/api)
    add_subdirectory(apps/api_server)
endif()

# robotsim: the robot with no hardware and no cloud. Never depends on the server.
add_subdirectory(apps/robot_sim)

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "")
message(STATUS "  ProdxCloud Robot ${PROJECT_VERSION}")
message(STATUS "  ---------------------------------")
message(STATUS "  robotsim     : yes")
message(STATUS "  tests        : ${ENABLE_TESTS}")
message(STATUS "  HTTP server  : ${ENABLE_SERVER}")
message(STATUS "  provisioning : delegated to vxnode (none in this repo)")
message(STATUS "")
