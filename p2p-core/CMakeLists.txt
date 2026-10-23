if(NOT CONFIG_ENABLE_TUYA_P2P)
    return()
endif()

# pjproject and base_ice compile pj headers; both need the pj configuration
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
)

add_subdirectory(base_ice)
add_subdirectory(lib_rtp)
add_subdirectory(pjproject)
add_subdirectory(svc_ipc_core)
add_subdirectory(svc_streaming_p2p)
add_subdirectory(local_store)
add_subdirectory(svc_ring_buffer)

unset(_p2p_pj_defines)
unset(_p2p_warn_opts)
