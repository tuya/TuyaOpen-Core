# Official library directory: everything is a git submodule / official source
# (official files are unmodified).
# Each library is registered as an independent target; the in-house connectivity
# layer (iot-core/tuya_transport) links them and injects transport_interface.h and
# user-provided configuration header paths into the official compile units
# (core_http_config.h / core_mqtt_config.h / tuya_tls_config.h, all under
#  iot-core/tuya_transport/config/, never inside the official directories).

# (TUYAOPEN_THIRD_PARTY_DIR is defined at top level by tuyaopen-core.cmake)

# In-house KV storage (legacy; still kept here, not an official library)

# cJSON (submodule, v1.7.18): single-file library, target name kept as libcjson
add_library(libcjson STATIC ${CMAKE_CURRENT_SOURCE_DIR}/cJSON/cJSON.c)
target_include_directories(libcjson PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cJSON>
)
target_link_libraries(libcjson PRIVATE tuyaopen-headers)

# coreHTTP (submodule, v3.1.3): core client + official parser llhttp (nested submodule)
add_library(corehttp STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/core_http_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/dependency/3rdparty/llhttp/src/api.c
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/dependency/3rdparty/llhttp/src/llhttp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/dependency/3rdparty/llhttp/src/http.c
)
target_include_directories(corehttp
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/dependency/3rdparty/llhttp/include
)

# coreMQTT (submodule, v5.0.2)
file(GLOB _coremqtt_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/coreMQTT/source/*.c"
)
add_library(coremqtt STATIC ${_coremqtt_srcs})
target_include_directories(coremqtt
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/coreMQTT/source/include>
)
unset(_coremqtt_srcs)

# backoffAlgorithm (official AWS v1.3.0): cloud connection retry backoff
add_library(backoff_algorithm STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/backoffAlgorithm/source/backoff_algorithm.c
)
target_include_directories(backoff_algorithm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backoffAlgorithm/source/include>
)

# mbedtls (submodule, v3.1.0): whole library built (library/*.c).
# Configuration uses the official override mechanism MBEDTLS_CONFIG_FILE ->
# tuya_transport/config/tuya_tls_config.h; when CONFIG_ENABLE_PLATFORM_MBEDTLS=ON
# the platform provides it and this target is not registered.
if(NOT CONFIG_ENABLE_PLATFORM_MBEDTLS)
    file(GLOB _mbedtls_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/*.c"
    )
    add_library(mbedtls STATIC ${_mbedtls_srcs})
    target_include_directories(mbedtls
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library
    )
    target_compile_definitions(mbedtls PUBLIC MBEDTLS_CONFIG_FILE=\"tuya_tls_config.h\")
    # threading_alt.h (THREADING_ALT implementation of the trimmed config) pulls in CORE headers such as core_mutex
    target_link_libraries(mbedtls PRIVATE tuyaopen-headers core_system)
    unset(_mbedtls_srcs)
else()
    message(STATUS "Using mbedtls from platform")
endif()
