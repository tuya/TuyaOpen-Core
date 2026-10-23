# Connection service layer: provides TLS-enabled HTTP / MQTT / TLS connection
# interfaces upward, and depends downward only on the official third_party
# libraries and core_system.
# All in-house code is built into this single target: transport primitives
# (tcp/tls/websocket transporters), http/mqtt wrappers, the TLS session
# wrapper, and the security algorithms.

file(GLOB_RECURSE _tuya_transport_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tuya_transport STATIC)
target_sources(tuya_transport PRIVATE ${_tuya_transport_srcs})
# Four subdirectories under include/ mirroring src/; consumers still use
# #include "xxx.h" without a prefix
target_include_directories(tuya_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/transport>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/tls>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/http>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/mqtt>
    # User-provided config headers: expansion points for MBEDTLS_CONFIG_FILE /
    # core_http_config.h / core_mqtt_config.h
    # They are needed by more than the official libraries (this target and
    # consumers as well), so they must propagate with INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/config>
)

# Official library targets (registered by third_party) are linked to the
# in-house side; the mbedtls target may not exist due to CONFIG gating.
# PUBLIC: official library headers and symbols propagate to all consumers.
foreach(_tp_official corehttp coremqtt backoff_algorithm libcjson mbedtls)
    if(TARGET ${_tp_official})
        target_link_libraries(tuya_transport PUBLIC ${_tp_official})
    endif()
endforeach()
unset(_tp_official)

# Inject in-house header paths into the official library compile units
# (transport_interface.h, config headers) along with the CORE header
# dependencies (tuya_cloud_types.h, core_memory.h, etc.), keeping third_party
# as pure official definitions that do not reference in-house directories.
#
# mbedtls is injected only when core builds it itself
# (CONFIG_ENABLE_PLATFORM_MBEDTLS=OFF): when the platform provides mbedtls
# (esp-idf host), that target name is a host-side component/bridge target, and
# injecting core's config directory would make the host's mbedtls consumers
# (esp_common etc.) pick up core's threading_alt.h (which includes
# core_mutex.h) and fail to compile; the host's own mbedtls configuration
# system (esp_config.h + ROM version of threading_alt.h) is injected by the host.
foreach(_tp_official corehttp coremqtt)
    if(TARGET ${_tp_official})
        target_include_directories(${_tp_official} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        # The config headers (expansion points for core_http_config.h /
        # core_mqtt_config.h) propagate with INTERFACE: every module linking
        # the official libraries needs them; they are not private to this
        # target
        target_include_directories(${_tp_official} INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/config
        )
        target_link_libraries(${_tp_official} PRIVATE tuyaopen-headers core_system)
    endif()
endforeach()
unset(_tp_official)

if(TARGET mbedtls AND NOT CONFIG_ENABLE_PLATFORM_MBEDTLS)
    target_include_directories(mbedtls PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    # The config headers (expansion point for MBEDTLS_CONFIG_FILE) propagate
    # with INTERFACE: every module linking mbedtls needs them (e.g.
    # tuya_utilities uses mbedtls/base64.h)
    target_include_directories(mbedtls INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/config
    )
    target_link_libraries(mbedtls PRIVATE tuyaopen-headers core_system)
endif()

target_link_libraries(tuya_transport PRIVATE
    tuyaopen-headers
    core_system
)
# General utilities (crc/random/mix_method/dev_evt, etc.) propagate to
# consumers with this target as PUBLIC
target_link_libraries(tuya_transport PUBLIC tuya_utilities)
unset(_tuya_transport_srcs)
