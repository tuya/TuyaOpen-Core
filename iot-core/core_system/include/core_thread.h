/**
 * @file core_thread.h
 * @brief CORE thread interface: create and start, delete, state query and
 *        current-thread identity.
 *
 * A thread is described by THREAD_CFG_T (stack depth, priority and name) and
 * runs its process function between the
 * optional enter and exit hooks. Creation and deletion dispatch through the
 * CORE_API_OPS_T thread members. core_thread_get_id returns an opaque identity
 * of the calling thread that is stable for that thread's lifetime.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __CORE_THREAD_H__
#define __CORE_THREAD_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *THREAD_HANDLE;

/**
 * @brief max length of thread name
 *
 */
#define CORE_THREAD_MAX_NAME_LEN 16

/**
 * @brief thread process function
 *
 */
typedef void (*THREAD_FUNC_CB)(void *args);
/**
 * @brief thread enter function
 *
 */
typedef void (*THREAD_ENTER_CB)(void);

/**
 * @brief thread exut function
 *
 */
typedef void (*THREAD_EXIT_CB)(void); // thread extract
/**
 * @brief thread running status
 *
 */
typedef enum {
    THREAD_STATE_EMPTY = 0,
    THREAD_STATE_RUNNING,
    THREAD_STATE_STOP,
    THREAD_STATE_DELETE,
} THREAD_STATE_E;

/**
 * @brief thread priority
 *
 */
typedef enum {
    THREAD_PRIO_0 = 5,
    THREAD_PRIO_1 = 4,
    THREAD_PRIO_2 = 3,
    THREAD_PRIO_3 = 2,
    THREAD_PRIO_4 = 1,
    THREAD_PRIO_5 = 0,
    THREAD_PRIO_6 = 0,
} THREAD_PRIO_E;
/**
 * @brief thread parameters
 *
 */
typedef struct {
    uint32_t stackDepth; // stack size
    uint8_t priority;    // thread priority
    char *thrdname;      // thread name
} THREAD_CFG_T;

/**
 * @brief create and start a tuya sdk thread
 *
 * @param[in] enter: the function called before the thread process called.can be
 * null
 * @param[in] exit: the function called after the thread process called.can be
 * null
 * @param[in] func: the main thread process function
 * @param[in] func_args: the args of the pThrdFunc.can be null
 * @param[in] cfg: the param of creating a thread
 * @param[out] handle: the tuya sdk thread context
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_thread_create_and_start(THREAD_HANDLE *handle, const THREAD_ENTER_CB enter, const THREAD_EXIT_CB exit,
                                        const THREAD_FUNC_CB func, const void *func_args, const THREAD_CFG_T *cfg);
/**
 * @brief stop and free a tuya sdk thread
 *
 * @param[in] handle: the input thread context
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_thread_delete(const THREAD_HANDLE handle);

/**
 * @brief get the thread context running status
 *
 * @param[in] thrdHandle: the input thread context
 * @return the thread status
 */
THREAD_STATE_E core_thread_get_state(const THREAD_HANDLE handle);

/**
 * @brief Get an opaque identity handle of the "current calling thread"
 *
 * @param[out] thread: thread identity handle. The same thread always gets the
 *             same value; it need not be the same kind of handle produced by
 *             core_thread_create_and_start.
 *
 * @note p2p pjlib (os_core_tuyaos.c) uses it to select a TLS row per OS task:
 *       tasks are distinguished by identity, and one thread reuses one row.
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_thread_get_id(THREAD_HANDLE *thread);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
