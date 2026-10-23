cmake_minimum_required(VERSION 3.16)

project(p2p_demo C)

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
        elseif(_tuyaopen_line MATCHES "^CONFIG_([A-Z0-9_]+)=(.+)$")
            # int/string options (demo parameters, clip path, etc.): kconfiglib
            # quotes string values (an empty string is written as CONFIG_X=""), so
            # strip the surrounding quotes here -- otherwise an empty value turns
            # into two quote characters, the "non-empty" test is wrong, and the
            # injection produces an empty macro like -DXXX="""".
            # Note: string(REGEX REPLACE) overwrites CMAKE_MATCH_*, so both the
            # variable name and value must be saved into local variables before
            # running the regex; otherwise set writes to the overwritten group
            # name.
            if(NOT DEFINED CONFIG_${CMAKE_MATCH_1})
                set(_tuyaopen_name "${CMAKE_MATCH_1}")
                set(_tuyaopen_val "${CMAKE_MATCH_2}")
                string(REGEX REPLACE "^\"(.*)\"$" "\\1" _tuyaopen_val "${_tuyaopen_val}")
                set(CONFIG_${_tuyaopen_name} "${_tuyaopen_val}")
            endif()
        endif()
    endforeach()
    unset(_tuyaopen_line)
    unset(_tuyaopen_name)
    unset(_tuyaopen_val)
    unset(_tuyaopen_kconfig_lines)
endif()
unset(TUYAOPEN_DOTCONFIG)

# Built-in defaults: switches required by the P2P link (wired demo, QR code used
# for scan-to-bind)
if(NOT DEFINED CONFIG_ENABLE_WIRED)
    set(CONFIG_ENABLE_WIRED ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_QRCODE)
    set(CONFIG_ENABLE_QRCODE ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_TUYA_P2P)
    set(CONFIG_ENABLE_TUYA_P2P ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_IPC_RING_BUFFER)
    set(CONFIG_ENABLE_IPC_RING_BUFFER ON)
endif()

# Demo parameter defaults (same as the Kconfig defaults; used by the clip and AV info)
if(NOT DEFINED CONFIG_P2P_DEMO_WIDTH)
    set(CONFIG_P2P_DEMO_WIDTH 2304)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_HEIGHT)
    set(CONFIG_P2P_DEMO_HEIGHT 1296)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_FPS)
    set(CONFIG_P2P_DEMO_FPS 15)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_KBPS)
    set(CONFIG_P2P_DEMO_KBPS 960)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_GOP)
    set(CONFIG_P2P_DEMO_GOP 60)
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
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../components/qrcode/CMakeLists.txt")
    message(FATAL_ERROR "p2p_demo must be built inside the examples tree (it reuses the shared qrcode component): ${CMAKE_CURRENT_SOURCE_DIR}/../components/qrcode/CMakeLists.txt not found")
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../components/qrcode ${CMAKE_BINARY_DIR}/qrcode)

# 4) Video source: copied to the build directory at configure time, used by the
# demo_vcam default path "demo_video.264"
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/demo_video.264 ${CMAKE_BINARY_DIR}/demo_video.264 COPYONLY)

