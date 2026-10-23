# AI service layer (tuya_ai_service target: AI protocol/transport and MCP
# server core).
# Application-side components (utility/audio_player/ai_audio/ai_agent/
# ai_skills/ai_mcp tools) live in the application and are organized by the
# application itself (see examples/agent_demo/components/).
if(NOT CONFIG_ENABLE_AI_COMPONENTS)
    return()
endif()

set(_ai_srcs)
set(_ai_incs)

list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/port/include)
file(GLOB_RECURSE _ai_port_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/port/src/*.c")
list(APPEND _ai_srcs ${_ai_port_srcs})
unset(_ai_port_srcs)

file(GLOB_RECURSE _ai_agent_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_agent/src/*.c")
list(APPEND _ai_srcs ${_ai_agent_srcs})
list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_agent/include)
unset(_ai_agent_srcs)

file(GLOB_RECURSE _ai_basic_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_basic/src/*.c")
list(APPEND _ai_srcs ${_ai_basic_srcs})
list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_basic/include)
unset(_ai_basic_srcs)

file(GLOB_RECURSE _ai_monitor_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_monitor/src/*.c")
list(APPEND _ai_srcs ${_ai_monitor_srcs})
list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_monitor/include)
unset(_ai_monitor_srcs)

# MCP server core (protocol parsing and the mcp_cb wiring with
# svc_ai_agent). The tool set (ai_mcp) lives in the application and uses
# its public API through this include path. The module is gated by
# CONFIG_ENABLE_COMP_AI_MCP (defined in svc_ai_mcp/Kconfig).
if(CONFIG_ENABLE_COMP_AI_MCP)
    file(GLOB_RECURSE _ai_mcp_server_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_mcp/src/*.c")
    list(APPEND _ai_srcs ${_ai_mcp_server_srcs})
    list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_mcp/include)
    unset(_ai_mcp_server_srcs)
endif()

add_library(tuya_ai_service STATIC)
target_sources(tuya_ai_service PRIVATE ${_ai_srcs})
target_include_directories(tuya_ai_service PUBLIC ${_ai_incs})
target_link_libraries(tuya_ai_service PRIVATE
    tuyaopen-headers
    libcjson
    tuya_transport
    core_system
    tuya_cloud_service
)
unset(_ai_srcs)
unset(_ai_incs)
