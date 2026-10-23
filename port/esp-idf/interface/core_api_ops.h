/**
 * @file core_api_ops.h
 * @brief Contract header for the CORE main ops card: CORE_API_OPS_T plus the
 *        handle and callback aliases used by its members.
 *
 * The card holds one member per port implementation of the log, memory,
 * system, semaphore, mutex, thread and queue interfaces dispatched by the core
 * layer; OTA is not a card member (it is callback-injected, see core_ota.h).
 * The port assembles core_api_ops_platform from this card and core reaches it
 * through core_api_get_ops(); the port never includes or calls core, and this
 * header declares no implementation.
 *
 * Handle/callback aliases are void* / void(*)(void*) aliases identical to
 * core's core_semaphore / core_mutex / core_thread / core_queue definitions;
 * repeated typedefs are legal since C11, so a translation unit including both
 * sides compiles.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __CORE_API_OPS_H__
#define __CORE_API_OPS_H__

#include "tuya_cloud_types.h"
#include "tuya_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Handle/callback aliases (identical to core's core_* header definitions) */
typedef void *SEM_HANDLE;
typedef void *MUTEX_HANDLE;
typedef void *THREAD_HANDLE;
typedef void (*THREAD_FUNC_CB)(void *args);
typedef void *QUEUE_HANDLE;

/**
 * @brief core_api platform ops: members map one-to-one to the port's
 *        memory/system/semaphore/mutex/thread/queue/log implementations and
 *        cover only the interfaces actually dispatched by the core layer (OTA is
 *        injected as callbacks, see core_ota.h)
 */
typedef struct {
    //! log output (used early at startup by the core_log callback and QR output; placed first in the card)
    void (*log_output)(const char *format, ...);
    //! memory
    void *(*malloc)(size_t size);
    void (*free)(void *ptr);
    //! system
    void (*sleep)(uint32_t num_ms);
    void (*reset)(void);
    SYS_TIME_T (*get_millisecond)(void);
    int (*get_random)(uint32_t range);
    //! semaphore
    OPERATE_RET (*semaphore_create_init)(SEM_HANDLE *handle, uint32_t sem_cnt, uint32_t sem_max);
    OPERATE_RET (*semaphore_wait)(const SEM_HANDLE handle, uint32_t timeout);
    OPERATE_RET (*semaphore_post)(const SEM_HANDLE handle);
    OPERATE_RET (*semaphore_release)(const SEM_HANDLE handle);
    //! mutex
    OPERATE_RET (*mutex_create_init)(MUTEX_HANDLE *handle);
    OPERATE_RET (*mutex_lock)(const MUTEX_HANDLE handle);
    OPERATE_RET (*mutex_unlock)(const MUTEX_HANDLE handle);
    OPERATE_RET (*mutex_release)(const MUTEX_HANDLE handle);
    //! thread
    OPERATE_RET (*thread_create)(THREAD_HANDLE *thread, const char *name, uint32_t stack_size, uint32_t priority,
                                 THREAD_FUNC_CB func, void *const arg);
    OPERATE_RET (*thread_release)(const THREAD_HANDLE thread);
    OPERATE_RET (*thread_set_self_name)(const char *name);
    OPERATE_RET (*thread_is_self)(const THREAD_HANDLE thread, BOOL_T *is_self);
    OPERATE_RET (*thread_get_id)(THREAD_HANDLE *thread);
    //! queue
    OPERATE_RET (*queue_create_init)(QUEUE_HANDLE *queue, int msgsize, int msgcount);
    OPERATE_RET (*queue_post)(const QUEUE_HANDLE queue, void *data, uint32_t timeout);
    OPERATE_RET (*queue_fetch)(const QUEUE_HANDLE queue, void *msg, uint32_t timeout);
    void (*queue_free)(const QUEUE_HANDLE queue);
} CORE_API_OPS_T;

#ifdef __cplusplus
}
#endif

#endif /* __CORE_API_OPS_H__ */
