/**
 * @file port_api.c
 * @brief Assembly of the core_api platform ops card + single-file OS primitive
 *        implementations (esp32 port)
 *
 * This file holds the seven groups -- log output / memory / system /
 * semaphore / mutex / thread / queue -- exposed only through the CORE_API_OPS_T
 * card (OTA is injected as core callbacks and implemented by the application
 * layer, so it has no card members here):
 * - log output / semaphore / mutex / system / thread / queue are private to the
 *   ops card (static), so the adaptation surface is a single card;
 * - memory's malloc/free are a **port-internal interface** (the implementation
 *   body of the main card's .malloc/.free, also used across files by
 *   port_kv.c; declarations in include/system/port_memory.h)
 *   -- the core side only goes through card dispatch and never calls this
 *   layer's symbols directly;
 *   calloc is not a card member: core's core_calloc implements it with the
 *   card's .malloc + memset;
 * - thread_get_id is an ops card member (provided unconditionally; p2p-core's
 *   pjproject uses it through core_thread_get_id dispatch) and is static like
 *   the rest of the thread family.
 *
 * Platform implementation notes:
 * - memory: maps directly to the esp-idf system heap (a multi-region heap);
 * - sleep/reset/get_millisecond/get_random: FreeRTOS / esp-idf primitives;
 *   the millisecond clock uses esp_timer (microsecond resolution) rather than
 *   ticks (only 10ms resolution at 100Hz);
 * - semaphore/mutex/queue: native FreeRTOS objects, and the handle is the
 *   object pointer;
 * - thread: xTaskCreate with the stack counted in bytes, and the handle is a
 *   TaskHandle_t.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_error_code.h"
#include "tuya_cloud_types.h"

#include "core_api_ops.h"

/* ---------------------------------------------------------------------------
 * Handle types and timeout macros shared by this file
 * --------------------------------------------------------------------------- */

/* malloc/free are the implementation body of the main card CORE_API_OPS_T's
 * .malloc/.free.
 * **Port-internal interface** (used across files by port_kv.c; declaration in
 * include/system/port_memory.h) -- the core side only goes through card
 * dispatch and never calls this layer's symbols directly. */
#include "port_memory.h"

#define PORT_SEM_WAIT_FOREVER 0xFFFFFFFF

#define PORT_QUEUE_WAIT_FOREVER 0xFFFFFFFF

typedef void (*THREAD_FUNC_T)(void *);

/* esp-idf does not automatically -include sdkconfig.h (its path is in the
 * component includes), so macros such as CONFIG_IDF_TARGET_* must be included
 * explicitly */
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * log output implementation: placed first in the card because it is used early
 * at startup by the core_log callback and QR output.
 * esp-idf's stdout writes straight to the UART (console) through VFS, so
 * printf suffices.
 * --------------------------------------------------------------------------- */
/**
* @brief Output log information
*
* @param[in] str: log information
*
* @note This API is used for outputing log information
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static void __log_output(const char *str, ...)
{
    printf("%s", str);
    return;
}

/* ---------------------------------------------------------------------------
 * memory implementation: malloc/free are the implementation body of the card's
 * .malloc/.free and a **port-internal interface** (used across files by
 * port_kv.c; declaration in include/system/port_memory.h). They map directly to
 * the esp-idf system heap (malloc/free are thread-safe; the esp32 heap is a
 * multi-region block manager).
 * calloc is not a card member: core's core_calloc implements it with the card's
 * .malloc + memset (including the multiplication overflow check).
 * --------------------------------------------------------------------------- */
/**
* @brief Alloc memory of system
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return the memory address malloced
*/
void *port_system_malloc(const SIZE_T size)
{
    return malloc(size);
}

/**
* @brief Free memory of system
*
* @param[in] ptr: memory point
*
* @note This API is used to free memory of system.
*
* @return void
*/
void port_system_free(void *ptr)
{
    if (ptr) {
        free(ptr);
    }
}

/* ---------------------------------------------------------------------------
 * system implementation: sleep/reset/get_millisecond/get_random are only
 * dispatched through the card by the core layer and are static. The millisecond
 * clock uses esp_timer (a microsecond counter) rather than FreeRTOS ticks
 * (with CONFIG_FREERTOS_HZ=100 a tick is only 10ms, which would distort
 * timeout/timestamp calculations).
 * --------------------------------------------------------------------------- */
/**
* @brief System sleep
*
* @param[in] msTime: time in MS
*
* @note This API is used for system sleep.
*
* @return void
*/
static void __system_sleep(const uint32_t num_ms)
{
    uint32_t delay_ms = num_ms;

    /* 1 tick = 10ms (CONFIG_FREERTOS_HZ=100): a delay shorter than 1 tick would
     * be truncated to 0 by pdMS_TO_TICKS and return immediately, so round it up
     * to 10ms */
    if (delay_ms < 10) {
        delay_ms = 10;
    }

    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    return;
}

