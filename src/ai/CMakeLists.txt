add_library(prodxcloud_ai STATIC
    llm_provider.cpp
    providers/anthropic_provider.cpp
    providers/openai_provider.cpp
    providers/ollama_provider.cpp
    intent_detector.cpp
    memory.cpp
    tool_executor.cpp
    ai_agent.cpp
    ai_agent_tools.cpp
)

target_include_directories(prodxcloud_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(prodxcloud_ai PUBLIC
    prodxcloud_common
    prodxcloud_telemetry
    # The agents' provisioning tools are thin wrappers over the node API — there is
    # no other way for an agent to touch infrastructure.
    prodxcloud_vxnode
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Threads::Threads
)
