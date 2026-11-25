/**
 * @file core_network_ops.c
 * @brief CORE network platform dispatch: forwards core_net_* calls to the
 *        port's network ops card (core_network_ops_platform).
 *
 * Active when ENABLE_PLATFORM_NETWORK_OPS == 1:
 * all core_net_* calls are dispatched through the platform-provided
 * CORE_NETWORK_OPS_T instance (core_network_ops_platform, e.g. TKL tkl_net_*
 * on port/tuyaopen). Core does not include core_network_platform.h or any
 * lwIP/POSIX socket headers, and platform-specific hooks inside tkl_network.c
 * (such as cellular RRC release timers) remain active.
 *
 * When ENABLE_PLATFORM_NETWORK_OPS is 0 or undefined (default), this
 * translation unit is compiled out and core_network.c handles socket calls
 * directly via core_network_platform.h.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tuya_iot_config.h"

#if defined(ENABLE_PLATFORM_NETWORK_OPS) && (ENABLE_PLATFORM_NETWORK_OPS == 1)

#include "core_api.h"
#include "core_log.h"
#include "core_network.h"
#include "core_network_ops.h"
#include "core_system.h"
#include "dev_evt.h"

/* Active link source address: pushed by the link owner (application/port
 * layer) on link events */
static TUYA_IP_ADDR_T s_active_ipaddr = 0;

OPERATE_RET core_network_set_active_ip(TUYA_IP_ADDR_T ipaddr)
{
    s_active_ipaddr = ipaddr;

    return OPRT_OK;
}

TUYA_IP_ADDR_T core_network_get_active_ip(void)
{
    return s_active_ipaddr;
}

/* Platform ops card (defined by the port, e.g. tuyaopen-core_ops.c),
 * dispatched by core through the getter below. */
extern CORE_NETWORK_OPS_T core_network_ops_platform;

CORE_NETWORK_OPS_T *core_net_get_ops(void)
{
    return &core_network_ops_platform;
}

#define CORE_NET_EXEC_OP(op_name, default_ret, ...)                                                                    \
    do {                                                                                                               \
        CORE_NETWORK_OPS_T *ops = core_net_get_ops();                                                                  \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("CORE NET operation %s not available", #op_name);                                                       \
        return default_ret;                                                                                            \
    } while (0)

TUYA_ERRNO core_net_get_errno(void)
{
    CORE_NET_EXEC_OP(get_errno, UNW_FAIL);
}

OPERATE_RET core_net_fd_set(int fd, TUYA_FD_SET_T *fds)
{
    CORE_NET_EXEC_OP(fd_set, OPRT_NOT_SUPPORTED, fd, fds);
}

OPERATE_RET core_net_fd_clear(int fd, TUYA_FD_SET_T *fds)
{
    CORE_NET_EXEC_OP(fd_clear, OPRT_NOT_SUPPORTED, fd, fds);
}

OPERATE_RET core_net_fd_isset(int fd, TUYA_FD_SET_T *fds)
{
    CORE_NET_EXEC_OP(fd_isset, FALSE, fd, fds);
}

OPERATE_RET core_net_fd_zero(TUYA_FD_SET_T *fds)
{
    CORE_NET_EXEC_OP(fd_zero, OPRT_NOT_SUPPORTED, fds);
}

int core_net_select(const int maxfd, TUYA_FD_SET_T *readfds, TUYA_FD_SET_T *writefds, TUYA_FD_SET_T *errorfds,
                    const uint32_t ms_timeout)
{
    CORE_NET_EXEC_OP(select, UNW_FAIL, maxfd, readfds, writefds, errorfds, ms_timeout);
}

int core_net_get_nonblock(const int fd)
{
    CORE_NET_EXEC_OP(get_nonblock, UNW_FAIL, fd);
}

OPERATE_RET core_net_set_block(const int fd, const BOOL_T block)
{
    CORE_NET_EXEC_OP(set_block, OPRT_NOT_SUPPORTED, fd, block);
}

TUYA_ERRNO core_net_close(const int fd)
{
    CORE_NET_EXEC_OP(close, UNW_FAIL, fd);
}

TUYA_ERRNO core_net_shutdown(const int fd, const int how)
{
    CORE_NET_EXEC_OP(shutdown, UNW_FAIL, fd, how);
}

int core_net_socket_create(const TUYA_PROTOCOL_TYPE_E type)
{
    CORE_NET_EXEC_OP(socket_create, UNW_FAIL, type);
}

TUYA_ERRNO core_net_connect(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port)
{
    CORE_NETWORK_OPS_T *ops = core_net_get_ops();
    if (NULL == ops || NULL == ops->connect) {
        PR_ERR("CORE NET operation connect not available");
        return UNW_FAIL;
    }

    tuya_dev_evt_notify(DEV_EVT_TCP_CONNECT, ACTION_BEFORE, NULL);
    TUYA_ERRNO rt = ops->connect(fd, addr, port);
    tuya_dev_evt_notify(DEV_EVT_TCP_CONNECT, ACTION_AFTER, NULL);

    return rt;
}

TUYA_ERRNO core_net_connect_raw(const int fd, void *p_socket_addr, const int len)
{
    CORE_NET_EXEC_OP(connect_raw, UNW_FAIL, fd, p_socket_addr, len);
}

TUYA_ERRNO core_net_bind(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port)
{
    CORE_NET_EXEC_OP(bind, UNW_FAIL, fd, addr, port);
}

TUYA_ERRNO core_net_listen(const int fd, const int backlog)
{
    CORE_NET_EXEC_OP(listen, UNW_FAIL, fd, backlog);
}

int core_net_accept(const int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    CORE_NET_EXEC_OP(accept, UNW_FAIL, fd, addr, port);
}

TUYA_ERRNO core_net_send(const int fd, const void *buf, const uint32_t nbytes)
{
    CORE_NET_EXEC_OP(send, UNW_FAIL, fd, buf, nbytes);
}

TUYA_ERRNO core_net_send_to(const int fd, const void *buf, const uint32_t nbytes, const TUYA_IP_ADDR_T addr,
                            const uint16_t port)
{
    CORE_NET_EXEC_OP(send_to, UNW_FAIL, fd, buf, nbytes, addr, port);
}

TUYA_ERRNO core_net_recv(const int fd, void *buf, const uint32_t nbytes)
{
    CORE_NET_EXEC_OP(recv, UNW_FAIL, fd, buf, nbytes);
}

int core_net_recv_nd_size(const int fd, void *buf, const uint32_t buf_size, const uint32_t nd_size)
{
    CORE_NET_EXEC_OP(recv_nd_size, UNW_FAIL, fd, buf, buf_size, nd_size);
}

TUYA_ERRNO core_net_recvfrom(const int fd, void *buf, const uint32_t nbytes, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    CORE_NET_EXEC_OP(recvfrom, UNW_FAIL, fd, buf, nbytes, addr, port);
}

OPERATE_RET core_net_setsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname,
                                const void *optval, const int optlen)
{
    CORE_NET_EXEC_OP(setsockopt, OPRT_NOT_SUPPORTED, fd, level, optname, optval, optlen);
}

