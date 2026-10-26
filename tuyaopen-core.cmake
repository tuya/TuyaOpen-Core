# tuyaopen-core build entry (used via include).
#
# Host integration (plain CMake project):
#   include(<tuyaopen-core>/tuyaopen-core.cmake)
#   add_subdirectory(<port>)                # the port injects its headers into tuyaopen-headers
# esp-idf: the root CMakeLists.txt is the component entry (it includes this file
# after idf_component_register) -- an include context does not call project(),
# so no esp-idf special-casing is needed.
#
# Prerequisite: the host has already called project(). Idempotent: repeated
# includes are skipped automatically.
if(TARGET tuyaopen-headers)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/TuyaOpenOptions.cmake)

# Root path of the official libraries (third_party submodules), visible to all scopes
set(TUYAOPEN_THIRD_PARTY_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party)

# Environment base (same role as esp-idf's esp_common): the host injects the port
# header paths and modules depend on it uniformly as PRIVATE. Inter-module
# dependencies are declared explicitly by each module (the plain-CMake form of
# esp-idf REQUIRES/PRIV_REQUIRES).
# Host injection:
#   target_include_directories(tuyaopen-headers INTERFACE <port headers>)
add_library(tuyaopen-headers INTERFACE)

# Switches -> compile macros: only enabled ones are injected (source guards use
# the defined() style, so OFF means not defined).
# For manual builds (not using this CMake), the equivalent is -DENABLE_BLUETOOTH=1 ...
# in the compile command, or uncommenting the matching #define in the port's
# static tuya_kconfig.h.
set(_tuyaopen_switch_defs)
foreach(_tuyaopen_opt IN ITEMS
    ENABLE_BLUETOOTH
    ENABLE_PLATFORM_MBEDTLS
    ENABLE_DEVICE_TIMER
    ENABLE_TUYA_P2P
    ENABLE_LOCAL_STORE
    ENABLE_IPC_RING_BUFFER
    ENABLE_AI_COMPONENTS
    ENABLE_AI_MONITOR
    ENABLE_COMP_AI_MCP
    ENABLE_COMP_AI_AUDIO
    ENABLE_AI_PLAYER
    AI_PLAYER_ALERT_SOURCE_LOCAL
    AI_PLAYER_ALERT_SOURCE_CUSTOM
    ENABLE_AI_PLAYER_DECODER_OPUS
    ENABLE_AI_PLAYER_DECODER_OGGOPUS
    ENABLE_QRCODE
    LITTLE_END
)
    if(CONFIG_${_tuyaopen_opt})
        list(APPEND _tuyaopen_switch_defs ${_tuyaopen_opt}=1)
    endif()
endforeach()
if(_tuyaopen_switch_defs)
    target_compile_definitions(tuyaopen-headers INTERFACE ${_tuyaopen_switch_defs})
endif()
unset(_tuyaopen_switch_defs)
unset(_tuyaopen_opt)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third_party ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-core/third_party)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/iot-core ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-core/iot-core)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/agent-core ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-core/agent-core)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/p2p-core ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-core/p2p-core)

set(_tuyaopen_core_libs)
# Core modules only. Application-side components (ai_agent/ai_skills/ai_mcp/
# audio_player/ai_audio/utility) live in the application layer
# (examples/agent_demo/components) and are aggregated and linked by the application.
foreach(_tuyaopen_mod IN ITEMS
    libcjson
    tuya_utilities
    tuya_transport
    corehttp
    coremqtt
    backoff_algorithm
    mbedtls
    core_system
    tuya_cloud_service
    tuya_ai_service
    base_ice
    lib_rtp
    pjproject
    svc_ipc_core
    svc_streaming_p2p
    local_store
    svc_ring_buffer
)
    if(TARGET ${_tuyaopen_mod})
        list(APPEND _tuyaopen_core_libs ${_tuyaopen_mod})
    endif()
endforeach()

# External aggregation: core modules only.
# - Target tuyaopen-core: flat passthrough for hosts that need no circular-reference
#   handling (such as esp-idf components)
# - Variable TUYAOPEN_CORE_LIBS: GNU/Clang hosts use it to put the core libraries
#   and the port into the same --start-group and resolve the circular references
#   between core services and the port (under CMake 3.16 raw flags and
#   INTERFACE targets interleave incorrectly; a variable list preserves the order):
#   target_link_libraries(app PRIVATE
#       -Wl,--start-group ${TUYAOPEN_CORE_LIBS} $<TARGET_FILE:tuyaopen-port> -Wl,--end-group
#       tuyaopen-port)
add_library(tuyaopen-core INTERFACE)
target_link_libraries(tuyaopen-core INTERFACE ${_tuyaopen_core_libs})
set(TUYAOPEN_CORE_LIBS "${_tuyaopen_core_libs}" CACHE INTERNAL "tuyaopen-core static libs")
unset(_tuyaopen_core_libs)
unset(_tuyaopen_mod)
