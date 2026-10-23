file(GLOB_RECURSE _svc_ipc_core_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(svc_ipc_core STATIC)
target_sources(svc_ipc_core PRIVATE ${_svc_ipc_core_srcs})
target_include_directories(svc_ipc_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_options(svc_ipc_core PRIVATE ${_p2p_warn_opts})
target_link_libraries(svc_ipc_core PRIVATE tuyaopen-headers libcjson tuya_cloud_service base_ice svc_streaming_p2p)
unset(_svc_ipc_core_srcs)
