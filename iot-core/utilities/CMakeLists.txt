# Generic utilities layer: platform-independent pure C utilities (list/mem heap/slist/
# tools/crc/random/mix_method/dev_evt/queue_template). Zero in-house dependencies --
# only tuyaopen-headers (port header base) and third_party (mbedtls base64); depended on
# publicly by core_system, so consumers reach it automatically.
file(GLOB_RECURSE _utilities_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(tuya_utilities STATIC)
target_sources(tuya_utilities PRIVATE ${_utilities_srcs})
target_include_directories(tuya_utilities PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(tuya_utilities PRIVATE tuyaopen-headers)
if(TARGET mbedtls)
    target_link_libraries(tuya_utilities PRIVATE mbedtls)
endif()
unset(_utilities_srcs)