/**
* @brief System reset
*
* @param void
*
* @note This API is used for system reset.
*
* @return void
*/
static void __system_reset(void)
{
    printf("Rev Restart Req\r\n");
    esp_restart();
    return;
}

/**
* @brief Get system millisecond
*
* @param none
*
* @return system millisecond
*/
static SYS_TIME_T __system_get_millisecond(void)
{
    return (SYS_TIME_T)(esp_timer_get_time() / 1000);
}

/**
* @brief Get a random number in the specified range
*
* @param[in] range: range
*
* @note This API is used for getting a random number in the specified range
*
* @return a random number in the specified range
*/
static int __system_get_random(const uint32_t range)
{
    /* esp_random is the hardware RNG; range=0 returns 0-255 */
    uint32_t value = esp_random() & 0x7FFFFFFF;

    return (0 == range) ? (int)(value & 0xFF) : (int)(value % range);
}

/* ---------------------------------------------------------------------------
 * semaphore implementation (private to the ops card): FreeRTOS counting
 * semaphores, the handle is the object pointer.
 * --------------------------------------------------------------------------- */
/**
* @brief Create semaphore
*
* @param[out] handle: semaphore handle
* @param[in] sem_cnt: semaphore init count
* @param[in] sem_max: semaphore max count
*
* @note This API is used for creating semaphore.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __semaphore_create_init(SEM_HANDLE *handle, const uint32_t sem_cnt, const uint32_t sem_max)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    *handle = (SEM_HANDLE)xSemaphoreCreateCounting(sem_max, sem_cnt);
    if (NULL == *handle) {
        return OPRT_OS_ADAPTER_SEM_CREAT_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Wait semaphore
*
* @param[in] handle: semaphore handle
* @param[in] timeout: wait timeout; 0xFFFFFFFF means wait until the semaphore is obtained
*
* @note This API is used for waiting semaphore.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __semaphore_wait(const SEM_HANDLE handle, const uint32_t timeout)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    BaseType_t ret;
    TickType_t xTicksToWait = portMAX_DELAY;

    if (pdTRUE == xPortInIsrContext()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xSemaphoreTakeFromISR((SemaphoreHandle_t)handle, &xHigherPriorityTaskWoken);
        if (pdTRUE == ret && pdTRUE == xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    } else {
        if (timeout < PORT_SEM_WAIT_FOREVER) {
            xTicksToWait = pdMS_TO_TICKS(timeout);
        }
        ret = xSemaphoreTake((SemaphoreHandle_t)handle, xTicksToWait);
    }

    if (pdTRUE != ret) {
        return OPRT_OS_ADAPTER_SEM_WAIT_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Post semaphore
*
* @param[in] handle: semaphore handle
*
* @note This API is used for posting semaphore.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __semaphore_post(const SEM_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    BaseType_t ret;

    if (pdTRUE == xPortInIsrContext()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xSemaphoreGiveFromISR((SemaphoreHandle_t)handle, &xHigherPriorityTaskWoken);
        if (pdTRUE == ret && pdTRUE == xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    } else {
        ret = xSemaphoreGive((SemaphoreHandle_t)handle);
    }

    if (pdTRUE != ret) {
        return OPRT_OS_ADAPTER_SEM_POST_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Release semaphore
*
* @param[in] handle: semaphore handle
*
* @note This API is used for releasing semaphore.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __semaphore_release(const SEM_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    vSemaphoreDelete((SemaphoreHandle_t)handle);

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * mutex implementation: static (the implementation body of the card's
 * .mutex_* members, also used internally by this file).
 * The core side (tuya_queue, etc.) goes through core_mutex_* and main-card
 * dispatch. Recursive mutex.
 * --------------------------------------------------------------------------- */
