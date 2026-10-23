/**
 * @file core_network.c
 * @brief CORE network utilities: active-address tracking and socket operations.
 *
 * The socket calls go directly to the platform socket API. The platform
 * difference (POSIX on LINUX, lwIP on ESP32) is confined to the port-provided
 * core_network_platform.h header, so this file contains no ops table and no
 * platform #if. The active IPv4 address is kept in a local static and pushed
 * by the link owner on link events.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#include "tuya_iot_config.h"
#include "core_api.h"

#include "core_network.h"
#include "core_network_platform.h"
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

/***********************************************************
***********************typedef define***********************
***********************************************************/

typedef struct NETWORK_ERRNO_TRANS {
    int sys_err;
    int priv_err;
} NETWORK_ERRNO_TRANS_S;

static const NETWORK_ERRNO_TRANS_S unw_errno_trans[] = {{EINTR, UNW_EINTR},
                                                 {EBADF, UNW_EBADF},
                                                 {EAGAIN, UNW_EAGAIN},
                                                 {EFAULT, UNW_EFAULT},
                                                 {EBUSY, UNW_EBUSY},
                                                 {EINVAL, UNW_EINVAL},
                                                 {ENFILE, UNW_ENFILE},
                                                 {EMFILE, UNW_EMFILE},
                                                 {ENOSPC, UNW_ENOSPC},
                                                 {EPIPE, UNW_EPIPE},
                                                 {EWOULDBLOCK, UNW_EWOULDBLOCK},
                                                 {ENOTSOCK, UNW_ENOTSOCK},
                                                 {ENOPROTOOPT, UNW_ENOPROTOOPT},
                                                 {EADDRINUSE, UNW_EADDRINUSE},
                                                 {EADDRNOTAVAIL, UNW_EADDRNOTAVAIL},
                                                 {ENETDOWN, UNW_ENETDOWN},
                                                 {ENETUNREACH, UNW_ENETUNREACH},
                                                 {ENETRESET, UNW_ENETRESET},
                                                 {ECONNRESET, UNW_ECONNRESET},
                                                 {ENOBUFS, UNW_ENOBUFS},
                                                 {EISCONN, UNW_EISCONN},
                                                 {ENOTCONN, UNW_ENOTCONN},
                                                 {ETIMEDOUT, UNW_ETIMEDOUT},
                                                 {ECONNREFUSED, UNW_ECONNREFUSED},
                                                 {EHOSTDOWN, UNW_EHOSTDOWN},
                                                 {EHOSTUNREACH, UNW_EHOSTUNREACH},
                                                 {ENOMEM, UNW_ENOMEM},
                                                 {EMSGSIZE, UNW_EMSGSIZE}};

/***********************************************************
********************function declaration********************
***********************************************************/

static void __core_net_fill_sockaddr(struct sockaddr_in *sin, TUYA_IP_ADDR_T addr, uint16_t port)
{
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    sin->sin_addr.s_addr = htonl(addr);
}

static void __core_net_extract_addr_port(const struct sockaddr_in *sin, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    if (addr) {
        *addr = ntohl(sin->sin_addr.s_addr);
    }
    if (port) {
        *port = ntohs(sin->sin_port);
    }
}

/***********************************************************
************************macro define************************
***********************************************************/

/* TUYA_FD_SET_T is a fixed-size bitmap: reject fds that would write past it
 * (FD_SET/FD_CLR/FD_ISSET do no bounds check of their own). */
#define CORE_NET_FD_IS_VALID(fd) ((fd) >= 0 && (fd) < (int)(sizeof(TUYA_FD_SET_T) * 8))

/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * @brief Get error code of network
 *
 * @param void
 *
 * @note This API is used for getting error code of network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_get_errno(void)
{
    int sys_err = errno;
    int i = 0;

    for (i = 0; i < (int)(sizeof(unw_errno_trans) / sizeof(unw_errno_trans[0])); i++) {
        if (unw_errno_trans[i].sys_err == sys_err) {
            return unw_errno_trans[i].priv_err;
        }
    }

    return -100 - sys_err;
}

