# tuyaopen-port (LINUX platform port) build.
# Prerequisite: add_subdirectory(tuyaopen-core) first (switches and tuyaopen-headers ready).
# This directory collects sources/headers, trims by core switches and injects
# tuyaopen-headers; the KV card additionally links core's tuya_transport for
# AES-CBC/SHA-256 (mbedtls-backed).
if(TARGET tuyaopen-port)
    return()
endif()

set(PORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Layout: include/ (headers) + src/ (implementations). The BLE adapter
# (port_bluetooth.c, implementation + card) sits next to the other port_*.c
# files; the Linux-only bluez backend and its third-party library live in
# src/port_bt/, and the KV card with its littlefs engine lives in src/port_kv/.
# Generic utilities live in core (tuyaopen-core/iot-core/tuya_transport/src/utilities);
# tuya_mem_heap is not used by the port (port_system_malloc maps to the platform
# malloc, see src/port_api.c - layering: the port does not depend on core)
set(_port_srcs)
file(GLOB_RECURSE _port_srcs CONFIGURE_DEPENDS
    "${PORT_ROOT}/include/*.c"
    "${PORT_ROOT}/src/*.c"
)
if(_port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/CMakeFiles/")
    # bluez_inc examples/ are third-party samples (central/main.c and
    # peripheral/peripheral.c each define int main()), which GLOB_RECURSE would
    # pull into the port static library when BT=ON, so they are excluded
    list(FILTER _port_srcs EXCLUDE REGEX "/bluez_inc/examples/")
    list(REMOVE_DUPLICATES _port_srcs)
endif()

# littlefs lives in src/port_kv/littlefs, so the src/ GLOB above already picks up
# lfs.c/lfs_util.c. lfs_util.h expands lfs_config.h through LFS_CONFIG (under
# include/system/, injected via _port_incs below); its lfs_malloc / LFS_ERROR use
# this layer's port_system_malloc / PORT_LOG_ERR (layering: not core's
# core_malloc / PR_ERR)
if(EXISTS "${PORT_ROOT}/src/port_kv/littlefs/lfs.c")
    set(_port_lfs_defs LFS_CONFIG=lfs_config.h)
endif()

# BLE sources: the single adapter at src/port_bluetooth.c (implementation +
# card) plus the Linux-only bluez backend under src/port_bt/. All of them are
# dropped when BLE is off; keep this list in sync if another BLE source file is
# added.
if(NOT CONFIG_ENABLE_BLUETOOTH AND _port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/port_bt/")
    list(FILTER _port_srcs EXCLUDE REGEX "/src/port_bluetooth\\.c$")
endif()

set(_port_incs)
file(GLOB_RECURSE _port_hdrs CONFIGURE_DEPENDS
    "${PORT_ROOT}/include/*.h"
    "${PORT_ROOT}/src/*.h"
    "${PORT_ROOT}/interface/*.h"
)
if(_port_hdrs)
    list(FILTER _port_hdrs EXCLUDE REGEX "/CMakeFiles/")
    foreach(_port_hdr ${_port_hdrs})
        get_filename_component(_port_hdr_dir "${_port_hdr}" DIRECTORY)
        list(APPEND _port_incs "${_port_hdr_dir}")
    endforeach()
    list(REMOVE_DUPLICATES _port_incs)
endif()
if(IS_DIRECTORY "${PORT_ROOT}")
    list(APPEND _port_incs "${PORT_ROOT}")
endif()
list(REMOVE_DUPLICATES _port_incs)

if(_port_srcs)
    add_library(tuyaopen-port STATIC)
    target_sources(tuyaopen-port PRIVATE ${_port_srcs})
    target_include_directories(tuyaopen-port PUBLIC ${_port_incs})
    if(_port_lfs_defs)
        target_compile_definitions(tuyaopen-port PRIVATE ${_port_lfs_defs})
    endif()
    if(UNIX)
        target_link_libraries(tuyaopen-port PUBLIC pthread m)
    endif()

    # port_api (core_api ops card): **zero core dependency** - card types come
    # from this layer's interface/, common types from this layer's include/common.
    # Deliberately neither links core_system nor injects core header paths:
    # writing an include of core back fails the build at once (compile-time
    # layering gate).
    #
    # Network is not adapted here anymore: core_network.c calls the platform
    # socket API directly, and this port only provides
    # interface/core_network_platform.h.

    # Path coupled to the src file/directory names: keep in sync
    if(EXISTS "${PORT_ROOT}/src/port_kv/port_kv.c")
        # port_kv (littlefs + AES-CBC/SHA-256): crypto comes from core's
        # mbedtls-backed tuya_transport (core_symmetry/core_hash), now that the
        # port lives inside the core repository; the KV card contract still comes
        # from this layer's interface/core_kv_ops.h.
        target_link_libraries(tuyaopen-port PRIVATE tuya_transport)
    endif()

    if(EXISTS "${PORT_ROOT}/src/port_bluetooth.c" AND CONFIG_ENABLE_BLUETOOTH)
        # BLE (bluez backend in src/port_bt/ + port_bluetooth.c): **zero
        # core dependency** - card contract and CORE_BLE_* types come from this
        # layer's interface/ (core_bluetooth_ops.h + core_bluetooth_def.h), logging uses
        # printf. Deliberately neither links core_system nor injects core ble/ headers:
        # any include of core fails the build (compile-time layering gate)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(GIO gio-2.0 QUIET)
            pkg_check_modules(DBUS dbus-1 QUIET)
        endif()
        if(GIO_FOUND)
            # binc speaks GDBus, which lives in libgio (pulls gobject/glib).
            target_include_directories(tuyaopen-port PRIVATE ${GIO_INCLUDE_DIRS})
            target_link_libraries(tuyaopen-port PRIVATE ${GIO_LIBRARIES})
        else()
            target_include_directories(tuyaopen-port PRIVATE
                /usr/include/glib-2.0
                /usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/glib-2.0/include
            )
            target_link_libraries(tuyaopen-port PRIVATE gio-2.0 gobject-2.0 glib-2.0)
        endif()
        if(DBUS_FOUND)
            target_include_directories(tuyaopen-port PRIVATE ${DBUS_INCLUDE_DIRS})
            target_link_libraries(tuyaopen-port PRIVATE ${DBUS_LIBRARIES})
        endif()
        target_link_libraries(tuyaopen-port PRIVATE bluetooth)
    endif()
else()
    add_library(tuyaopen-port INTERFACE)
    if(_port_incs)
        target_include_directories(tuyaopen-port INTERFACE ${_port_incs})
    endif()
    message(WARNING "tuyaopen-port has no C sources; no TKL implementation will be linked")
endif()

# Port headers (including the static tuya_kconfig.h) are self-injected into the core environment base
if(TARGET tuyaopen-headers)
    target_include_directories(tuyaopen-headers INTERFACE ${_port_incs})
endif()

unset(_port_srcs)
unset(_port_incs)
unset(_port_hdrs)
unset(_port_hdr)
unset(_port_hdr_dir)
unset(_port_lfs_defs)
unset(PORT_ROOT)
