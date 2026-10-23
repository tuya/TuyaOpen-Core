/**
 * @file core_netif.c
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

#include "core_netif.h"
#include "core_netif_ops.h"
#include "core_log.h"

/* Platform ops card (defined in the port's port_netif.c), dispatched by core
 * through the getter */
extern CORE_NETIF_OPS_T core_netif_ops_platform;

CORE_NETIF_OPS_T *core_netif_get_ops(void)
{
    return &core_netif_ops_platform;
}

#define CORE_NETIF_EXEC_OP(op_name, default_ret, ...)                                                                   \
    do {                                                                                                               \
        CORE_NETIF_OPS_T *ops = core_netif_get_ops();                                                                    \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("CORE NETIF operation %s not available", #op_name);                                                      \
        return default_ret;                                                                                            \
    } while (0)

OPERATE_RET core_netif_get_ip(NW_IP_S *ip)
{
    CORE_NETIF_EXEC_OP(get_ip, OPRT_COM_ERROR, ip);
}

OPERATE_RET core_netif_set_link_cb(CORE_NETIF_LINK_CHANGE_CB cb)
{
    CORE_NETIF_EXEC_OP(set_link_cb, OPRT_COM_ERROR, cb);
}