/**
 * @brief Add file descriptor to set
 *
 * @param[in] fd: file descriptor
 * @param[in] fds: set of file descriptor
 *
 * @note This API is used to add file descriptor to set.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_fd_set(int fd, TUYA_FD_SET_T *fds)
{
    if (!CORE_NET_FD_IS_VALID(fd) || (fds == NULL)) {
        return -3000 + fd;
    }

    FD_SET(fd, CORE_NET_NATIVE_FD_SET(fds));

    return OPRT_OK;
}

/**
 * @brief Clear file descriptor from set
 *
 * @param[in] fd: file descriptor
 * @param[in] fds: set of file descriptor
 *
 * @note This API is used to clear file descriptor from set.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_fd_clear(int fd, TUYA_FD_SET_T *fds)
{
    if (!CORE_NET_FD_IS_VALID(fd) || (fds == NULL)) {
        return -3000 + fd;
    }

    FD_CLR(fd, CORE_NET_NATIVE_FD_SET(fds));

    return OPRT_OK;
}

/**
 * @brief Check file descriptor is in set
 *
 * @param[in] fd: file descriptor
 * @param[in] fds: set of file descriptor
 *
 * @note This API is used to check the file descriptor is in set.
 *
 * @return TRUE or FALSE
 */
OPERATE_RET core_net_fd_isset(int fd, TUYA_FD_SET_T *fds)
{
    if (!CORE_NET_FD_IS_VALID(fd) || (fds == NULL)) {
        return -3000 + fd;
    }

    return FD_ISSET(fd, CORE_NET_NATIVE_FD_SET(fds)) ? 1 : 0;
}

/**
 * @brief Clear all file descriptor in set
 *
 * @param[in] fds: set of file descriptor
 *
 * @note This API is used to clear all file descriptor in set.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_fd_zero(TUYA_FD_SET_T *fds)
{
    if (fds == NULL) {
        return -1;
    }

    FD_ZERO(CORE_NET_NATIVE_FD_SET(fds));

    return OPRT_OK;
}

/**
 * @brief Get available file descriptors
 *
 * @param[in] maxfd: max count of file descriptor
 * @param[out] readfds: a set of readalbe file descriptor
 * @param[out] writefds: a set of writable file descriptor
 * @param[out] errorfds: a set of except file descriptor
 * @param[in] ms_timeout: time out
 *
 * @note This API is used to get available file descriptors.
 *
 * @return the count of available file descriptors.
 */
int core_net_select(const int maxfd, TUYA_FD_SET_T *readfds, TUYA_FD_SET_T *writefds, TUYA_FD_SET_T *errorfds,
                   const uint32_t ms_timeout)
{
    if (maxfd <= 0) {
        /* No descriptor to watch: return immediately, do not sleep. */
        return maxfd;
    }

    /* ms_timeout == 0 means wait forever (wall-clock timeout is NULL). */
    struct timeval timeout = {ms_timeout / 1000, (ms_timeout % 1000) * 1000};
    struct timeval *tmp = ms_timeout ? &timeout : NULL;

    return select(maxfd, CORE_NET_NATIVE_FD_SET(readfds), CORE_NET_NATIVE_FD_SET(writefds),
                  CORE_NET_NATIVE_FD_SET(errorfds), tmp);
}

/**
 * @brief Get no block file descriptors
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to get no block file descriptors.
 *
 * @return the count of no block file descriptors.
 */
int core_net_get_nonblock(const int fd)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    return ((fcntl(fd, F_GETFL, 0) & O_NONBLOCK) == O_NONBLOCK) ? 1 : 0;
}

/**
 * @brief Set block flag for file descriptors
 *
 * @param[in] fd: file descriptor
 * @param[in] block: block flag
 *
 * @note This API is used to set block flag for file descriptors.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_set_block(const int fd, const BOOL_T block)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return OPRT_COM_ERROR;
    }

    if (block) {
        flags &= (~O_NONBLOCK);
    } else {
        flags |= O_NONBLOCK;
    }

    if (fcntl(fd, F_SETFL, flags) < 0) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief Close file descriptors
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to close file descriptors.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_close(const int fd)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    return close(fd);
}

/**
 * @brief Disable send and/or receive on a socket
 *
 * @param[in] fd: file descriptor
 * @param[in] how: 0 to shut down receive, 1 to shut down send, 2 for both
 *
 * @note This API is used to shut down part of a full-duplex connection while
 * the file descriptor stays open.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_shutdown(const int fd, const int how)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    return shutdown(fd, how);
}

/**
 * @brief Create a tcp/udp socket
 *
 * @param[in] type: protocol type, tcp or udp
 *
 * @note This API is used for creating a tcp/udp socket.
 *
 * @return file descriptor
 */
int core_net_socket_create(const TUYA_PROTOCOL_TYPE_E type)
{
    if (PROTOCOL_TCP == type) {
        return socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    } else if (PROTOCOL_RAW == type) {
        return socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    }

    return socket(AF_INET, SOCK_DGRAM, 0);
}

