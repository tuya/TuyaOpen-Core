file(GLOB_RECURSE _core_system_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(core_system STATIC)
target_sources(core_system PRIVATE ${_core_system_srcs})
target_include_directories(core_system PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>

)
# core_api.h is an umbrella header that includes sibling module headers (the
# esp-idf REQUIRES semantics), so propagation must be PUBLIC.
target_link_libraries(core_system
    PUBLIC tuya_transport tuya_utilities
    PRIVATE tuyaopen-headers
)
unset(_core_system_srcs)
