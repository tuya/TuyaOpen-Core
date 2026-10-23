# ESP32 port (esp-idf component).
# Layout: include/ (domain headers) + src/ (TKL implementation) + interface/
# (contract layer).
#
# esp-idf derives a component's name from its directory name, so this
# directory is the `esp-idf` component: the application adds it with
# EXTRA_COMPONENT_DIRS and depends on `esp-idf`. The plain library target built
# below is named `tuyaopen-port` (the port archive), independent of the
# component name.
idf_component_register(
    SRCS "" INCLUDE_DIRS ""
    REQUIRES tuyaopen-core esp_wifi esp_event esp_netif nvs_flash lwip esp_timer app_update bt
)

set(PORT_ROOT "${CMAKE_CURRENT_LIST_DIR}")

set(_port_srcs)
file(GLOB_RECURSE _port_srcs CONFIGURE_DEPENDS
    "${PORT_ROOT}/include/*.c"
    "${PORT_ROOT}/src/*.c"
)
if(_port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/CMakeFiles/")
    list(REMOVE_DUPLICATES _port_srcs)
endif()

# BLE gating. CONFIG_ENABLE_BLUETOOTH is core's switch, visible in the
# component scope; the implementation is built on the esp-idf bundled NimBLE
# host, so CONFIG_BT_NIMBLE_ENABLED must be on as well. src/port_bluetooth.c is
# always compiled: its own ENABLE_BLUETOOTH guard selects the NimBLE
# implementation + real card, or the empty card (all members NULL) so core
# degrades every BLE call to OPRT_NOT_SUPPORTED.
if(CONFIG_ENABLE_BLUETOOTH AND NOT CONFIG_BT_NIMBLE_ENABLED)
    message(FATAL_ERROR
        "CONFIG_ENABLE_BLUETOOTH is on but CONFIG_BT_NIMBLE_ENABLED is off. "
        "The tuyaopen-port BLE implementation (src/port_bluetooth.c) is built "
        "on the esp-idf bundled NimBLE host: enable CONFIG_BT_ENABLED and "
        "CONFIG_BT_NIMBLE_ENABLED, or turn CONFIG_ENABLE_BLUETOOTH off.")
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

# ---- core's mbedtls integration under the esp-idf host (platform specifics
# stay in the port; core is untouched) ----
#
# esp_config.h: the bare name in core modules' target_link_libraries(... mbedtls ...)
# resolves in an esp-idf build to esp-idf's mbedtls library target (created by
# add_subdirectory(mbedtls) inside the mbedtls component, an upstream target of
# the same name) -- it carries the MBEDTLS_CONFIG_FILE definition but lacks the
# directory containing esp_config.h (that directory is attached to the component
# target idf::mbedtls). Add that include to core's environment base
# (tuyaopen-headers, which core modules uniformly depend on PRIVATE), covering
# all core translation units. The two THREADING_ALT header variants each belong
# to their own allocator: the core side (tuya_tls.c) uses the core_mutex version
# from core's config, while esp-idf's mbedtls library/component uses the ROM
# version (injected into the mbedtls target by the main component), and they do
# not conflict.
if(TARGET tuyaopen-headers AND TARGET idf::mbedtls)
    target_include_directories(tuyaopen-headers INTERFACE
        $ENV{IDF_PATH}/components/mbedtls/port/include)
endif()

# ---- core's lwIP integration under the esp-idf host ----
#
# core_network.c includes this port's interface/core_network_platform.h, which
# pulls in lwip/sockets.h and friends. Core modules are plain static libraries
# that only link tuyaopen-headers (PRIVATE); attach the lwIP component to that
# shared base as an explicit edge. (IDF v5.4 also propagates lwIP includes via
# esp_common/newlib common requirements, but this does not rely on that.)
if(TARGET tuyaopen-headers AND TARGET idf::lwip)
    target_link_libraries(tuyaopen-headers INTERFACE idf::lwip)
endif()

if(_port_srcs)
    add_library(tuyaopen-port STATIC)
    target_sources(tuyaopen-port PRIVATE ${_port_srcs})
    target_include_directories(tuyaopen-port PUBLIC ${_port_incs})

    # port_api/port_kv/port_netif/port_bluetooth: **zero core dependency**
    # -- card contracts come from this layer's interface/ (core_api_ops.h /
    # core_kv_ops.h / core_netif_ops.h / core_bluetooth_ops.h), common types
    # come from this layer's include/common, and logging goes through this
    # layer's port_log.h. Deliberately does not link core_system and does not
    # add core's ble/ header path: if anyone writes an include back to core
    # (such as core_log.h / core_memory.h / core_system.h), compilation fails
    # immediately (a compile-time self-proving layering gate).
    #
    # Network is not adapted here anymore: core_network.c calls the platform
    # socket API directly, and this port only provides
    # interface/core_network_platform.h. That header is included by core, so
    # the lwIP include path must reach core below.

    # BLE: same switch shape as core's ENABLE_BLUETOOTH (the C-side guard in
    # src/port_bluetooth.c reads it).
    # NimBLE is guaranteed to be on here by the source-selection gate above.
    if(CONFIG_ENABLE_BLUETOOTH)
        target_compile_definitions(tuyaopen-port PRIVATE ENABLE_BLUETOOTH=1)
    endif()

    # TKL sources (a plain library) need esp-idf component headers, injected
    # via target linking.
    #
    # idf::bt is listed explicitly: the REQUIRES above only covers the component
    # library (${COMPONENT_LIB}), not this plain target, and the port's BLE
    # sources need the NimBLE headers (host/ble_gap.h, nimble/nimble_port.h, ...)
    # and the esp_vhci / controller symbols. When CONFIG_BT_ENABLED is off,
    # idf::bt is an empty interface target, so the unconditional link is safe.
    target_link_libraries(tuyaopen-port PRIVATE
        idf::esp_wifi idf::esp_event idf::esp_netif idf::nvs_flash
        idf::lwip idf::esp_timer idf::app_update idf::esp_partition idf::bt)
else()
    add_library(tuyaopen-port INTERFACE)
    if(_port_incs)
        target_include_directories(tuyaopen-port INTERFACE ${_port_incs})
    endif()
    message(WARNING "tuyaopen-port has no C sources; no TKL implementation will be linked")
endif()

# Self-inject the port headers (including the static tuya_kconfig.h) into core's
# environment base
if(TARGET tuyaopen-headers)
    target_include_directories(tuyaopen-headers INTERFACE ${_port_incs})
endif()

# The port is linked by target name. esp-idf component ordering (REQUIRES
# tuyaopen-core and bt) places the core libraries, the port archive and the
# esp-idf dependencies in link order.
target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-port)

unset(_port_srcs)
unset(_port_incs)
unset(_port_hdrs)
unset(_port_hdr)
unset(_port_hdr_dir)
unset(PORT_ROOT)
