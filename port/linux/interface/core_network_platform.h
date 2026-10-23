/**
 * @file core_network_platform.h
 * @brief Platform socket API for core_network.c (LINUX/POSIX).
 *
 * core_network.c includes this header and calls the socket API directly. This
 * header is the only platform seam for the network layer: it pulls in the
 * platform socket stack, provides the TUYA_FD_SET_T -> fd_set bridge and the
 * Linux-only "bind to the interface owning the source address" helper.
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

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>

/* TUYA_FD_MAX_COUNT is 1024 on LINUX, so TUYA_FD_SET_T and glibc's fd_set are
 * both 128 bytes. Keep the cast below guarded by a compile-time size check. */
#define CORE_NET_NATIVE_FD_SET(fds) ((fd_set *)(fds))
typedef char core_net_fd_set_size_check_t[(sizeof(TUYA_FD_SET_T) >= sizeof(fd_set)) ? 1 : -1];

/**
 * @brief Best-effort bind of a socket to the interface owning @p addr
 *
 * Called by core_net_bind() after bind() succeeds on a non-ANY source address.
 * Failure is ignored by the caller (bind() keeps its result): on multi-NIC
 * hosts this pins outbound traffic to the interface that owns the active
 * address; SO_BINDTODEVICE requires CAP_NET_RAW and may fail in containers.
 *
 * @return 0 when the socket was bound to the matching interface; negative when
 *         no interface matched or on error
 */
static inline int core_net_platform_bind_interface(int fd, TUYA_IP_ADDR_T addr)
{
    struct if_nameindex *name_list;
    struct ifreq ifr;
    struct sockaddr_in *sin;
    int sock_fd;
    int ret = -1;
    int i;

    name_list = if_nameindex();
    if (NULL == name_list) {
        return -1;
    }

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        if_freenameindex(name_list);
        return -2;
    }

    for (i = 0; name_list[i].if_index != 0; i++) {
        if (strcmp(name_list[i].if_name, "lo") == 0) {
            continue;
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, name_list[i].if_name, sizeof(ifr.ifr_name) - 1);
        if (ioctl(sock_fd, SIOCGIFADDR, &ifr) < 0) {
            continue;
        }

        sin = (struct sockaddr_in *)&ifr.ifr_addr;
        if (sin->sin_addr.s_addr == htonl(addr)) {
            ret = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, name_list[i].if_name,
                             sizeof(ifr.ifr_name));
            break;
        }
    }

    close(sock_fd);
    if_freenameindex(name_list);

    return ret;
}

#endif /* __CORE_NETWORK_PLATFORM_H__ */
