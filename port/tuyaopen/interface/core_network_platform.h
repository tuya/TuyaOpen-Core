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

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#if !defined(LWIP_TIMEVAL_PRIVATE) || !LWIP_TIMEVAL_PRIVATE
#include <sys/time.h>
#endif

/*
 * On this port fcntl() maps to lwip_fcntl(), which tests lwIP's O_NONBLOCK
 * bit (1). newlib's <fcntl.h> defines O_NONBLOCK as 0x4000, so the core must
 * not use the libc flag value here.
 */
#define CORE_NET_O_NONBLOCK 1

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

static inline int core_net_platform_set_timeout(int fd, int optname, int ms_timeout)
{
    struct timeval timeout = {ms_timeout / 1000, (ms_timeout % 1000) * 1000};
    return setsockopt(fd, SOL_SOCKET, optname, (const char *)&timeout, sizeof(timeout));
}

#else

/* lwIP has no SO_BINDTODEVICE equivalent */
static inline int core_net_platform_bind_interface(int fd, TUYA_IP_ADDR_T addr)
{
    (void)fd;
    (void)addr;

    return 0;
}

/*
 * lwIP takes an int (ms) for SO_RCVTIMEO/SO_SNDTIMEO when built with
 * LWIP_SO_SNDRCVTIMEO_NONSTANDARD=1, and a struct timeval otherwise.
 * LWIP_SO_SNDRCVTIMEO_NONSTANDARD (or an explicit CORE_NET_SO_TIMEOUT_MS_INT
 * build override for platforms whose minimal lwipopts.h view differs from
 * the linked library) must match the lwIP archive actually linked.
 */
#if defined(CORE_NET_SO_TIMEOUT_MS_INT)
#define CORE_NET_PLATFORM_SO_TIMEOUT_MS_INT CORE_NET_SO_TIMEOUT_MS_INT
#elif defined(LWIP_SO_SNDRCVTIMEO_NONSTANDARD) && LWIP_SO_SNDRCVTIMEO_NONSTANDARD
#define CORE_NET_PLATFORM_SO_TIMEOUT_MS_INT 1
#else
#define CORE_NET_PLATFORM_SO_TIMEOUT_MS_INT 0
#endif

static inline int core_net_platform_set_timeout(int fd, int optname, int ms_timeout)
{
#if CORE_NET_PLATFORM_SO_TIMEOUT_MS_INT
    int timeout = ms_timeout;
    return setsockopt(fd, SOL_SOCKET, optname, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval timeout = {ms_timeout / 1000, (ms_timeout % 1000) * 1000};
    return setsockopt(fd, SOL_SOCKET, optname, (const char *)&timeout, sizeof(timeout));
#endif
}

#endif

#endif /* __CORE_NETWORK_PLATFORM_H__ */