OPERATE_RET core_net_getsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname, void *optval,
                                int *optlen)
{
    CORE_NET_EXEC_OP(getsockopt, OPRT_NOT_SUPPORTED, fd, level, optname, optval, optlen);
}

OPERATE_RET core_net_set_timeout(const int fd, const int ms_timeout, const TUYA_TRANS_TYPE_E type)
{
    CORE_NET_EXEC_OP(set_timeout, OPRT_NOT_SUPPORTED, fd, ms_timeout, type);
}

OPERATE_RET core_net_set_bufsize(const int fd, const int buf_size, const TUYA_TRANS_TYPE_E type)
{
    CORE_NET_EXEC_OP(set_bufsize, OPRT_NOT_SUPPORTED, fd, buf_size, type);
}

OPERATE_RET core_net_set_reuse(const int fd)
{
    CORE_NET_EXEC_OP(set_reuse, OPRT_NOT_SUPPORTED, fd);
}

OPERATE_RET core_net_disable_nagle(const int fd)
{
    CORE_NET_EXEC_OP(disable_nagle, OPRT_NOT_SUPPORTED, fd);
}

OPERATE_RET core_net_set_broadcast(const int fd)
{
    CORE_NET_EXEC_OP(set_broadcast, OPRT_NOT_SUPPORTED, fd);
}

OPERATE_RET core_net_gethostbyname(const char *domain, TUYA_IP_ADDR_T *addr)
{
    CORE_NETWORK_OPS_T *ops = core_net_get_ops();
    if (NULL == ops || NULL == ops->gethostbyname) {
        PR_ERR("CORE NET operation gethostbyname not available");
        return OPRT_NOT_SUPPORTED;
    }

    tuya_dev_evt_notify(DEV_EVT_DNS_LOOKUP, ACTION_BEFORE, NULL);
    OPERATE_RET rt = ops->gethostbyname(domain, addr);
    tuya_dev_evt_notify(DEV_EVT_DNS_LOOKUP, ACTION_AFTER, NULL);

    return rt;
}

OPERATE_RET core_net_set_keepalive(int fd, const BOOL_T alive, const uint32_t idle, const uint32_t intr,
                                   const uint32_t cnt)
{
    CORE_NET_EXEC_OP(set_keepalive, OPRT_NOT_SUPPORTED, fd, alive, idle, intr, cnt);
}

OPERATE_RET core_net_get_socket_ip(int fd, TUYA_IP_ADDR_T *addr)
{
    CORE_NET_EXEC_OP(get_socket_ip, OPRT_NOT_SUPPORTED, fd, addr);
}

OPERATE_RET core_net_getsockname(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    CORE_NET_EXEC_OP(getsockname, OPRT_NOT_SUPPORTED, fd, addr, port);
}

OPERATE_RET core_net_getpeername(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    CORE_NET_EXEC_OP(getpeername, OPRT_NOT_SUPPORTED, fd, addr, port);
}

TUYA_IP_ADDR_T core_net_str2addr(const char *ip_str)
{
    CORE_NET_EXEC_OP(str2addr, 0xFFFFFFFFU, ip_str);
}

char *core_net_addr2str(TUYA_IP_ADDR_T ipaddr)
{
    CORE_NET_EXEC_OP(addr2str, NULL, ipaddr);
}

#endif /* defined(ENABLE_PLATFORM_NETWORK_OPS) && (ENABLE_PLATFORM_NETWORK_OPS == 1) */
