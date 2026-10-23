file(GLOB_RECURSE _lib_rtp_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/payload/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rtpext/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(lib_rtp STATIC)
target_sources(lib_rtp PRIVATE ${_lib_rtp_srcs})
target_include_directories(lib_rtp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(lib_rtp PRIVATE NDEBUG)
target_compile_options(lib_rtp PRIVATE ${_p2p_warn_opts})
target_link_libraries(lib_rtp PRIVATE tuyaopen-headers)
unset(_lib_rtp_srcs)
