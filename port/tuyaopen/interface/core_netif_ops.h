/**
 * @file core_netif_ops.h
 * @brief Contract header for the CORE network interface ops card:
 *        CORE_NETIF_OPS_T, the link status enum and the link change callback.
 *
 * A types-only contract. core's core_netif.h includes this header and
 * dispatches core_netif_get_ip and core_netif_set_link_cb through the card,
 * which the port assembles as core_netif_ops_platform. The port reports link
 * up when at least one non-loopback interface is up and link down otherwise.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __CORE_NETIF_OPS_H__
#define __CORE_NETIF_OPS_H__

#include <stdbool.h>

#include "tuya_cloud_types.h"
#include "tuya_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Network interface link status */
typedef enum {
    CORE_NETIF_LINK_DOWN = 0, ///< link down (no non-loopback interface is up)
    CORE_NETIF_LINK_UP,       ///< link up (at least one non-loopback interface is up)
} CORE_NETIF_LINK_STAT_E;

/**
 * @brief Link status change callback
 *
 * @param[out] status current link status
 */
typedef void (*CORE_NETIF_LINK_CHANGE_CB)(CORE_NETIF_LINK_STAT_E status);

/** Interface-name buffer length, including the terminating NUL */
#define CORE_NETIF_NAME_LEN 16

/**
 * @brief One local IPv4 interface address
 *
 * Plain character arrays (not NW_IP_S): the NW_IP_S layout depends on the
 * platform, while this contract is shared by the core and every port.
 */
typedef struct {
    char name[CORE_NETIF_NAME_LEN]; ///< interface name, e.g. "sta" / "wlan0"
    char ip[16];                    ///< IPv4 address, dotted quad
    char mask[16];                  ///< IPv4 netmask, dotted quad
    char gw[16];                    ///< IPv4 gateway, may be empty
} CORE_NETIF_ADDR_T;

/**
 * @brief Interface address enumeration callback
 *
 * @param[in] addr      one interface address, never NULL
 * @param[in] user_data opaque pointer passed to core_netif_foreach
 *
 * @return true to stop the enumeration, false to continue
 */
typedef bool (*CORE_NETIF_ADDR_CB)(const CORE_NETIF_ADDR_T *addr, void *user_data);

/**
 * @brief core_netif platform ops: one member per port netif implementation
 */
typedef struct {
    /* Get the local interface IP; see core_netif_get_ip in core_netif.h */
    OPERATE_RET (*get_ip)(NW_IP_S *ip);
    /* Register the link status change callback; see core_netif_set_link_cb in core_netif.h */
    OPERATE_RET (*set_link_cb)(CORE_NETIF_LINK_CHANGE_CB cb);
    /* Enumerate the local IPv4 interface addresses; optional, NULL when the
     * platform has no interface list. See core_netif_foreach in core_netif.h */
    OPERATE_RET (*foreach_addr)(CORE_NETIF_ADDR_CB cb, void *user_data);
} CORE_NETIF_OPS_T;

#ifdef __cplusplus
}
#endif

#endif /* __CORE_NETIF_OPS_H__ */
