add_library(prodxcloud_vxnode STATIC
    http_client.cpp
    vxnode_client.cpp
)

target_include_directories(prodxcloud_vxnode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(prodxcloud_vxnode PUBLIC
    prodxcloud_common
    prodxcloud_telemetry
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# Optional: libcurl gives the client TLS, for nodes reached over https:// rather
# than on the loopback interface. Without it, http:// still works everywhere.
find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(prodxcloud_vxnode PUBLIC CURL::libcurl)
    target_compile_definitions(prodxcloud_vxnode PUBLIC HAS_CURL=1)
    message(STATUS "vxnode client: TLS enabled (libcurl)")
else()
    message(STATUS "vxnode client: plain HTTP only (libcurl not found) — fine for a loopback node")
endif()

if(WIN32)
    target_link_libraries(prodxcloud_vxnode PUBLIC ws2_32)
endif()
