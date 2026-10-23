# tuya_cloud_service sources are flat under iot-core/ (cloud/, lan/, ble/, ...),
# so the target is defined here rather than in per-module subdirectories.

# Base libraries of this group: tuya_utilities (common tools, no internal deps),
# core_system (system abstraction) and tuya_transport (connection service layer,
# which includes core_network / core_security / libtls / http&mqtt wrappers).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/utilities)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/core_system)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tuya_transport)
file(GLOB_RECURSE _cloud_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lan/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cloud/*.c"
)

if(NOT CONFIG_ENABLE_DEVICE_TIMER)
    list(REMOVE_ITEM _cloud_srcs "${CMAKE_CURRENT_SOURCE_DIR}/cloud/tuya_device_timer.c")
endif()

# The LAN protocol (lan/tuya_lan.c) is always built, as upstream; lan_sock.c
# (the socket event loop) is shared with the AI monitor.

set(_cloud_incs
    ${CMAKE_CURRENT_SOURCE_DIR}/cloud
    ${CMAKE_CURRENT_SOURCE_DIR}/file_storage
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/schema
    ${CMAKE_CURRENT_SOURCE_DIR}/lan
    ${CMAKE_CURRENT_SOURCE_DIR}/weather
)

# BLE network configuration is the self-contained module ble/ble_netcfg.c,
# compiled with BLUETOOTH and started directly by the application
# (tuya_ble_init -> ble_netcfg_start(finish_cb)).

if(CONFIG_ENABLE_BLUETOOTH)
    file(GLOB_RECURSE _ble_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ble/*.c"
    )
    list(APPEND _cloud_srcs ${_ble_srcs})
    list(APPEND _cloud_incs ${CMAKE_CURRENT_SOURCE_DIR}/ble)
    unset(_ble_srcs)
endif()

add_library(tuya_cloud_service STATIC)
target_sources(tuya_cloud_service PRIVATE ${_cloud_srcs})
target_include_directories(tuya_cloud_service PUBLIC ${_cloud_incs})
set(_cloud_deps
    tuyaopen-headers
    tuya_transport
    corehttp
    coremqtt
    backoff_algorithm
    mbedtls
    libcjson
    core_system
)
target_link_libraries(tuya_cloud_service
    PUBLIC tuya_transport libcjson core_system corehttp coremqtt backoff_algorithm mbedtls
    PRIVATE ${_cloud_deps}
)
unset(_cloud_srcs)
unset(_cloud_incs)
unset(_cloud_deps)
