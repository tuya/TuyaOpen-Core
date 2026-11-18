# libre (baresip/re) build selection for the TuyaOpen P2P ICE/STUN/TURN stack.
#
# Upstream source is vendored in p2p-core/third_party/re and patched in-tree: the
# TuyaOpen changes (triggered PRFLX pair, early conclusion, STUN cookie
# validation, MCU/ESP-IDF portability) live directly in the vendored files, so
# there is no submodule and no build-time patch staging. This file selects the
# P2P subset explicitly instead of using libre's CMakeLists: upstream has no
# "ICE/STUN/TURN only" option and its default list also pulls
# SIP/SDP/RTP/SRTP/HTTP/DNS/RTMP. Linux host builds use libre's POSIX platform
# sources; the MCU/lwIP port replaces those files (see platform/).
#
# Usage:
#   set(TUYAOPEN_THIRD_PARTY_DIR <repo>/third_party)   # optional fallback below
#   include(<repo>/p2p-core/third_party/port/re/re.cmake)
#   target_link_libraries(<app> PRIVATE libre_p2p)
#
# The single target is named libre_p2p to avoid clashing with upstream's `re`
# target when a host also builds p2p-core/third_party/re through its own CMake.

if(TARGET libre_p2p)
    return()
endif()

if(NOT DEFINED RE_UPSTREAM_DIR)
    # p2p-core/third_party/port/re -> p2p-core/third_party/re
    set(RE_UPSTREAM_DIR "${CMAKE_CURRENT_LIST_DIR}/../../re")
endif()
if(NOT EXISTS "${RE_UPSTREAM_DIR}/include/re.h")
    message(FATAL_ERROR
        "libre sources not found: ${RE_UPSTREAM_DIR}\n"
        "p2p-core/third_party/re is vendored in-tree; restore it from the repository.")
endif()

# P2P subset. Deliberately excluded:
#   src/dns/*              (P2P uses IP literals; see resource analysis)
#   src/ice/icesdp.c       (kept for now: ice_sdp_decode/icem_sdp_decode are
#                           the only public way to set remote ufrag/pwd; the
#                           adapter can drop this later with an internal shim)
#   SIP/SDP/RTP/SRTP/HTTP/RTMP/PCP/BFCP/REM
#
# icesdp.c's mDNS branch calls re_thread_async(); async.c is therefore linked,
# but no async worker is created unless a candidate carries a .local domain.
set(RE_P2P_SOURCES
    # main / event loop
    src/main/init.c
    src/main/method.c
    src/main/main.c

    # memory / buffers / containers / timers
    src/mem/mem.c
    src/mem/mem_pool.c
    src/mem/secure.c
    src/mbuf/mbuf.c
    src/list/list.c
    src/tmr/tmr.c
    src/mqueue/mqueue.c
    src/hash/hash.c
    src/hash/func.c
    src/btrace/btrace.c

    # formatting / diagnostics (re_printf is used by the STUN/ICE traces)
    src/fmt/ch.c
    src/fmt/hexdump.c
    src/fmt/pl.c
    src/fmt/print.c
    src/fmt/prm.c
    src/fmt/regex.c
    src/fmt/str.c
    src/fmt/str_error.c
    src/fmt/text2pcap.c
    src/fmt/time.c
    src/fmt/unicode.c
    src/dbg/dbg.c

    # addresses / sockets / networking
    src/sa/sa.c
    src/sa/printaddr.c
    src/udp/udp.c
    src/tcp/tcp.c
    src/tcp/tcp_high.c
    src/net/if.c
    src/net/net.c
    src/net/netstr.c
    src/net/rt.c
    src/net/sock.c
    src/net/sockopt.c

    # threads / sys
    src/thread/thread.c
    src/thread/posix.c
    src/sys/endian.c
    src/sys/fs.c
    src/sys/sys.c

    # Crypto needed by STUN integrity. libre v4.12.0 has no built-in
    # SHA/MD5/HMAC backend: the wrappers select OpenSSL, mbedTLS, Apple
    # CommonCrypto or Windows CNG at compile time. TuyaOpen builds (Linux host
    # and MCU) use the bundled/platform mbedtls target; the standalone
    # host build falls back to OpenSSL. Backend sources are appended
    # below.
    src/md5/wrap.c
    src/sha/wrap.c
    src/hmac/hmac_sha1.c
    src/crc32/crc32.c
    src/base64/b64.c

    # STUN
    src/stun/addr.c
    src/stun/attr.c
    src/stun/ctrans.c
    src/stun/ind.c
    src/stun/keepalive.c
    src/stun/msg.c
    src/stun/rep.c
    src/stun/req.c
    src/stun/stun.c
    src/stun/stunstr.c
    src/stun/hdr.c

    # ICE
    src/ice/cand.c
    src/ice/candpair.c
    src/ice/comp.c
    src/ice/connchk.c
    src/ice/icem.c
    src/ice/icesdp.c
    src/ice/icestr.c
    src/ice/util.c
    src/ice/stunsrv.c
    src/ice/chklist.c

    # TURN client (UDP/TCP transport; TURN over TLS is out of scope)
    src/turn/chan.c
    src/turn/perm.c
    src/turn/turnc.c

    # only used by the mDNS branch of icesdp.c; created lazily, never by us
    src/async/async.c
)