/**
 * @brief Connect to network
 *
 * @param[in] fd: file descriptor
 * @param[in] addr: address information of server
 * @param[in] port: port information of server
 *
 * @note This API is used for connecting to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_connect(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    struct sockaddr_in sock_addr;
    __core_net_fill_sockaddr(&sock_addr, addr, port);

    /* Notify ULP around the TCP connect so it can keep the device awake for the
     * duration (covers TLS/HTTP/MQTT/ATOP socket setup - they all land here). */
    tuya_dev_evt_notify(DEV_EVT_TCP_CONNECT, ACTION_BEFORE, NULL);
    TUYA_ERRNO rt = connect(fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    tuya_dev_evt_notify(DEV_EVT_TCP_CONNECT, ACTION_AFTER, NULL);

    return rt;
}

/**
 * @brief Connect to network with raw data
 *
 * @param[in] fd: file descriptor
 * @param[in] p_socket: raw socket data
 * @param[in] len: data lenth
 *
 * @note This API is used for connecting to network with raw data.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_connect_raw(const int fd, void *p_socket_addr, const int len)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    return connect(fd, (struct sockaddr *)p_socket_addr, len);
}

/**
 * @brief Bind to network
 *
 * @param[in] fd: file descriptor
 * @param[in] addr: address information of server
 * @param[in] port: port information of server
 *
 * @note This API is used for binding to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_bind(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    struct sockaddr_in sock_addr;
    __core_net_fill_sockaddr(&sock_addr, addr, port);

    TUYA_ERRNO ret = bind(fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    if ((0 == ret) && (addr != TY_IPADDR_ANY)) {
        if (0 != core_net_platform_bind_interface(fd, addr)) {
            PR_DEBUG("bind interface for 0x%08x failed", addr);
        }
    }

    return ret;
}

/**
 * @brief Listen to network
 *
 * @param[in] fd: file descriptor
 * @param[in] backlog: max count of backlog connection
 *
 * @note This API is used for listening to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_listen(const int fd, const int backlog)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    return listen(fd, backlog);
}

/**
 * @brief Listen to network
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: the accept ip addr
 * @param[out] port: the accept port number
 *
 * @note This API is used for listening to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
int core_net_accept(const int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(sock_addr);
    int ret = accept(fd, (struct sockaddr *)&sock_addr, &len);
    if (ret < 0) {
        return -1;
    }

    __core_net_extract_addr_port(&sock_addr, addr, port);

    return ret;
}

/**
 * @brief Send data to network
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: send data buffer
 * @param[in] nbytes: buffer lenth
 *
 * @note This API is used for sending data to network
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_send(const int fd, const void *buf, const uint32_t nbytes)
{
    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    return send(fd, buf, nbytes, 0);
}

/**
 * @brief Send data to specified server
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: send data buffer
 * @param[in] nbytes: buffer lenth
 * @param[in] addr: address information of server
 * @param[in] port: port information of server
 *
 * @note This API is used for sending data to network
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_send_to(const int fd, const void *buf, const uint32_t nbytes, const TUYA_IP_ADDR_T addr,
                           const uint16_t port)
{
    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    struct sockaddr_in sock_addr;
    __core_net_fill_sockaddr(&sock_addr, addr, port);

    return sendto(fd, buf, nbytes, 0, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
}

/**
 * @brief Receive data from network
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: receive data buffer
 * @param[in] nbytes: buffer lenth
 *
 * @note This API is used for receiving data from network
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_recv(const int fd, void *buf, const uint32_t nbytes)
{
    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    TUYA_ERRNO ret = recv(fd, buf, nbytes, 0);
    if (ret < 0) {
        TUYA_ERRNO err = core_net_get_errno();
        if ((UNW_EINTR == err) || (UNW_EAGAIN == err)) {
            core_system_sleep(10); /* 10ms retry pause */
            ret = recv(fd, buf, nbytes, 0);
        }
    }

    return ret;
}

/**
 * @brief Receive data from network with need size
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: receive data buffer
 * @param[in] nbytes: buffer lenth
 * @param[in] nd_size: the need size
 *
 * @note This API is used for receiving data from network with need size
 *
 * @return >0 on success. Others on error
 */
