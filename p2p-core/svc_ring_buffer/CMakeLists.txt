if(NOT CONFIG_ENABLE_IPC_RING_BUFFER)
    return()
endif()

file(GLOB_RECURSE _ring_buf_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(svc_ring_buffer STATIC)
target_sources(svc_ring_buffer PRIVATE ${_ring_buf_srcs})
target_include_directories(svc_ring_buffer
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_options(svc_ring_buffer PRIVATE ${_p2p_warn_opts})
target_link_libraries(svc_ring_buffer PRIVATE tuyaopen-headers core_system svc_streaming_p2p)
unset(_ring_buf_srcs)
