/**
 * @file re_platform_config.h
 * @brief libre platform selection, included from re_types.h.
 *
 * Included by every libre source before any system header, so the platform
 * macros are decided once here and the build needs no per-platform -D flags
 * and no platform cmake file. Selection uses compiler-provided macros plus
 * RE_STANDALONE (set by re.cmake when libre is built without the TuyaOpen
 * core, i.e. the standalone host build):
 *
 *   __linux__ + RE_STANDALONE  host test, libre's own POSIX platform sources
 *   __linux__                  host with core_netif/core_thread
 *   otherwise                  embedded target
 *
 * On embedded and core-based host builds the pthread functions libre calls are
 * implemented in p2p-core/third_party/port/re/src/re_pthread.c on top of core_thread/
 * core_mutex/core_semaphore and core_thread TLS.
 *
 * @copyright Copyright (c) 2026 Tuya Inc. All Rights Reserved.
 */
#ifndef RE_PLATFORM_CONFIG_H
#define RE_PLATFORM_CONFIG_H

#if defined(__linux__) && defined(RE_STANDALONE)
/* ---------------------------------------------------------------------------
 * Standalone host test: libre's upstream POSIX platform sources are compiled
 * and the native pthread/time/network APIs are used.
 * ------------------------------------------------------------------------- */
#define LINUX 1
#define ARCH "x86_64"
#define OS "Linux"
#define HAVE_GETIFADDRS 1
#define HAVE_ROUTE_LIST 1
#define HAVE_EPOLL 1
#define HAVE_ACCEPT4 1
#define HAVE_PRCTL 1
#define HAVE_SETRLIMIT 1
#define HAVE_STRERROR_R 1
#define HAVE_STRINGS_H 1
#define HAVE_UNAME 1
#define HAVE_PWD_H 1
#define HAVE_FORK 1
#define HAVE_MEMRCHR 1
#define HAVE_SYSLOG 1
#define HAVE_GETOPT 1
#define HAVE_UNIXSOCK 1
#define HAVE_SIGNAL 1

#elif defined(__linux__)
/* ---------------------------------------------------------------------------
 * Host with the TuyaOpen core: the shared port sources are compiled, network
 * interfaces come from core_netif (no OS-specific enumeration), and the pthread
 * shim provides libre's threads.
 * ------------------------------------------------------------------------- */
#define ARCH "x86_64"
#define OS "Linux"
#define HAVE_EPOLL 1
#define HAVE_ACCEPT4 1
#define HAVE_PRCTL 1
#define HAVE_SETRLIMIT 1
#define HAVE_STRERROR_R 1
#define HAVE_STRINGS_H 1
#define HAVE_UNAME 1
#define HAVE_PWD_H 1
#define HAVE_FORK 1
#define HAVE_MEMRCHR 1
#define HAVE_SYSLOG 1
#define HAVE_GETOPT 1
#define HAVE_UNIXSOCK 1
#define HAVE_SIGNAL 1

#define RE_PTHREAD_SHIM 1

#else
/* ---------------------------------------------------------------------------
 * Embedded targets share this branch. Platform-specific pieces are
 * implementations, not macros: clock_gettime/pipe in the platform or port
 * library, gai_strerror in the port (weak where the C library lacks it).
 * ------------------------------------------------------------------------- */
#define OS "TuyaOpen"

/* Some C libraries expose _PTHREAD_ONCE_INIT; C11's once_flag (libre) uses the
 * POSIX name. Defined here because this header is force-included before
 * pthread.h. */
#ifndef PTHREAD_ONCE_INIT
#define PTHREAD_ONCE_INIT _PTHREAD_ONCE_INIT
#endif

/* Some C libraries only expose the POSIX mutex types with
 * _UNIX98_THREAD_MUTEX_ATTRIBUTES; libre initializes its recursive mutex with
 * the POSIX value 1. */
#ifndef PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_RECURSIVE 1
#endif

/* lwIP's compat/posix/netdb.h omits the POSIX size limits libre uses for its
 * service buffers. */
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif

/* Some lwIP integrations do not provide gai_strerror(); declare it here so
 * libre's debug paths compile. The embedded ports provide it (weak where
 * needed). */
#ifndef HAVE_GAI_STRERROR
const char *gai_strerror(int errcode);
#endif

/* Symbol renames: some platform stacks export the same names. Renaming inside
 * libre only keeps both users linkable. */
#define hmac_sha1 re_hmac_sha1
#define hmac_sha256 re_hmac_sha256
#define udp_send re_udp_send
#define udp_connect re_udp_connect
#define tcp_accept re_tcp_accept
#define tcp_connect re_tcp_connect

#define RE_PTHREAD_SHIM 1

#endif /* platform selection */

/* Timer clock for libre's jiffies. Some C libraries declare
 * CLOCK_MONOTONIC_RAW but reject it at runtime; others only define the raw
 * clock. Pin the monotonic clock where the raw clock is not usable; otherwise
 * keep tmr.c's upstream preference for the raw clock. */
#if defined(ESP_PLATFORM)
#ifndef RE_TIMER_CLOCK_ID
#define RE_TIMER_CLOCK_ID CLOCK_MONOTONIC
#endif
#endif

#if defined(RE_PTHREAD_SHIM)
/* Thread API */
#define pthread_create             re_pthread_create
#define pthread_join               re_pthread_join
#define pthread_detach             re_pthread_detach
#define pthread_exit               re_pthread_exit
#define pthread_self               re_pthread_self
#define pthread_equal              re_pthread_equal
#define pthread_setname_np         re_pthread_setname_np
#define pthread_once               re_pthread_once

/* Thread-specific data */
#define pthread_key_create         re_pthread_key_create
#define pthread_key_delete         re_pthread_key_delete
#define pthread_setspecific        re_pthread_setspecific
#define pthread_getspecific        re_pthread_getspecific

/* Mutex */
#define pthread_mutex_init         re_pthread_mutex_init
#define pthread_mutex_destroy      re_pthread_mutex_destroy
#define pthread_mutex_lock         re_pthread_mutex_lock
#define pthread_mutex_trylock      re_pthread_mutex_trylock
#define pthread_mutex_unlock       re_pthread_mutex_unlock
#define pthread_mutexattr_init     re_pthread_mutexattr_init
#define pthread_mutexattr_settype  re_pthread_mutexattr_settype
#define pthread_mutexattr_destroy  re_pthread_mutexattr_destroy

/* Condition variable */
#define pthread_cond_init          re_pthread_cond_init
#define pthread_cond_destroy       re_pthread_cond_destroy
#define pthread_cond_signal        re_pthread_cond_signal
#define pthread_cond_broadcast     re_pthread_cond_broadcast
#define pthread_cond_wait          re_pthread_cond_wait
#define pthread_cond_timedwait     re_pthread_cond_timedwait

#endif /* RE_PTHREAD_SHIM */

#endif /* RE_PLATFORM_CONFIG_H */
