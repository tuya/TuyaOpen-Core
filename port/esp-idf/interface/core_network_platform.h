/**
 * @file core_network_platform.h
 * @brief Platform socket API for core_network.c (ESP32/lwIP).
 *
 * core_network.c includes this header and calls the socket API directly.
 * ESP-IDF's lwip/sockets.h wrapper exposes the BSD names (socket/connect/
 * bind/listen/accept/send/recv/sendto/recvfrom/getsockopt/setsockopt/
 * shutdown); close/fcntl/select are provided by newlib's VFS layer. This
 * header is the only platform seam for the network layer.
 *
 * Layering: this header is part of the port's contract layer. It may include
 * tuya_cloud_types.h and platform headers only; it must not include any core
 * header.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __CORE_NETWORK_PLATFORM_H__
#define __CORE_NETWORK_PLATFORM_H__

#include "tuya_cloud_types.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>

/* On ESP-IDF, lwipopts.h includes <sys/select.h>, so fd_set is newlib's
 * 64-bit set (FD_SETSIZE=64) and TUYA_FD_SET_T is 8 bytes. lwIP socket fds
 * start at LWIP_SOCKET_OFFSET (54 with CONFIG_LWIP_MAX_SOCKETS=10), so the
 * size check below is the important invariant; the fd numbering itself is
 * handled by the stack. */
#define CORE_NET_NATIVE_FD_SET(fds) ((fd_set *)(fds))
typedef char core_net_fd_set_size_check_t[(sizeof(TUYA_FD_SET_T) >= sizeof(fd_set)) ? 1 : -1];

/**
 * @brief lwIP has no SO_BINDTODEVICE equivalent: keep the core bind path uniform
 *
 * @return always 0
 */
static inline int core_net_platform_bind_interface(int fd, TUYA_IP_ADDR_T addr)
{
    (void)fd;
    (void)addr;

    return 0;
}

#endif /* __CORE_NETWORK_PLATFORM_H__ */
