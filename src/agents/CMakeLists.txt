add_library(prodxcloud_agents STATIC
    agent_base.cpp
    agent_controller.cpp
    agent_executor.cpp
    agent_registry.cpp
    policies/retry_policy.cpp
    policies/rate_limit_policy.cpp
    policies/timeout_policy.cpp
    specialized/devops_agent.cpp
    specialized/sre_agent.cpp
    specialized/openclaw_agent.cpp
    specialized/cicd_agent.cpp
)

target_include_directories(prodxcloud_agents PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(prodxcloud_agents PUBLIC
    prodxcloud_common
    prodxcloud_telemetry
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Threads::Threads
)
