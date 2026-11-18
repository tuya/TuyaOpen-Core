file(GLOB_RECURSE _base_ice_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

# KCP core is vendored in p2p-core/third_party/kcp and modified in-tree (third_party/port/kcp).
# Any leftover KCP source/header under base_ice would be compiled or shadow the
# vendored header: base_ice/include precedes p2p-core/third_party/kcp on the include
# path, so a stale include/ikcp*.h wins over the vendored header silently.
# Fail at configure time instead.
file(GLOB _base_ice_stale_kcp CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ikcp*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ikcp*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ikcp*.h")
if(_base_ice_stale_kcp)
    message(FATAL_ERROR
        "stale KCP sources/headers under base_ice/: ${_base_ice_stale_kcp}\n"
        "KCP is provided by p2p-core/third_party/kcp; remove them.")
endif()

# Defensive filter for files created after configure; the check above covers
# the normal case.
list(FILTER _base_ice_srcs EXCLUDE REGEX "/ikcp[^/]*\\.c$")

# ICE: libre_ice.c provides the pj_ice.h adapter surface on top of the
# vendored libre subset. A stale pj_ice.c must not shadow it.
list(FILTER _base_ice_srcs EXCLUDE REGEX "/pj_ice\\.c$")
list(APPEND _base_ice_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/port/re/src/libre_ice.c")

include("${CMAKE_CURRENT_SOURCE_DIR}/../third_party/port/kcp/kcp.cmake")

add_library(base_ice STATIC)
target_sources(base_ice PRIVATE ${_base_ice_srcs} ${KCP_PORT_SOURCES})
target_include_directories(base_ice
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${KCP_PORT_INCLUDE_DIRS}
)
target_include_directories(base_ice PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/port/re/include")
target_compile_definitions(base_ice PRIVATE CONFIG_P2P_ICE_LIBRE=1)
target_compile_definitions(base_ice PRIVATE NDEBUG ${_p2p_pj_defines})
target_compile_options(base_ice PRIVATE ${_p2p_warn_opts})
target_link_libraries(base_ice PRIVATE tuyaopen-headers libcjson core_system)
target_link_libraries(base_ice PRIVATE libre_p2p)
unset(_base_ice_srcs)
