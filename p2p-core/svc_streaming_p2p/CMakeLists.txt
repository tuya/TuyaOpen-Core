file(GLOB_RECURSE _svc_streaming_p2p_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(svc_streaming_p2p STATIC)
target_sources(svc_streaming_p2p PRIVATE ${_svc_streaming_p2p_srcs})
target_include_directories(svc_streaming_p2p PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_options(svc_streaming_p2p PRIVATE ${_p2p_warn_opts})
target_link_libraries(svc_streaming_p2p PRIVATE tuyaopen-headers libcjson tuya_cloud_service lib_rtp base_ice core_system tuya_transport)
# tuya_ai_service is conditional: this module's sources reference none of its
# symbols, while the target is gated by CONFIG_ENABLE_AI_COMPONENTS (the first
# line of agent-core/CMakeLists.txt returns early). An unconditional link would
# fail with "cannot find -ltuya_ai_service" when AI=OFF (the P2P and AI switches
# are not coupled). Link it when present, for future real references.
if(TARGET tuya_ai_service)
    target_link_libraries(svc_streaming_p2p PRIVATE tuya_ai_service)
endif()
unset(_svc_streaming_p2p_srcs)