list(TRANSFORM RE_P2P_SOURCES PREPEND "${RE_UPSTREAM_DIR}/")

# Port sources. Every TuyaOpen build (host and MCU) uses the same three port
# files on top of core_netif/core_thread/core_system; all platform differences
# are decided at compile time in include/re_platform_config.h. The standalone
# host build has no TuyaOpen core and keeps libre's upstream POSIX sources.
if(TARGET core_system)
    set(RE_HAVE_CORE ON)
    list(APPEND RE_P2P_SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/src/re_net_tuyaopen.c"
        "${CMAKE_CURRENT_LIST_DIR}/src/re_sys_tuyaopen.c"
        "${CMAKE_CURRENT_LIST_DIR}/src/re_pthread.c")
    # Replaced by the port: lwIP has no fcntl-based sockopt, and the P2P subset
    # does not use libre's file API.
    list(REMOVE_ITEM RE_P2P_SOURCES
        "${RE_UPSTREAM_DIR}/src/net/sockopt.c"
        "${RE_UPSTREAM_DIR}/src/sys/fs.c")
else()
    set(RE_HAVE_CORE OFF)
    list(APPEND RE_P2P_SOURCES
        "${RE_UPSTREAM_DIR}/src/net/posix/pif.c"
        "${RE_UPSTREAM_DIR}/src/net/ifaddrs.c"
        "${RE_UPSTREAM_DIR}/src/net/linux/rt.c"
        "${RE_UPSTREAM_DIR}/src/net/linux/addrs.c"
        "${RE_UPSTREAM_DIR}/src/sys/rand.c"
        "${RE_UPSTREAM_DIR}/src/sys/sleep.c")
endif()

# Crypto backend. This is explicit: TuyaOpen builds (host and MCU) must use
# mbedTLS; only the standalone host build explicitly requests OpenSSL. Do not
# fall back silently when a TuyaOpen build lacks an mbedtls target - that would
# put OpenSSL into an MCU/cross build.
if(NOT DEFINED RE_P2P_CRYPTO)
    set(RE_P2P_CRYPTO "mbedtls")
endif()
string(TOLOWER "${RE_P2P_CRYPTO}" RE_P2P_CRYPTO)

if(RE_P2P_CRYPTO STREQUAL "mbedtls")
    if(NOT TARGET mbedtls)
        message(FATAL_ERROR
            "libre: RE_P2P_CRYPTO=mbedtls but no mbedtls target exists. "
            "Build through tuyaopen-core (third_party adds it) or provide the "
            "platform mbedtls target; do not use OpenSSL for cross builds.")
    endif()

    # The bundled config (third_party/mbedtls + tuya_tls_config.h) is checked
    # here. A platform-provided mbedtls target owns its config; the libre
    # wrappers still compile-time #error if MBEDTLS_MD_C/SHA1_C/MD5_C are off.
    set(_re_mbed_cfg
        "${CMAKE_CURRENT_LIST_DIR}/../../../../iot-core/tuya_transport/config/tuya_tls_config.h")
    if(CONFIG_ENABLE_PLATFORM_MBEDTLS)
        # The platform owns its config; libre's wrappers still #error at
        # compile time if MBEDTLS_MD_C/SHA1_C/MD5_C are off.
        message(STATUS
            "libre: platform mbedtls target; ensure its config enables "
            "MBEDTLS_MD_C/SHA1_C/MD5_C")
    elseif(EXISTS "${_re_mbed_cfg}")
        file(STRINGS "${_re_mbed_cfg}" _re_mbed_hash_defs
             REGEX "^#define MBEDTLS_(MD_C|SHA1_C|MD5_C)([ \t]|$)")
        list(LENGTH _re_mbed_hash_defs _re_mbed_hash_count)
        if(_re_mbed_hash_count LESS 3)
            message(FATAL_ERROR
                "libre: ${_re_mbed_cfg} must enable MBEDTLS_MD_C, "
                "MBEDTLS_SHA1_C and MBEDTLS_MD5_C for STUN/TURN integrity")
        endif()
        unset(_re_mbed_hash_defs)
        unset(_re_mbed_hash_count)
    else()
        message(FATAL_ERROR
            "libre: bundled mbedtls target is present but ${_re_mbed_cfg} "
            "was not found")
    endif()
    unset(_re_mbed_cfg)

    list(APPEND RE_P2P_SOURCES
        "${RE_UPSTREAM_DIR}/src/hmac/hmac.c"
        "${RE_UPSTREAM_DIR}/src/aes/stub.c"
    )
    set(RE_P2P_CRYPTO_DEFS USE_MBEDTLS)
    set(RE_P2P_CRYPTO_LIBS mbedtls)