int core_net_recv_nd_size(const int fd, void *buf, const uint32_t buf_size, const uint32_t nd_size)
{
    if ((fd < 0) || (NULL == buf) || (buf_size == 0) || (nd_size == 0) || (buf_size < nd_size)) {
        return -3000 + fd;
    }

    uint32_t rd_size = 0;
    int ret = -1;

    while (rd_size < nd_size) {
        ret = recv(fd, ((uint8_t *)buf + rd_size), nd_size - rd_size, 0);
        if (ret <= 0) {
            if (0 == ret) {
                /* Peer closed. errno may still hold a stale EAGAIN/EINTR from an
                 * earlier call; do not spin on it. */
                break;
            }
            TUYA_ERRNO err = core_net_get_errno();
            if ((UNW_EWOULDBLOCK == err) || (UNW_EINTR == err) || (UNW_EAGAIN == err)) {
                core_system_sleep(10); /* 10ms retry pause */
                continue;
            }
            break;
        }
        rd_size += ret;
    }

    if (rd_size < nd_size) {
        return -2;
    }

    return (int)rd_size;
}

/**
 * @brief Receive data from specified server
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: receive data buffer
 * @param[in] nbytes: buffer lenth
 * @param[in] addr: address information of server
 * @param[in] port: port information of server
 *
 * @note This API is used for receiving data from specified server
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO core_net_recvfrom(const int fd, void *buf, const uint32_t nbytes, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    struct sockaddr_in sock_addr;
    socklen_t addr_len = sizeof(sock_addr);
    TUYA_ERRNO ret = recvfrom(fd, buf, nbytes, 0, (struct sockaddr *)&sock_addr, &addr_len);
    if (ret <= 0) {
        return ret;
    }

    __core_net_extract_addr_port(&sock_addr, addr, port);

    return ret;
}

/**
 * @brief Set socket options
 *
 * @param[in] fd: file descriptor
 * @param[in] level: setting level
 * @param[in] optname: the name of the option
 * @param[in] optval: the value of option
 * @param[in] optlen: the length of the option value
 *
 * @note This API is used for setting socket options.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_setsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname,
                               const void *optval, const int optlen)
{
    return setsockopt(fd, level, optname, optval, optlen);
}

/**
 * @brief Get socket options
 *
 * @param[in] fd: file descriptor
 * @param[in] level: getting level
 * @param[in] optname: the name of the option
 * @param[out] optval: the value of option
 * @param[out] optlen: the length of the option value
 *
 * @note This API is used for getting socket options.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_getsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname, void *optval,
                               int *optlen)
{
    if (NULL == optlen) {
        return OPRT_COM_ERROR;
    }

    return getsockopt(fd, level, optname, optval, (socklen_t *)optlen);
}

/**
 * @brief Set timeout option of socket fd
 *
 * @param[in] fd: file descriptor
 * @param[in] ms_timeout: timeout in ms
 * @param[in] type: transfer type, receive or send
 *
 * @note This API is used for setting timeout option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_set_timeout(const int fd, const int ms_timeout, const TUYA_TRANS_TYPE_E type)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    struct timeval timeout = {ms_timeout / 1000, (ms_timeout % 1000) * 1000};
    int optname = ((type == TRANS_RECV) ? SO_RCVTIMEO : SO_SNDTIMEO);

    return core_net_setsockopt(fd, SOL_SOCKET, optname, (const char *)&timeout, sizeof(timeout));
}

/**
 * @brief Set buffer_size option of socket fd
 *
 * @param[in] fd: file descriptor
 * @param[in] buf_size: buffer size in byte
 * @param[in] type: transfer type, receive or send
 *
 * @note This API is used for setting buffer_size option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_set_bufsize(const int fd, const int buf_size, const TUYA_TRANS_TYPE_E type)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    int size = buf_size;
    int optname = ((type == TRANS_RECV) ? SO_RCVBUF : SO_SNDBUF);

    return core_net_setsockopt(fd, SOL_SOCKET, optname, (const char *)&size, sizeof(size));
}

/**
 * @brief Enable reuse option of socket fd
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to enable reuse option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_set_reuse(const int fd)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    int flag = 1;

    return core_net_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&flag, sizeof(flag));
}

/**
 * @brief Disable nagle option of socket fd
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to disable nagle option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_disable_nagle(const int fd)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    int flag = 1;

    return core_net_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
}

/**
 * @brief Enable broadcast option of socket fd
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to enable broadcast option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_set_broadcast(const int fd)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    int flag = 1;

    return core_net_setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&flag, sizeof(flag));
}

/**
 * @brief Get address information by domain
 *
 * @param[in] domain: domain information
 * @param[in] addr: address information
 *
 * @note This API is used for getting address information by domain.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_gethostbyname(const char *domain, TUYA_IP_ADDR_T *addr)
{
    if ((domain == NULL) || (addr == NULL)) {
        return -2;
    }

    /* Notify ULP around the DNS lookup so it can keep the device awake for it. */
    tuya_dev_evt_notify(DEV_EVT_DNS_LOOKUP, ACTION_BEFORE, NULL);
    struct hostent *h = gethostbyname(domain);
    tuya_dev_evt_notify(DEV_EVT_DNS_LOOKUP, ACTION_AFTER, NULL);

    if (NULL == h) {
        return -1;
    }

    *addr = ntohl(((struct in_addr *)(h->h_addr_list[0]))->s_addr);

    return OPRT_OK;
}

