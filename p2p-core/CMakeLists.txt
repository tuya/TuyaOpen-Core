if(NOT CONFIG_ENABLE_TUYA_P2P)
    return()
endif()

# libre and base_ice compile the pj_ice.h adapter surface; both need the pj configuration
# defines, with endianness driven by CONFIG_LITTLE_END.
if(CONFIG_LITTLE_END)
    set(_p2p_pj_defines PJ_IS_LITTLE_ENDIAN=1 PJ_IS_BIG_ENDIAN=0)
else()
    set(_p2p_pj_defines PJ_IS_LITTLE_ENDIAN=0 PJ_IS_BIG_ENDIAN=1)
endif()
list(APPEND _p2p_pj_defines PJ_TUYAOS=1)

# Warning suppressions for the p2p subtree, applied per target in the
# subdirectories below.
set(_p2p_warn_opts
    -Wno-unused-const-variable
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-missing-field-initializers
    -Wno-comment
    # Some toolchains define uint32_t as long; the existing %u format strings
    # are valid for the value but not for the type. Keep the diagnostics visible
    # without failing the build.
    -Wno-error=format
)

# The P2P ICE/STUN/TURN stack is the vendored libre (baresip/re) subset; there
# is no backend selection. libre_ice.c provides the pj_ice.h adapter surface
# consumed by base_ice.
# libre is included before base_ice so its Threads::Threads dependency is
# visible when base_ice's link interface is created.
# libre's struct sa always embeds sockaddr_in6. The platform's lwIP header
# view must define LWIP_IPV6 so the type is visible.

# P2P large-buffer placement is left to each platform's allocator policy;
# core_p2p_malloc() uses the normal core heap. A port that wants KCP mbuf
# payloads in external memory configures its allocator threshold accordingly.
# libre (the P2P ICE/STUN/TURN stack). re.cmake owns the sources and the
# platform macros live in third_party/port/re/include/re_platform_config.h; nothing here
# depends on the platform name.
include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/port/re/re.cmake)

add_subdirectory(base_ice)
add_subdirectory(lib_rtp)
add_subdirectory(svc_ipc_core)
add_subdirectory(svc_streaming_p2p)
add_subdirectory(local_store)
add_subdirectory(svc_ring_buffer)

unset(_p2p_pj_defines)
unset(_p2p_warn_opts)