/**
* @brief Create mutex
*
* @param[out] pMutexHandle: mutex handle
*
* @note This API is used to create mutex.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __mutex_create_init(MUTEX_HANDLE *handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

#if configUSE_RECURSIVE_MUTEXES
    *handle = (MUTEX_HANDLE)xSemaphoreCreateRecursiveMutex();
#else
    *handle = (MUTEX_HANDLE)xSemaphoreCreateMutex();
#endif
    if (NULL == *handle) {
        return OPRT_OS_ADAPTER_MUTEX_CREAT_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Lock mutex
*
* @param[in] mutexHandle: mutex handle
*
* @note This API is used to lock mutex.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __mutex_lock(const MUTEX_HANDLE handle)
{
    BaseType_t ret;

    if (!handle) {
        return OPRT_INVALID_PARM;
    }

#if configUSE_RECURSIVE_MUTEXES
    ret = xSemaphoreTakeRecursive((SemaphoreHandle_t)handle, portMAX_DELAY);
#else
    ret = xSemaphoreTake((SemaphoreHandle_t)handle, portMAX_DELAY);
#endif
    if (pdTRUE != ret) {
        return OPRT_OS_ADAPTER_MUTEX_LOCK_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Unlock mutex
*
* @param[in] mutexHandle: mutex handle
*
* @note This API is used to unlock mutex.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __mutex_unlock(const MUTEX_HANDLE handle)
{
    BaseType_t ret;

    if (!handle) {
        return OPRT_INVALID_PARM;
    }

#if configUSE_RECURSIVE_MUTEXES
    ret = xSemaphoreGiveRecursive((SemaphoreHandle_t)handle);
#else
    ret = xSemaphoreGive((SemaphoreHandle_t)handle);
#endif
    if (pdTRUE != ret) {
        return OPRT_OS_ADAPTER_MUTEX_UNLOCK_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Release mutex
*
* @param[in] mutexHandle: mutex handle
*
* @note This API is used to release mutex.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __mutex_release(const MUTEX_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    vSemaphoreDelete((SemaphoreHandle_t)handle);

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * thread implementation (private to the ops card): xTaskCreate counts the stack
 * in bytes (converted internally by the esp-idf macro); the handle is a
 * TaskHandle_t.
 * --------------------------------------------------------------------------- */
/**
* @brief Create thread
*
* @param[out] thread: thread handle
* @param[in] name: thread name
* @param[in] stack_size: stack size of thread
* @param[in] priority: priority of thread
* @param[in] func: the main thread process function
* @param[in] arg: the args of the func, can be null
*
* @note This API is used for creating thread.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_create(THREAD_HANDLE *thread, const char *name, uint32_t stack_size,
                                     uint32_t priority, THREAD_FUNC_T func, void *const arg)
{
    if (!thread || !func) {
        return OPRT_INVALID_PARM;
    }

    /* s3 builds get 1KB of extra stack margin: their core tasks have deeper
     * call chains */
#if CONFIG_IDF_TARGET_ESP32S3
    stack_size += 1024;
#endif

    BaseType_t ret = xTaskCreate(func, name, stack_size, arg, priority, (TaskHandle_t *)thread);
    if (ret != pdPASS) {
        return OPRT_OS_ADAPTER_THRD_CREAT_FAILED;
    }

    return OPRT_OK;
}

/**
* @brief Terminal thread and release thread resources
*
* @param[in] thread: thread handle
*
* @note This API is used to terminal thread and release thread resources.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_release(const THREAD_HANDLE thread)
{
    if (!thread) {
        return OPRT_INVALID_PARM;
    }

    /* Supports task self-deletion (vTaskDelete with its own handle) and external
     * deletion */
    vTaskDelete((TaskHandle_t)thread);

    return OPRT_OK;
}

/**
* @brief Set name of self thread
*
* @param[in] name: thread name
*
* @note This API is used to set name of self thread.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_set_self_name(const char *name)
{
    if (!name) {
        return OPRT_INVALID_PARM;
    }

    /* FreeRTOS task names are given at creation (xTaskCreate's name) and there
     * is no public runtime rename API, so this is a no-op (core only uses it
     * for debugging convenience) */
    return OPRT_OK;
}

/**
* @brief Check thread is self thread
*
* @param[in] thread: thread handle
* @param[out] is_self: is self thread or not
*
* @note This API is used to check thread is self thread.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_is_self(const THREAD_HANDLE thread, BOOL_T *is_self)
{
    if (NULL == thread || NULL == is_self) {
        return OPRT_INVALID_PARM;
    }

    *is_self = ((TaskHandle_t)thread == xTaskGetCurrentTaskHandle());
    return OPRT_OK;
}

/**
* @brief Get the handle of the calling thread
*
* @param[out] thread: thread handle
*
* @note p2p-core's pjproject (os_core_tuyaos.c) uses it through
*       core_thread_get_id for TLS row matching and only requires an opaque
*       identity where "the same thread always gets the same value". Under
*       FreeRTOS it returns the current task handle. A card member, provided
*       unconditionally.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_get_id(THREAD_HANDLE *thread)
{
    if (NULL == thread) {
        return OPRT_INVALID_PARM;
    }

    *thread = (THREAD_HANDLE)xTaskGetCurrentTaskHandle();
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * queue implementation (private to the ops card): a FreeRTOS message queue
 * copies msgsize bytes per item by value into the queue's own storage.
 * --------------------------------------------------------------------------- */
