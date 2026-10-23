if(NOT CONFIG_ENABLE_COMP_AI_MCP)
    return()
endif()

file(GLOB _ai_mcp_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(ai_mcp STATIC)
target_sources(ai_mcp PRIVATE ${_ai_mcp_srcs})
target_include_directories(ai_mcp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
# tuya_cloud_service: device_info_get reads the real devid from the tuya_iot
# client (tuya_iot_client_get / tuya_iot_devid_get, headers on
# tuya_cloud_service's PUBLIC path). The dependency is one-way
# (tuya_cloud_service does not depend on application-side components), so there
# is no cycle.
target_link_libraries(ai_mcp PRIVATE tuyaopen-headers core_system ai_agent tuya_ai_service tuya_cloud_service)
unset(_ai_mcp_srcs)
