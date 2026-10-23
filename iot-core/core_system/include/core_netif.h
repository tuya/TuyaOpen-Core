/**
 * @file core_netif.h
 * @brief core_netif platform dispatch: forwards the two interfaces to the
 *        port's netif implementation (core_netif_ops_platform).
 *
 * core holds no network interface implementation -- interface walking,
 * ioctl retrieval and the polling thread all live in the port (see the
 * core_netif_ops.h contract). Dispatch goes through CORE_NETIF_EXEC_OP, which
 * checks the card and member before calling them and returns the given
 * default otherwise.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __CORE_NETIF_H__
#define __CORE_NETIF_H__

#include "tuya_cloud_types.h"

/* Contract layer (supplied by the selected platform's port):
 * CORE_NETIF_LINK_STAT_E / CORE_NETIF_LINK_CHANGE_CB / CORE_NETIF_OPS_T are
 * defined in port/interface/core_netif_ops.h -- core consumes the contract,
 * never defines it (layering: port does not depend on core). */
#include "core_netif_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the platform ops of core_netif
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
CORE_NETIF_OPS_T *core_netif_get_ops(void);

/**
 * @brief  Get the local network interface IP (first non-loopback interface
 *         that is up)
 *
 * @param[out]  ip: the ip address
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET core_netif_get_ip(NW_IP_S *ip);

/**
 * @brief  Register a link state change callback (starts an internal 3s
 *         polling thread; repeated calls are idempotent)
 *
 * @param[in]   cb: the callback when link status changed
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET core_netif_set_link_cb(CORE_NETIF_LINK_CHANGE_CB cb);

#ifdef __cplusplus
}
#endif

#endif // __CORE_NETIF_H__