/**
 * @brief Create message queue
 *
 * @param[in] msgsize message size
 * @param[in] msgcount message number
 * @param[out] queue the queue handle created
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
static OPERATE_RET __queue_create_init(QUEUE_HANDLE *handle, int msgsize, int msgcount)
{
    if ((NULL == handle) || (msgsize <= 0) || (msgcount <= 0)) {
        return OPRT_INVALID_PARM;
    }

    *handle = (QUEUE_HANDLE)xQueueCreate(msgcount, msgsize);
    if (NULL == *handle) {
        return OPRT_OS_ADAPTER_QUEUE_CREAT_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief post a message to the message queue
 *
 * @param[in] queue the handle of the queue
 * @param[in] data the data of the message
 * @param[in] timeout timeout time
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
static OPERATE_RET __queue_post(const QUEUE_HANDLE handle, void *data, uint32_t timeout)
{
    if (NULL == handle || NULL == data) {
        return OPRT_INVALID_PARM;
    }

    BaseType_t ret;
    TickType_t xTicksToWait = portMAX_DELAY;

    if (pdTRUE == xPortInIsrContext()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xQueueSendFromISR((QueueHandle_t)handle, data, &xHigherPriorityTaskWoken);
        if (pdPASS == ret && pdTRUE == xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    } else {
        if (timeout < PORT_QUEUE_WAIT_FOREVER) {
            xTicksToWait = pdMS_TO_TICKS(timeout);
        }
        ret = xQueueSend((QueueHandle_t)handle, data, xTicksToWait);
    }

    if (pdPASS != ret) {
        return OPRT_OS_ADAPTER_QUEUE_SEND_FAIL;
    }

    return OPRT_OK;
}

/**
 * @brief fetch message from the message queue
 *
 * @param[in] queue the message queue handle
 * @param[inout] msg the message fetch form the message queue
 * @param[in] timeout timeout time
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
static OPERATE_RET __queue_fetch(const QUEUE_HANDLE handle, void *msg, uint32_t timeout)
{
    if (NULL == handle) {
        return OPRT_INVALID_PARM;
    }

    BaseType_t ret;
    TickType_t xTicksToWait = portMAX_DELAY;

    if (pdTRUE == xPortInIsrContext()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xQueueReceiveFromISR((QueueHandle_t)handle, msg, &xHigherPriorityTaskWoken);
        if (pdPASS == ret && pdTRUE == xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    } else {
        if (timeout < PORT_QUEUE_WAIT_FOREVER) {
            xTicksToWait = pdMS_TO_TICKS(timeout);
        }
        ret = xQueueReceive((QueueHandle_t)handle, msg, xTicksToWait);
    }

    if (pdPASS != ret) {
        return OPRT_OS_ADAPTER_QUEUE_RECV_FAIL;
    }

    return OPRT_OK;
}

/**
 * @brief free the message queue
 *
 * @param[in] queue the message queue handle
 *
 * @return void
 */
static void __queue_free(const QUEUE_HANDLE handle)
{
    if (NULL == handle) {
        return;
    }

    vQueueDelete((QueueHandle_t)handle);
}

/* ---------------------------------------------------------------------------
 * Standard platform ops card: assembled from the implementations in this file;
 * core's core_api.c / core_thread.c / core_system.c reference it via
 * extern and complete core_* dispatch for the memory/system/semaphore/mutex/
 * thread/queue groups (log output is fetched directly from the card by the
 * application callback; OTA is injected as callbacks and does not go through
 * this card).
 * --------------------------------------------------------------------------- */
CORE_API_OPS_T core_api_ops_platform = {
    //! log output
    .log_output = __log_output,
    //! memory
    .malloc = port_system_malloc,
    .free = port_system_free,
    //! system
    .sleep = __system_sleep,
    .reset = __system_reset,
    .get_millisecond = __system_get_millisecond,
    .get_random = __system_get_random,
    //! semaphore
    .semaphore_create_init = __semaphore_create_init,
    .semaphore_wait = __semaphore_wait,
    .semaphore_post = __semaphore_post,
    .semaphore_release = __semaphore_release,
    //! mutex
    .mutex_create_init = __mutex_create_init,
    .mutex_lock = __mutex_lock,
    .mutex_unlock = __mutex_unlock,
    .mutex_release = __mutex_release,
    //! thread
    .thread_create = __thread_create,
    .thread_release = __thread_release,
    .thread_set_self_name = __thread_set_self_name,
    .thread_is_self = __thread_is_self,
    .thread_get_id = __thread_get_id,
    //! queue
    .queue_create_init = __queue_create_init,
    .queue_post = __queue_post,
    .queue_fetch = __queue_fetch,
    .queue_free = __queue_free,
};
