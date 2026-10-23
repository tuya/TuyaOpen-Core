/**
 * @file port_mutex.h
 * @brief Port mutex (port-internal interface): the implementation behind the
 *        card's .mutex_* members.
 *
 * Used across files inside the port (the storage lock in port_kv.c). core
 * never calls this layer directly: core_mutex_* dispatch through the card.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __PORT_MUTEX_H__
#define __PORT_MUTEX_H__

#include "tuya_cloud_types.h"
#include "tuya_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *PORT_MUTEX_HANDLE;

OPERATE_RET port_mutex_create_init(PORT_MUTEX_HANDLE *handle);
OPERATE_RET port_mutex_lock(const PORT_MUTEX_HANDLE handle);
OPERATE_RET port_mutex_unlock(const PORT_MUTEX_HANDLE handle);
OPERATE_RET port_mutex_release(const PORT_MUTEX_HANDLE handle);

#ifdef __cplusplus
}
#endif

#endif /* __PORT_MUTEX_H__ */
