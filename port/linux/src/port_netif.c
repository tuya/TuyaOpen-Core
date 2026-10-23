/**
 * @file port_netif.c
 * @brief Local network interface information (POSIX ioctl): IP and link-status callback.
 *
 * Works for any non-loopback interface (wlan0 and eth0 alike); sockets are not
 * adapted here: core_network.c calls POSIX directly through
 * interface/core_network_platform.h.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "core_netif_ops.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static CORE_NETIF_LINK_CHANGE_CB s_netif_link_cb;
static pthread_t s_netif_link_thread;

/* Loopback interfaces are not network links */
static int is_loopback_interface(const char *if_name)
{
    if (if_name == NULL) {
        return 0;
    }

    return (strcmp(if_name, "lo") == 0 || strncmp(if_name, "loopback", 8) == 0);
}

/* Get the link status of one interface (ioctl SIOCGIFFLAGS) */
static OPERATE_RET __netif_get_status_by_name(const char *if_name, CORE_NETIF_LINK_STAT_E *status)
{
    int sockfd;
    struct ifreq ifr;

    memset((void *)&ifr, 0, sizeof(ifr));

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return OPRT_SOCK_ERR;
    }

    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
    if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
        perror("ioctl");
        close(sockfd);
        return OPRT_COM_ERROR;
    }

    *status = (ifr.ifr_flags & IFF_UP) ? CORE_NETIF_LINK_UP : CORE_NETIF_LINK_DOWN;
    close(sockfd);

    return OPRT_OK;
}

/* Walk non-loopback interfaces; any one being up means the link is up */
static OPERATE_RET __netif_get_status(CORE_NETIF_LINK_STAT_E *status)
{
    struct if_nameindex *name_list;
    int i;

    *status = CORE_NETIF_LINK_DOWN;

    name_list = if_nameindex();
    if (NULL == name_list) {
        return OPRT_NOT_FOUND;
    }

    for (i = 0; name_list[i].if_index != 0; ++i) {
        if (is_loopback_interface(name_list[i].if_name))
            continue;

        CORE_NETIF_LINK_STAT_E if_status = CORE_NETIF_LINK_DOWN;
        if (!__netif_get_status_by_name(name_list[i].if_name, &if_status)) {
            *status = if_status;
            if (if_status == CORE_NETIF_LINK_UP) {
                break;
            }
        }
    }

    if_freenameindex(name_list);
    return OPRT_OK;
}

/* Link status polling thread: every 3s, invoke the callback on change */
static void *netif_link_event_cb(void *arg)
{
    CORE_NETIF_LINK_STAT_E last_stat = -1;
    CORE_NETIF_LINK_STAT_E stat = CORE_NETIF_LINK_DOWN;

    (void)arg;
    pthread_detach(pthread_self());
    while (1) {
        __netif_get_status(&stat);
        if (stat != last_stat) {
            s_netif_link_cb(stat);
            last_stat = stat;
        }

        sleep(3);
    }

    return NULL;
}

/* Get the IP/netmask of one interface (ioctl SIOCGIFADDR / SIOCGIFNETMASK) */
static OPERATE_RET __netif_get_ip_by_name(const char *if_name, NW_IP_S *ip)
{
    int sock;
    struct ifreq ifr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        printf("socket create failse...GetLocalIp!\n");
        return OPRT_COM_ERROR;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
    if (ioctl(sock, SIOCGIFADDR, &ifr) < 0) {
        close(sock);
        return OPRT_COM_ERROR;
    }
    strncpy(ip->ip, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr), sizeof(ip->ip));

    if (ioctl(sock, SIOCGIFNETMASK, &ifr) < 0) {
        close(sock);
        return OPRT_COM_ERROR;
    }
    strncpy(ip->mask, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr), sizeof(ip->mask));

    close(sock);
    return OPRT_OK;
}

static OPERATE_RET __netif_get_ip(NW_IP_S *ip)
{
    OPERATE_RET rt = OPRT_NOT_FOUND;
    struct if_nameindex *name_list;
    int i;

    name_list = if_nameindex();
    if (NULL == name_list) {
        return OPRT_NOT_FOUND;
    }

    for (i = 0; name_list[i].if_index != 0; ++i) {
        if (is_loopback_interface(name_list[i].if_name))
            continue;

        CORE_NETIF_LINK_STAT_E if_status = CORE_NETIF_LINK_DOWN;
        if (!__netif_get_status_by_name(name_list[i].if_name, &if_status) && if_status == CORE_NETIF_LINK_UP) {
            if (!__netif_get_ip_by_name(name_list[i].if_name, ip)) {
                rt = OPRT_OK;
                break;
            }
        }
    }

    if_freenameindex(name_list);

    return rt;
}

static OPERATE_RET __netif_set_link_cb(CORE_NETIF_LINK_CHANGE_CB cb)
{
    if (cb) {
        CORE_NETIF_LINK_STAT_E status = CORE_NETIF_LINK_DOWN;

        __netif_get_status(&status);

        s_netif_link_cb = cb;
        if (!s_netif_link_thread) {
            int ret = pthread_create(&s_netif_link_thread, NULL, netif_link_event_cb, NULL);
            if (OPRT_OK != ret) {
                return ret;
            }
        }

        cb(status);
    }

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Platform ops card: assembled here, referenced by core's core_netif.c for
 * core_netif_* dispatch.
 * --------------------------------------------------------------------------- */
CORE_NETIF_OPS_T core_netif_ops_platform = {
    .get_ip = __netif_get_ip,
    .set_link_cb = __netif_set_link_cb,
};
