file(GLOB_RECURSE _base_ice_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(base_ice STATIC)
target_sources(base_ice PRIVATE ${_base_ice_srcs})
target_include_directories(base_ice PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(base_ice PRIVATE NDEBUG ${_p2p_pj_defines})
target_compile_options(base_ice PRIVATE ${_p2p_warn_opts})
target_link_libraries(base_ice PRIVATE tuyaopen-headers libcjson core_system pjproject)
unset(_base_ice_srcs)
