/**
 * @file core_network_platform.h
 * @brief Platform socket API for core_network.c.
 *
 * LINUX uses the POSIX socket API; ESP32 and lwIP-based MCU platforms use the
 * lwIP socket API. This header is the only platform seam of the network layer.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __CORE_NETWORK_PLATFORM_H__
#define __CORE_NETWORK_PLATFORM_H__

#include "tuya_cloud_types.h"

#if defined(__linux__) && !defined(ESP_PLATFORM)

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

#else /* ESP32 / lwIP-based platforms */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#endif

#define CORE_NET_NATIVE_FD_SET(fds) ((fd_set *)(fds))
typedef char core_net_fd_set_size_check_t[(sizeof(TUYA_FD_SET_T) >= sizeof(fd_set)) ? 1 : -1];

#if defined(__linux__) && !defined(ESP_PLATFORM)

/* bind the socket to the interface owning addr; failure is ignored by the caller */
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
            ret = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, name_list[i].if_name, sizeof(ifr.ifr_name));
            break;
        }
    }

    close(sock_fd);
    if_freenameindex(name_list);

    return ret;
}

#else

/* lwIP has no SO_BINDTODEVICE equivalent */
static inline int core_net_platform_bind_interface(int fd, TUYA_IP_ADDR_T addr)
{
    (void)fd;
    (void)addr;

    return 0;
}

#endif

#endif /* __CORE_NETWORK_PLATFORM_H__ */