/**
 * @brief Set keepalive option of socket fd to monitor the connection
 *
 * @param[in] fd: file descriptor
 * @param[in] alive: keepalive option, enable or disable option
 * @param[in] idle: keep idle option, if the connection has no data exchange
 * with the idle time(in seconds), start probe.
 * @param[in] intr: keep interval option, the probe time interval.
 * @param[in] cnt: keep count option, probe count.
 *
 * @note This API is used to set keepalive option of socket fd to monitor the
 * connection.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_set_keepalive(int fd, const BOOL_T alive, const uint32_t idle, const uint32_t intr,
                                  const uint32_t cnt)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    int keepalive = alive;
    int keepidle = idle;
    int keepinterval = intr;
    int keepcount = cnt;
    int ret = 0;

    ret |= core_net_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive, sizeof(keepalive));
    ret |= core_net_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (void *)&keepidle, sizeof(keepidle));
    ret |= core_net_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (void *)&keepinterval, sizeof(keepinterval));
    ret |= core_net_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (void *)&keepcount, sizeof(keepcount));

    return ret;
}

/**
 * @brief Get ip address by socket fd
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: ip address
 *
 * @note This API is used for getting ip address by socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_get_socket_ip(int fd, TUYA_IP_ADDR_T *addr)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    if (NULL == addr) {
        return OPRT_COM_ERROR;
    }

    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(sock_addr);

    memset(&sock_addr, 0, sizeof(sock_addr));
    if (0 != getsockname(fd, (struct sockaddr *)&sock_addr, &len)) {
        return OPRT_COM_ERROR;
    }

    *addr = ntohl(sock_addr.sin_addr.s_addr);

    return OPRT_OK;
}

/**
 * @brief Get the local address a socket fd is bound to
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: local ip address in host byte order, may be NULL
 * @param[out] port: local port in host byte order, may be NULL
 *
 * @note Unlike core_net_get_socket_ip() this also reports the port, which is
 * the only way to learn the port the stack picked when binding to port 0.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_getsockname(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(sock_addr);

    memset(&sock_addr, 0, sizeof(sock_addr));
    if (0 != getsockname(fd, (struct sockaddr *)&sock_addr, &len)) {
        return OPRT_COM_ERROR;
    }

    __core_net_extract_addr_port(&sock_addr, addr, port);

    return OPRT_OK;
}

/**
 * @brief Get the address of the peer connected to a socket fd
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: peer ip address in host byte order, may be NULL
 * @param[out] port: peer port in host byte order, may be NULL
 *
 * @note This API is used for getting the remote name of a connected socket.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_net_getpeername(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(sock_addr);

    memset(&sock_addr, 0, sizeof(sock_addr));
    if (0 != getpeername(fd, (struct sockaddr *)&sock_addr, &len)) {
        return OPRT_COM_ERROR;
    }

    __core_net_extract_addr_port(&sock_addr, addr, port);

    return OPRT_OK;
}

/**
 * @brief Change ip string to address
 *
 * @param[in] ip_str: ip string
 *
 * @note This API is used to change ip string to address.
 *
 * @return ip address
 */
TUYA_IP_ADDR_T core_net_str2addr(const char *ip_str)
{
    if (ip_str == NULL) {
        return 0xFFFFFFFF;
    }

    return ntohl(inet_addr(ip_str));
}

/**
 * @brief Change ip address to string
 *
 * @param[in] ipaddr: ip address
 *
 * @note This API is used to change ip address(in host byte order) to string(in
 * IPv4 numbers-and-dots(xx.xx.xx.xx) notion).
 *
 * @return ip string
 */
char *core_net_addr2str(TUYA_IP_ADDR_T ipaddr)
{
    struct in_addr hostaddr;

    hostaddr.s_addr = htonl(ipaddr);

    return inet_ntoa(hostaddr);
}
