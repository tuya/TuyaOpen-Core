/**
 * @file core_network_ops.h
 * @brief Contract header for the standard network ops card (CORE_NETWORK_OPS_T).
 *
 * Types only. Core's core_network.h includes this header and dispatches through
 * the card instance core_network_ops_platform, which the port defines.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __CORE_NETWORK_OPS_H__
#define __CORE_NETWORK_OPS_H__

#include "tuya_cloud_types.h"
#include "tuya_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    TUYA_ERRNO (*get_errno)(void);
    OPERATE_RET (*fd_set)(int fd, TUYA_FD_SET_T *fds);
    OPERATE_RET (*fd_clear)(int fd, TUYA_FD_SET_T *fds);
    OPERATE_RET (*fd_isset)(int fd, TUYA_FD_SET_T *fds);
    OPERATE_RET (*fd_zero)(TUYA_FD_SET_T *fds);
    int (*select)(const int maxfd, TUYA_FD_SET_T *readfds, TUYA_FD_SET_T *writefds, TUYA_FD_SET_T *errorfds,
                  const uint32_t ms_timeout);
    int (*get_nonblock)(const int fd);
    OPERATE_RET (*set_block)(const int fd, const BOOL_T block);
    TUYA_ERRNO (*close)(const int fd);
    TUYA_ERRNO (*shutdown)(const int fd, const int how);
    int (*socket_create)(const TUYA_PROTOCOL_TYPE_E type);
    TUYA_ERRNO (*connect)(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port);
    TUYA_ERRNO (*connect_raw)(const int fd, void *p_socket, const int len);
    TUYA_ERRNO (*bind)(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port);
    TUYA_ERRNO (*listen)(const int fd, const int backlog);
    TUYA_ERRNO (*send)(const int fd, const void *buf, const uint32_t nbytes);
    TUYA_ERRNO (*send_to)(const int fd, const void *buf, const uint32_t nbytes, const TUYA_IP_ADDR_T addr,
                          const uint16_t port);
    int (*accept)(const int fd, TUYA_IP_ADDR_T *addr, uint16_t *port);
    TUYA_ERRNO (*recv)(const int fd, void *buf, const uint32_t nbytes);
    int (*recv_nd_size)(const int fd, void *buf, const uint32_t buf_size, const uint32_t nd_size);
    TUYA_ERRNO (*recvfrom)(const int fd, void *buf, const uint32_t nbytes, TUYA_IP_ADDR_T *addr, uint16_t *port);
    OPERATE_RET (*set_timeout)(const int fd, const int ms_timeout, const TUYA_TRANS_TYPE_E type);
    OPERATE_RET (*set_bufsize)(const int fd, const int buf_size, const TUYA_TRANS_TYPE_E type);
    OPERATE_RET (*set_reuse)(const int fd);
    OPERATE_RET (*disable_nagle)(const int fd);
    OPERATE_RET (*set_broadcast)(const int fd);
    OPERATE_RET (*gethostbyname)(const char *domain, TUYA_IP_ADDR_T *addr);
    OPERATE_RET (*set_keepalive)(int fd, const BOOL_T alive, const uint32_t idle, const uint32_t intr,
                                 const uint32_t cnt);
    OPERATE_RET (*get_socket_ip)(int fd, TUYA_IP_ADDR_T *addr);
    OPERATE_RET (*getsockname)(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port);
    OPERATE_RET (*getpeername)(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port);
    TUYA_IP_ADDR_T (*str2addr)(const char *ip_str);
    char *(*addr2str)(TUYA_IP_ADDR_T ipaddr);
    OPERATE_RET (*setsockopt)(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname, const void *optval,
                              const int optlen);
    OPERATE_RET (*getsockopt)(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname, void *optval,
                              int *optlen);
} CORE_NETWORK_OPS_T;

#ifdef __cplusplus
}
#endif

#endif /* __CORE_NETWORK_OPS_H__ */
