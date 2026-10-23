set(_pj_incs
    ${CMAKE_CURRENT_SOURCE_DIR}/pjlib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/pjlib-util/include
    ${CMAKE_CURRENT_SOURCE_DIR}/pjmedia/include
    ${CMAKE_CURRENT_SOURCE_DIR}/pjnath/include
)

file(GLOB_RECURSE _pj_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib-util/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjmedia/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjnath/src/*.c"
)

list(REMOVE_ITEM _pj_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/os_rwmutex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/ioqueue_common_abs.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib-util/src/pjlib-util/scanner_cis_bitwise.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib-util/src/pjlib-util/scanner_cis_uint.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/os_core_unix.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/ioqueue_epoll.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/sock_bsd.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/sock_select.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/addr_resolv_sock.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/ip_helper_generic.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/ssl_sock_ossl.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/ssl_sock_gtls.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/ssl_sock_dump.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/ssl_sock_imp_common.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/ssl_sock_common.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pjlib/src/pj/os_time_bsd.c"
)

add_library(pjproject STATIC)
target_sources(pjproject PRIVATE ${_pj_srcs})
target_include_directories(pjproject PUBLIC ${_pj_incs})
target_compile_options(pjproject PRIVATE -w)
target_compile_definitions(pjproject PRIVATE ${_p2p_pj_defines})
set(_pj_deps tuyaopen-headers core_system)
# pj_tuyaos_local_ip.c includes core_netif.h under ENABLE_WIRED: core_netif is part
# of iot-core/core_system (built into core_system; its headers propagate through the
# PUBLIC include), so there is no separate core_netif target.
target_link_libraries(pjproject PRIVATE ${_pj_deps})
unset(_pj_deps)
unset(_pj_srcs)
unset(_pj_incs)