file(GLOB _p2p_demo_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
# Platform adaptation layer (src/adapt/: replacement point on real hardware -- fs_card)
# is collected together with the root directory
file(GLOB _p2p_demo_adapt_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/adapt/*.c"
)
list(APPEND _p2p_demo_srcs ${_p2p_demo_adapt_srcs})
unset(_p2p_demo_adapt_srcs)
# Selftests (selftest/ directory, standalone targets with their own main()) are not
# matched by this GLOB, so no filtering is needed

add_executable(p2p_demo ${_p2p_demo_srcs})

target_include_directories(p2p_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Explicitly provided credentials override the #ifndef defaults in tuya_config.h
if(TUYA_PRODUCT_ID)
    target_compile_definitions(p2p_demo PRIVATE TUYA_PRODUCT_ID="${TUYA_PRODUCT_ID}")
endif()
if(TUYA_OPENSDK_UUID)
    target_compile_definitions(p2p_demo PRIVATE TUYA_OPENSDK_UUID="${TUYA_OPENSDK_UUID}")
endif()
if(TUYA_OPENSDK_AUTHKEY)
    target_compile_definitions(p2p_demo PRIVATE TUYA_OPENSDK_AUTHKEY="${TUYA_OPENSDK_AUTHKEY}")
endif()

# Demo parameters and switch injection (shared by the app and the media chain
# selftest; the core tuyaopen-headers only handles ENABLE_* switch macros)
set(_p2p_demo_media_defs
    P2P_DEMO_WIDTH=${CONFIG_P2P_DEMO_WIDTH}
    P2P_DEMO_HEIGHT=${CONFIG_P2P_DEMO_HEIGHT}
    P2P_DEMO_FPS=${CONFIG_P2P_DEMO_FPS}
    P2P_DEMO_KBPS=${CONFIG_P2P_DEMO_KBPS}
    P2P_DEMO_GOP=${CONFIG_P2P_DEMO_GOP}
)
# "y"/"n" from .config can be tested directly as booleans in CMake (y=true, n=false)
# local_store playback: P2P_DEMO_LOCAL_STORE=1 is injected only when both the demo
# switch and the core switch are on (the only switch in the code, see the contract
# comment in tuya_ipc_demo.h). If only the demo switch is on, a warning is emitted and
# nothing is injected -- playback is not compiled and the live path works as usual
# (the menuconfig path keeps them consistent through Kconfig select).
if(CONFIG_P2P_DEMO_LOCAL_STORE)
    if(CONFIG_ENABLE_LOCAL_STORE)
        list(APPEND _p2p_demo_media_defs P2P_DEMO_LOCAL_STORE=1)
    else()
        message(WARNING "CONFIG_P2P_DEMO_LOCAL_STORE is on but CONFIG_ENABLE_LOCAL_STORE is off: "
                        "playback will not be compiled (the menuconfig path keeps them consistent "
                        "through Kconfig select; when using -D both must be given)")
    endif()
endif()
if(CONFIG_P2P_DEMO_LIVE_RECORD)
    list(APPEND _p2p_demo_media_defs P2P_DEMO_LIVE_RECORD=1)
endif()
# Segment duration: Kconfig only writes it when LIVE_RECORD is on; fall back to 120
# when undefined or empty
if(NOT DEFINED CONFIG_P2P_DEMO_RECORD_MAX_SEC OR CONFIG_P2P_DEMO_RECORD_MAX_SEC STREQUAL "")
    set(CONFIG_P2P_DEMO_RECORD_MAX_SEC 120)
endif()
list(APPEND _p2p_demo_media_defs P2P_DEMO_RECORD_MAX_SEC=${CONFIG_P2P_DEMO_RECORD_MAX_SEC})
if(CONFIG_P2P_DEMO_PB_SEED)
    list(APPEND _p2p_demo_media_defs P2P_DEMO_PB_SEED=1)
endif()
# Clip path override: when non-empty, use the specified file (defaults to
# demo_video.264 in the build directory)
if(CONFIG_P2P_DEMO_H264_FILE)
    list(APPEND _p2p_demo_media_defs P2P_DEMO_H264_FILE="${CONFIG_P2P_DEMO_H264_FILE}")
endif()
target_compile_definitions(p2p_demo PRIVATE ${_p2p_demo_media_defs})

# app link: one group contains both the core libraries and port (resolves circular
# references between CORE/cloud services and TKL); tuyaopen-port outside the group
# provides header paths, tuyaopen-headers provides switch macro injection and port
# headers (the app's own sources also need to see ENABLE_* and TKL headers)
set(_p2p_demo_link
    -Wl,--start-group
    ${TUYAOPEN_CORE_LIBS}
    $<TARGET_FILE:tuyaopen-port>
)
list(APPEND _p2p_demo_link -Wl,--end-group tuyaopen-port tuyaopen-headers)
target_link_libraries(p2p_demo PRIVATE ${_p2p_demo_link})

# demo_vcam selftest (standalone executable, links the same library group as p2p_demo)
# demo_vcam.c is the implementation under test (the app target picks it up via GLOB);
# include it explicitly here
add_executable(vcam_selftest
    selftest/demo_vcam_selftest.c
    src/demo_vcam.c
)
target_include_directories(vcam_selftest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Same as media_chain_selftest: let -DCONFIG_P2P_DEMO_H264_FILE and similar overrides
# apply to the vcam selftest as well, otherwise after switching the video source the
# selftest would silently test the bundled video
target_compile_definitions(vcam_selftest PRIVATE ${_p2p_demo_media_defs})
target_link_libraries(vcam_selftest PRIVATE ${_p2p_demo_link})

# Media chain selftest (standalone executable, links the same library group as p2p_demo):
# runs vcam -> frame validation/queue/ring buffer -> frame-fetch callback without cloud
# or App. Does not include tuya_main.c/reset_netcfg.c (the selftest does not connect to
# the cloud); the media implementation files are listed explicitly
add_executable(media_chain_selftest
    selftest/media_chain_selftest.c
    src/tuya_ipc_demo.c
    src/demo_vcam.c
    src/demo_media_event.c
)
target_include_directories(media_chain_selftest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(media_chain_selftest PRIVATE ${_p2p_demo_media_defs})
target_link_libraries(media_chain_selftest PRIVATE ${_p2p_demo_link})
unset(_p2p_demo_link)
unset(_p2p_demo_media_defs)

# qrcode has no circular dependency and links outside the group; when the switch
# is off the target is not registered and is skipped
if(TARGET qrcode)
    target_link_libraries(p2p_demo PRIVATE qrcode)
endif()

if(ENABLE_ASAN)
    target_compile_options(p2p_demo PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(p2p_demo PRIVATE -fsanitize=address)
    # Also enabled for the selftests: the vcam buffer/index allocations are part of
    # the memory surface, so running them under ASAN is worthwhile
    target_compile_options(vcam_selftest PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(vcam_selftest PRIVATE -fsanitize=address)
    # Same for the media chain selftest (frame pool/ring buffer copies are part of
    # the memory surface)
    target_compile_options(media_chain_selftest PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(media_chain_selftest PRIVATE -fsanitize=address)
endif()
