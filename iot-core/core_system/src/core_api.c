/**
 * @file core_api.c
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

#include "core_semaphore.h"
#include "core_mutex.h"
#include "core_queue.h"
#include "core_ota.h"
#include "core_log.h"
#include "core_api_ops.h"

extern CORE_API_OPS_T core_api_ops_platform;

CORE_API_OPS_T *core_api_get_ops(void)
{
    return &core_api_ops_platform;
}

/* OTA callback injection (optional platform capability, see core_ota.h): when
 * not registered core_ota_* returns OPRT_NOT_SUPPORTED; the registrant
 * guarantees the callback structure outlives the whole OTA flow */
static const CORE_OTA_CBS_T *s_ota_cbs = NULL;

void core_ota_register_cbs(const CORE_OTA_CBS_T *cbs)
{
    s_ota_cbs = cbs;
}

/***********************************************************
************************macro define************************
***********************************************************/

/**
 * @brief Macro to execute a CORE operation on a given ops card with error handling
 *
 * This macro simplifies the repetitive pattern of getting the ops card,
 * checking if the operation exists, and calling it with parameters.
 *
 * @param ops_ptr: pointer to the ops card (main card or a subsystem card)
 * @param op_name: The operation name in the ops struct
 * @param default_ret: Default return value if operation is not available
 * @param ...: Parameters to pass to the operation function
 */
#define CORE_EXEC_OP(ops_ptr, op_name, default_ret, ...)                                                                \
    do {                                                                                                               \
        if (NULL != (ops_ptr) && (ops_ptr)->op_name) {                                                                 \
            return (ops_ptr)->op_name(__VA_ARGS__);                                                                    \
        }                                                                                                              \
        PR_ERR("CORE API operation %s not available", #op_name);                                                        \
        return default_ret;                                                                                            \
    } while (0)

/**
 * @brief Macro to execute a CORE operation on a given ops card that returns void
 *
 * @param ops_ptr: pointer to the ops card (main card or a subsystem card)
 * @param op_name: The operation name in the ops struct
 * @param ...: Parameters to pass to the operation function
 */
#define CORE_EXEC_OP_VOID(ops_ptr, op_name, ...)                                                                        \
    do {                                                                                                               \
        if (NULL != (ops_ptr) && (ops_ptr)->op_name) {                                                                 \
            (ops_ptr)->op_name(__VA_ARGS__);                                                                           \
        }                                                                                                              \
    } while (0)

/* Convenience macros for the main card (core_api_ops_platform) */
#define CORE_API_EXEC_OP(op_name, default_ret, ...) CORE_EXEC_OP(core_api_get_ops(), op_name, default_ret, __VA_ARGS__)
#define CORE_API_EXEC_OP_VOID(op_name, ...)         CORE_EXEC_OP_VOID(core_api_get_ops(), op_name, __VA_ARGS__)

/* Convenience macros for OTA callback dispatch (s_ota_cbs, optional platform
 * capability; returns OPRT_NOT_SUPPORTED when not registered) */
#define CORE_OTA_EXEC_OP(op_name, default_ret, ...) CORE_EXEC_OP(s_ota_cbs, op_name, default_ret, __VA_ARGS__)

//! sem
OPERATE_RET core_semaphore_create_init(SEM_HANDLE *handle, uint32_t sem_cnt, uint32_t sem_max)
{
    CORE_API_EXEC_OP(semaphore_create_init, OPRT_COM_ERROR, handle, sem_cnt, sem_max);
}

OPERATE_RET core_semaphore_wait(SEM_HANDLE handle, uint32_t timeout)
{
    CORE_API_EXEC_OP(semaphore_wait, OPRT_COM_ERROR, handle, timeout);
}

OPERATE_RET core_semaphore_post(SEM_HANDLE handle)
{
    CORE_API_EXEC_OP(semaphore_post, OPRT_COM_ERROR, handle);
}

OPERATE_RET core_semaphore_release(SEM_HANDLE handle)
{
    CORE_API_EXEC_OP(semaphore_release, OPRT_COM_ERROR, handle);
}

//! mutex
OPERATE_RET core_mutex_create_init(MUTEX_HANDLE *handle)
{
    CORE_API_EXEC_OP(mutex_create_init, OPRT_COM_ERROR, handle);
}

OPERATE_RET core_mutex_lock(const MUTEX_HANDLE handle)
{
    CORE_API_EXEC_OP(mutex_lock, OPRT_COM_ERROR, handle);
}

OPERATE_RET core_mutex_unlock(const MUTEX_HANDLE handle)
{
    CORE_API_EXEC_OP(mutex_unlock, OPRT_COM_ERROR, handle);
}

OPERATE_RET core_mutex_release(const MUTEX_HANDLE handle)
{
    CORE_API_EXEC_OP(mutex_release, OPRT_COM_ERROR, handle);
}

//! ota

OPERATE_RET core_ota_start_notify(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path)
{
    CORE_OTA_EXEC_OP(start_notify, OPRT_NOT_SUPPORTED, image_size, type, path);
}

OPERATE_RET core_ota_data_process(TUYA_OTA_DATA_T *pack, uint32_t *remain_len)
{
    CORE_OTA_EXEC_OP(data_process, OPRT_NOT_SUPPORTED, pack, remain_len);
}

OPERATE_RET core_ota_end_notify(BOOL_T reset)
{
    CORE_OTA_EXEC_OP(end_notify, OPRT_NOT_SUPPORTED, reset);
}

//! queue
OPERATE_RET core_queue_create_init(QUEUE_HANDLE *queue, int msgsize, int msgcount)
{
    CORE_API_EXEC_OP(queue_create_init, OPRT_COM_ERROR, queue, msgsize, msgcount);
}

OPERATE_RET core_queue_post(QUEUE_HANDLE queue, void *data, uint32_t timeout)
{
    CORE_API_EXEC_OP(queue_post, OPRT_COM_ERROR, queue, data, timeout);
}

OPERATE_RET core_queue_fetch(QUEUE_HANDLE queue, void *msg, uint32_t timeout)
{
    CORE_API_EXEC_OP(queue_fetch, OPRT_COM_ERROR, queue, msg, timeout);
}

void core_queue_free(QUEUE_HANDLE queue)
{
    CORE_API_EXEC_OP_VOID(queue_free, queue);
}
