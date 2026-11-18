# KCP port (CORE port layer).
#
# Upstream KCP is vendored in p2p-core/third_party/kcp with the TuyaOpen
# mbuf/CUBIC/pacing changes applied in-tree: no submodule, no build-time
# staging and no patch files. This file only lists the sources and include
# directories for base_ice.
#
# Included (not add_subdirectory'd) by p2p-core/base_ice/CMakeLists.txt so the
# variables stay in the caller's scope.

if(NOT DEFINED KCP_UPSTREAM_DIR)
    # p2p-core/third_party/port/kcp -> p2p-core/third_party/kcp
    set(KCP_UPSTREAM_DIR "${CMAKE_CURRENT_LIST_DIR}/../../kcp")
endif()

set(_kcp_upstream_dir "${KCP_UPSTREAM_DIR}")
if(NOT EXISTS "${_kcp_upstream_dir}/ikcp.c" OR
   NOT EXISTS "${_kcp_upstream_dir}/ikcp.h")
    message(FATAL_ERROR
        "KCP sources not found: ${_kcp_upstream_dir}\n"
        "p2p-core/third_party/kcp is vendored in-tree; restore it from the repository.")
endif()

set(_kcp_port_dir "${CMAKE_CURRENT_LIST_DIR}")

set(KCP_PORT_SOURCES
    "${_kcp_upstream_dir}/ikcp.c"
    "${_kcp_port_dir}/ikcp_cong.c"
    "${_kcp_port_dir}/ikcp_minmax.c"
    "${_kcp_port_dir}/ikcp_pacing.c")

set(KCP_PORT_INCLUDE_DIRS
    "${_kcp_upstream_dir}"
    "${_kcp_port_dir}/include")
