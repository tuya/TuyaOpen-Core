/**
 * @file core_api.h
 * @brief CORE dispatch for semaphores, mutexes, queues and OTA.
 *
 * Every core_semaphore_*, core_mutex_* and core_queue_* function resolves the
 * platform ops card through core_api_get_ops(), which returns
 * core_api_ops_platform (defined by the port). CORE_EXEC_OP checks that the card
 * and the requested member are non-NULL, logs the missing operation and
 * returns a caller-supplied default on failure; CORE_EXEC_OP_VOID is the
 * variant for operations without a return value. The core_* signatures match
 * the ones declared in the core_semaphore.h, core_mutex.h and core_queue.h
 * headers.
 *
 * OTA is an optional capability delivered by runtime callback injection:
 * core_ota_register_cbs() stores a CORE_OTA_CBS_T pointer, and the three
 * core_ota_* functions dispatch through it with OPRT_NOT_SUPPORTED as the
 * default when no callback set is registered. The registrant keeps the
 * structure alive for the whole OTA flow.
 *
 * @note This file is part of the Tuya IoT Development Platform and is intended
 * for use in Tuya-based applications. It is subject to the platform's license
 * and copyright terms.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __CORE_API_H__
#define __CORE_API_H__

#include "tuya_cloud_types.h"

#include "core_log.h"
#include "core_memory.h"
#include "core_mutex.h"
#include "core_ota.h"
#include "core_queue.h"
#include "core_semaphore.h"
#include "core_system.h"
#include "core_thread.h"
#include "core_time_service.h"
#include "core_network.h"
#include "core_workqueue.h"
#include "core_kv.h"
#include "core_time_service.h"
#include "core_workq_service.h"
#include "core_security.h"
#include "core_sw_timer.h"
#include "core_event.h"

#include "core_api_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the platform ops of core_api
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
CORE_API_OPS_T *core_api_get_ops(void);

#ifdef __cplusplus
}
#endif /* __CORE_LOG_H__ */

#endif