elseif(RE_P2P_CRYPTO STREQUAL "openssl")
    # Host-only escape hatch for standalone host builds. Never select
    # this for an MCU/cross build.
    find_package(OpenSSL REQUIRED)
    list(APPEND RE_P2P_SOURCES
        "${RE_UPSTREAM_DIR}/src/hmac/openssl/hmac.c"
        "${RE_UPSTREAM_DIR}/src/aes/openssl/aes.c"
        "${RE_UPSTREAM_DIR}/src/main/openssl.c"
    )
    set(RE_P2P_CRYPTO_DEFS USE_OPENSSL USE_OPENSSL_AES USE_OPENSSL_HMAC)
    set(RE_P2P_CRYPTO_LIBS OpenSSL::Crypto)
else()
    message(FATAL_ERROR
        "libre: RE_P2P_CRYPTO must be 'mbedtls' or 'openssl', got "
        "'${RE_P2P_CRYPTO}'")
endif()
message(STATUS "libre ICE crypto backend: ${RE_P2P_CRYPTO}")

# Common libre definitions. Platform-specific ones (Linux/POSIX versus
# TuyaOpen/lwIP versus ESP-IDF) are supplied by the re_platform interface
# target selected through RE_PLATFORM_CMAKE. HAVE_THREADS (C11 threads) is
# intentionally not set: thread/posix.c is used.
set(RE_P2P_DEFINITIONS
    _GNU_SOURCE
    RE_VERSION="4.12.0"
    VER_MAJOR=4
    VER_MINOR=12
    VER_PATCH=0
    HAVE_ATOMIC
    HAVE_SELECT
    HAVE_SELECT_H
    HAVE_SYS_TIME_H
    HAVE_UNISTD_H
    HAVE_PTHREAD
    ${RE_P2P_CRYPTO_DEFS}
    $<$<NOT:$<CONFIG:Debug>>:RELEASE>
)

add_library(libre_p2p STATIC ${RE_P2P_SOURCES})
target_include_directories(libre_p2p
    PUBLIC $<BUILD_INTERFACE:${RE_UPSTREAM_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
    PRIVATE ${RE_UPSTREAM_DIR}/src/ice ${RE_UPSTREAM_DIR}/src/stun
            ${RE_UPSTREAM_DIR}/src/main
)
target_compile_definitions(libre_p2p PRIVATE ${RE_P2P_DEFINITIONS})
# Upstream is built warning-clean; our target is not the place to enforce its
# warning policy. Keep warnings off to avoid drowning the P2P build.
target_compile_options(libre_p2p PRIVATE -w)
# The platform config must be in effect before any system header: upstream
# files gate #includes on HAVE_* (e.g. init.c includes <signal.h> under
# HAVE_SIGNAL). Force-include it into libre's own sources; PRIVATE so it does
# not leak into targets that merely link libre.
target_compile_options(libre_p2p PRIVATE
    -include "${CMAKE_CURRENT_LIST_DIR}/include/re_platform_config.h")
if(RE_HAVE_CORE)
    target_link_libraries(libre_p2p PUBLIC core_system ${RE_P2P_CRYPTO_LIBS})
else()
    find_package(Threads REQUIRED)
    target_compile_definitions(libre_p2p PRIVATE RE_STANDALONE=1)
    target_link_libraries(libre_p2p PUBLIC Threads::Threads m ${RE_P2P_CRYPTO_LIBS})
endif()
# mbedTLS's tuya_tls_config.h includes the port's tuya_iot_config.h; the
# mbedtls target links tuyaopen-headers privately, so propagate the port
# include paths here as well.
if(TARGET tuyaopen-headers)
    target_link_libraries(libre_p2p PRIVATE tuyaopen-headers)
endif()

unset(RE_P2P_SOURCES)
unset(RE_P2P_DEFINITIONS)
unset(RE_P2P_CRYPTO_DEFS)
unset(RE_P2P_CRYPTO_LIBS)
unset(RE_HAVE_CORE)
