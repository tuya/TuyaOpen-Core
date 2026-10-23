cmake_minimum_required(VERSION 3.16)

project(iot_demo C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_ASAN "Enable AddressSanitizer for memory debugging" OFF)

# Optional Kconfig configuration: switches from this demo's .config (generated
# by running menuconfig in this directory) are imported automatically; y/n
# values are normalized to ON/OFF by the core TuyaOpenOptions module.
# Priority: -D command line > .config > built-in defaults below
set(TUYAOPEN_DOTCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/.config")
if(EXISTS "${TUYAOPEN_DOTCONFIG}")
    file(STRINGS "${TUYAOPEN_DOTCONFIG}" _tuyaopen_kconfig_lines)
    foreach(_tuyaopen_line IN LISTS _tuyaopen_kconfig_lines)
        if(_tuyaopen_line MATCHES "^CONFIG_([A-Z0-9_]+)=(y|n)$")
            if(NOT DEFINED CONFIG_${CMAKE_MATCH_1})
                set(CONFIG_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
            endif()
        elseif(_tuyaopen_line MATCHES "^# CONFIG_([A-Z0-9_]+) is not set$")
            # Switches disabled in menuconfig (default-on switches must honor this
            # form, otherwise they cannot be turned off)
            if(NOT DEFINED CONFIG_${CMAKE_MATCH_1})
                set(CONFIG_${CMAKE_MATCH_1} n)
            endif()
        endif()
    endforeach()
    unset(_tuyaopen_line)
    unset(_tuyaopen_kconfig_lines)
endif()
unset(TUYAOPEN_DOTCONFIG)

# Built-in defaults (the wired-binding demo needs QR code output)
if(NOT DEFINED CONFIG_ENABLE_WIRED)
    set(CONFIG_ENABLE_WIRED ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_QRCODE)
    set(CONFIG_ENABLE_QRCODE ON)
endif()

# Credentials are optional: when unset, the defaults in src/tuya_config.h are
# used, or place a tuya_config_secrets.h in the same directory (see the .example file)
if(NOT TUYA_PRODUCT_ID AND DEFINED ENV{TUYA_PRODUCT_ID})
    set(TUYA_PRODUCT_ID "$ENV{TUYA_PRODUCT_ID}")
endif()
if(NOT TUYA_OPENSDK_UUID AND DEFINED ENV{TUYA_OPENSDK_UUID})
    set(TUYA_OPENSDK_UUID "$ENV{TUYA_OPENSDK_UUID}")
endif()
if(NOT TUYA_OPENSDK_AUTHKEY AND DEFINED ENV{TUYA_OPENSDK_AUTHKEY})
    set(TUYA_OPENSDK_AUTHKEY "$ENV{TUYA_OPENSDK_AUTHKEY}")
endif()

# The examples live inside the core repository; the core root is three levels
# up and carries its linux port under port/linux. -D can point at any other copy.
set(TUYAOPEN_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." CACHE PATH "tuyaopen-core repository path")
set(TUYAOPEN_PORT_DIR "${TUYAOPEN_CORE_DIR}/port/linux" CACHE PATH "linux TKL port directory")
get_filename_component(TUYAOPEN_CORE_DIR "${TUYAOPEN_CORE_DIR}" ABSOLUTE)
get_filename_component(TUYAOPEN_PORT_DIR "${TUYAOPEN_PORT_DIR}" ABSOLUTE)

# 1) core: single entry point tuyaopen-core.cmake (switch macro injection, five
# library groups, and aggregation are all handled here)
include(${TUYAOPEN_CORE_DIR}/tuyaopen-core.cmake)

# 2) port: self-contained subdirectory (builds tuyaopen-port and injects its
# headers into tuyaopen-headers)
add_subdirectory(${TUYAOPEN_PORT_DIR} ${CMAKE_BINARY_DIR}/tuyaopen-port)

# 3) Shared component: qrcode (examples/components/qrcode, shared by the iot/agent/p2p
# demos, gated by CONFIG_ENABLE_QRCODE)
# Out-of-tree source directory: the binary dir must be specified explicitly (same as agent/p2p)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../components/qrcode ${CMAKE_BINARY_DIR}/qrcode)

file(GLOB _iot_demo_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_executable(iot_demo ${_iot_demo_srcs})

target_include_directories(iot_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Explicitly provided credentials override the #ifndef defaults in tuya_config.h
if(TUYA_PRODUCT_ID)
    target_compile_definitions(iot_demo PRIVATE TUYA_PRODUCT_ID="${TUYA_PRODUCT_ID}")
endif()
if(TUYA_OPENSDK_UUID)
    target_compile_definitions(iot_demo PRIVATE TUYA_OPENSDK_UUID="${TUYA_OPENSDK_UUID}")
endif()
if(TUYA_OPENSDK_AUTHKEY)
    target_compile_definitions(iot_demo PRIVATE TUYA_OPENSDK_AUTHKEY="${TUYA_OPENSDK_AUTHKEY}")
endif()

# app link: one group contains both the core libraries and port (resolves circular
# references between CORE/cloud services and TKL); tuyaopen-port outside the group
# provides header paths, tuyaopen-headers provides switch macro injection and port
# headers (the app's own sources also need to see ENABLE_* and TKL headers)
set(_iot_demo_link
    -Wl,--start-group
    ${TUYAOPEN_CORE_LIBS}
    $<TARGET_FILE:tuyaopen-port>
)
# core_netif (core_system) and core_bluetooth (tuya_cloud_service) are inside the group
# via TUYAOPEN_CORE_LIBS; their tkl_netif_* / tkl_ble_* symbols resolve against
# tuyaopen-port in the same group
list(APPEND _iot_demo_link -Wl,--end-group tuyaopen-port tuyaopen-headers)
target_link_libraries(iot_demo PRIVATE ${_iot_demo_link})

# core_kv selftest (standalone executable, links the same libraries as iot_demo)
add_executable(core_kv_selftest selftest/core_kv_selftest.c)
target_link_libraries(core_kv_selftest PRIVATE ${_iot_demo_link})
unset(_iot_demo_link)

# qrcode has no circular dependency and links outside the group; when the switch
# is off the target is not registered and is skipped
if(TARGET qrcode)
    target_link_libraries(iot_demo PRIVATE qrcode)
endif()

if(ENABLE_ASAN)
    target_compile_options(iot_demo PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(iot_demo PRIVATE -fsanitize=address)
endif()
