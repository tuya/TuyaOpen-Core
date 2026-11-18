/**
 * @file re_net_tuyaopen.c
 * @brief libre network-interface glue for TuyaOpen / lwIP platforms.
 *
 * The upstream host port enumerates interfaces with getifaddrs() and
 * OS-specific routing APIs. On TuyaOpen the platform owns the interface list:
 * core_netif_foreach() reports every IPv4 interface address, and ports with a
 * single-interface API report that one address. core_netif_get_ip() remains
 * the last-resort fallback. The TuyaOpen platform source set replaces the
 * upstream OS-specific net sources with this file and leaves the socket/ICE
 * code unchanged.
 *
 * @copyright Copyright (c) 2026 Tuya Inc. All Rights Reserved.
 */
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <re_types.h>
#include <re_fmt.h>
#include <re_mbuf.h>
#include <re_sa.h>
#include <re_net.h>

#include "core_netif.h"
#include "core_network.h"
#include "tuya_cloud_types.h"


#define RE_TUYAOPEN_IFNAME "sta"

/* NW_IP_S has two layouts (plain ip/mask/gw or an ip4/ip6 union selected at
 * build time). Prefer the nw*str macros when the platform defines them. */
static const char *__nw_ip_str(const NW_IP_S *ip)
{
#if defined(nwipstr)
    return ip->nwipstr;
#else
    return ip->ip;
#endif
}

struct if_list_ctx {
    net_ifaddr_h *ifh;
    void *arg;
};

/* core_netif_foreach callback: report IPv4 addresses only (P2P is IPv4). The
 * net_ifaddr_h contract stops the walk when the handler returns true. */
static bool __if_list_cb(const CORE_NETIF_ADDR_T *addr, void *user_data)
{
    struct if_list_ctx *ctx = user_data;
    struct sa sa;

    if (addr == NULL || ctx == NULL || ctx->ifh == NULL || addr->ip[0] == '\0') {
        return false;
    }
    if (sa_set_str(&sa, addr->ip, 0) != 0) {
        return false;
    }
    if (sa_af(&sa) != AF_INET) {
        return false;
    }

    return ctx->ifh(addr->name, &sa, ctx->arg);
}

/**
 * Enumerate the TuyaOpen local interface addresses.
 *
 * net_if_list() is the fallback used by net_if_apply()/net_if_getaddr() when
 * the upstream POSIX enumeration is not compiled in. Ports that can walk the
 * interface list report every IPv4 address; ports with a single-interface API
 * that one address. If the platform has no enumeration at all, fall back to
 * core_netif_get_ip(). Keep the handler contract: stop when it returns true.
 */
int net_if_list(net_ifaddr_h *ifh, void *arg)
{
    struct if_list_ctx ctx;
    NW_IP_S ip;
    struct sa sa;
    int err;

    if (ifh == NULL) {
        return EINVAL;
    }

    ctx.ifh = ifh;
    ctx.arg = arg;
    if (core_netif_foreach(__if_list_cb, &ctx) == OPRT_OK) {
        return 0;
    }

    memset(&ip, 0, sizeof(ip));
    if (core_netif_get_ip(&ip) != OPRT_OK || __nw_ip_str(&ip)[0] == '\0') {
        return ENODEV;
    }

    err = sa_set_str(&sa, __nw_ip_str(&ip), 0);
    if (err != 0) {
        return err;
    }
    if (sa_af(&sa) != AF_INET) {
        return EAFNOSUPPORT;
    }

    (void)ifh(RE_TUYAOPEN_IFNAME, &sa, arg);
    return 0;
}

/*
 * libre's sockopt.c uses fcntl(F_GETFL/F_SETFL). The port exposes the
 * operation through core_net_set_block(); reuse/v6only stay on the socket
 * setsockopt. src/net/sockopt.c is not compiled on any platform.
 */
int net_sockopt_blocking_set(re_sock_t fd, bool blocking)
{
    return (core_net_set_block(fd, blocking) == OPRT_OK) ? 0 : EINVAL;
}

int net_sockopt_reuse_set(re_sock_t fd, bool reuse)
{
    int r = reuse;

#ifdef SO_REUSEADDR
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &r, sizeof(r)) < 0) {
        return errno;
    }
#endif
    (void)fd;
    (void)reuse;
    return 0;
}

int net_sockopt_v6only(re_sock_t fd, bool only)
{
    int on = only;

#ifdef IPV6_V6ONLY
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) < 0) {
        return errno;
    }
#endif
    (void)fd;
    (void)only;
    return 0;
}
