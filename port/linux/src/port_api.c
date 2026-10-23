/**
 * @file port_api.c
 * @brief core_api platform ops card assembly and OS primitive implementations (linux port).
 *
 * This file is the platform side of core's core_api module (contract in
 * interface/core_api_ops.h). It implements log output, memory, system,
 * semaphore, mutex, thread and queue primitives and wires them into
 * CORE_API_OPS_T at the end. The CORE layer reaches them through the card; OTA
 * is not a card member (it is callback-injected through core_ota_register_cbs
 * and does not pass through the port):
 * - log output / semaphore / thread / queue implementations are private to the
 *   ops card (static), keeping the adaptation surface to one card;
 * - memory malloc/free are **port-internal interfaces** (they back the card
 *   .malloc/.free members and are also used across files by
 *   port_kv.c; declarations in include/system/port_memory.h). core reaches
 *   them only through card dispatch, never by calling this layer directly;
 *   allocation maps directly onto the platform heap (libc malloc/free on
 *   Linux) with no layer-specific cap or heap statistics;
 *   calloc is not a card member: core's core_calloc builds it from
 *   core_malloc + memset (with an overflow check);
 * - system sleep/reset/get_millisecond/get_random are dispatched only by the
 *   CORE layer through the card, so they are static; the card has no
 *   enter/exit_critical members and no CPU sleep-mode, tick-count,
 *   reset-reason or CPU-info queries;
 * - mutex create_init/lock/unlock/release are **port-internal interfaces**
 *   (they back the card .mutex_* members and serve as the storage lock of
 *   port_kv.c; declarations in include/system/port_mutex.h). core reaches
 *   them through card dispatch only;
 * - thread_get_id is an ops card member provided unconditionally (p2p-core's
 *   pjproject uses it through core_thread_get_id) and, like the rest of the
 *   thread family, is static. Linux returns pthread_self().
 *
 * core's core_api.c exposes the card via core_api_get_ops(); the
 * semaphore/mutex/queue wrappers there, plus core_system.c and core_thread.c,
 * dispatch through that accessor.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_error_code.h"
#include "tuya_cloud_types.h"

#include "core_api_ops.h"

/* ---------------------------------------------------------------------------
 * Handle types and timeout macros shared by the implementations below, plus
 * the port-internal memory/mutex interfaces.
 * --------------------------------------------------------------------------- */

/* malloc/free back the CORE_API_OPS_T .malloc/.free members and are
 * **port-internal interfaces** (used by port_kv.c; declared in
 * include/system/port_memory.h). core only reaches them through the card. */
#include "port_memory.h"
#include "port_mutex.h"

/* semaphore handle and wait-forever timeout */
#define PORT_SEM_WAIT_FOREVER 0xFFFFFFFF

/* queue handle and wait-forever timeout */
#define PORT_QUEUE_WAIT_FOREVER 0xFFFFFFFF

/* thread handle and entry function type */
typedef void (*THREAD_FUNC_T)(void *);

#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#if defined(__linux__) && defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
#include <sys/random.h>
#define PORT_HAS_GETRANDOM 1
#endif

/* ---------------------------------------------------------------------------
 * log output: first member of the card. The application's log callback reads
 * it from the card and forwards to it; that callback is registered with
 * core_log_init and is also used as the QR render target.
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
    fflush(stdout);
    return;
}

/* ---------------------------------------------------------------------------
 * memory: malloc/free back the card .malloc/.free members and are
 * **port-internal interfaces** (used by port_kv.c; declared in
 * include/system/port_memory.h). They map directly onto the platform heap
 * (libc malloc/free on Linux); this layer keeps no cap and no heap statistics.
 * calloc is not a card member; memset/memcpy are provided by the C library.
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
 * system: sleep/reset/get_millisecond/get_random are dispatched by the CORE
 * layer through the card, hence static. The card has no enter/exit_critical
 * members and no CPU sleep-mode, tick-count, reset-reason or CPU-info queries.
 * --------------------------------------------------------------------------- */
// Kernel CSPRNG, so the values are not a fixed sequence like unseeded rand().
static int __sys_random_bytes(uint8_t *buf, size_t len)
{
#if defined(PORT_HAS_GETRANDOM)
    size_t done = 0;

    while (done < len) {
        ssize_t got = getrandom(buf + done, len - done, 0);
        if (got <= 0) {
            if (EINTR == errno) {
                continue;
            }
            break;
        }
        done += (size_t)got;
    }

    if (done == len) {
        return 0;
    }
#endif

    FILE *fp = fopen("/dev/urandom", "rb");
    if (NULL == fp) {
        return -1;
    }

    size_t rd = fread(buf, 1, len, fp);
    fclose(fp);

    return (rd == len) ? 0 : -1;
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
    struct timespec time1 = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time1);
    return 1000*((uint64_t)time1.tv_sec) + ((uint64_t)time1.tv_nsec)/1000000;
}

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
    TIME_S sTmpTime;
    TIME_MS msTmpTime;

    sTmpTime = num_ms / 1000;
    msTmpTime = num_ms % 1000;

    if(sTmpTime)
        sleep(sTmpTime);

    if(msTmpTime)
        usleep(msTmpTime*1000);

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
    exit(0);
    return;
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
    uint32_t value = 0;

    if (0 != __sys_random_bytes((uint8_t *)&value, sizeof(value))) {
        /* Both kernel sources failed; seed rand() once so values are not constant */
        static int s_seeded = 0;
        if (!s_seeded) {
            struct timespec ts = {0, 0};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            srand((unsigned int)ts.tv_nsec ^ (unsigned int)getpid());
            s_seeded = 1;
        }
        value = (uint32_t)rand();
    }

    value &= 0x7FFFFFFF; // keep the documented non-negative int contract

    return (0 == range) ? (int)(value & 0xFF) : (int)(value % range);
}

/* ---------------------------------------------------------------------------
 * semaphore implementation (card-private)
 * --------------------------------------------------------------------------- */
typedef struct
{
    sem_t sem;
} PORT_SEM_MANAGE, *P_PORT_SEM_MANAGE;

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

    P_PORT_SEM_MANAGE sem_manage;
    sem_manage = (P_PORT_SEM_MANAGE)port_system_malloc(sizeof(PORT_SEM_MANAGE));
    if (sem_manage == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    int ret;
    ret = sem_init(&sem_manage->sem, 0, sem_cnt);
    if (ret != 0) {
        port_system_free(sem_manage);
        *handle = NULL;
        return ret;
    }

    *handle = (SEM_HANDLE)sem_manage;
    return OPRT_OK;
}

/**
* @brief Wait semaphore
*
* @param[in] handle: semaphore handle
* @param[in] timeout: wait timeout, PORT_SEM_WAIT_FOREVER means wait until get semaphore
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

    P_PORT_SEM_MANAGE sem_manage;
    sem_manage = (P_PORT_SEM_MANAGE)handle;

    int ret;
    if (timeout == PORT_SEM_WAIT_FOREVER) {
        ret = sem_wait(&sem_manage->sem);
    } else {
        struct timespec ts = {0, 0};

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (timeout / 1000);
        ts.tv_nsec += ((timeout % 1000) * 1000000);
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec = ts.tv_nsec % 1000000000;
        }

        while ((ret = sem_timedwait(&sem_manage->sem, &ts)) == -1 && errno == EINTR) {
            continue; /* Restart if interrupted by handler */
        }
    }

    if (0 != ret) {
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

    P_PORT_SEM_MANAGE sem_manage;
    sem_manage = (P_PORT_SEM_MANAGE)handle;

    int ret;
    ret = sem_post(&(sem_manage->sem));
    if (ret != 0) {
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

    P_PORT_SEM_MANAGE sem_manage;
    sem_manage = (P_PORT_SEM_MANAGE)handle;

    int ret;
    ret = sem_destroy(&(sem_manage->sem));
    port_system_free(handle); /* release the semaphore bookkeeping struct */
    if (ret != 0) {
        return OPRT_OS_ADAPTER_SEM_RELEASE_FAILED;
    }

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * mutex: **port-internal interface** (backs the card .mutex_* members and the
 * storage lock of port_kv.c; declared in include/system/port_mutex.h). core
 * (tuya_queue and friends) goes through core_mutex_* card dispatch.
 * --------------------------------------------------------------------------- */
typedef pthread_mutex_t PORT_THRD_MUTEX;
typedef struct
{
    PORT_THRD_MUTEX mutex;
} PORT_MUTEX_MANAGE, *P_PORT_MUTEX_MANAGE;

/**
* @brief Create mutex
*
* @param[out] pMutexHandle: mutex handle
*
* @note This API is used to create mutex.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_mutex_create_init(MUTEX_HANDLE *handle)
{
    if (!handle)
        return OPRT_INVALID_PARM;

    P_PORT_MUTEX_MANAGE mutex_manage;
    mutex_manage = (P_PORT_MUTEX_MANAGE)port_system_malloc(sizeof(PORT_MUTEX_MANAGE));
    if (!(mutex_manage))
        return OPRT_MALLOC_FAILED;

    int ret;
    pthread_mutexattr_t attr;

    ret = pthread_mutexattr_init(&attr);
    if (0 != ret) {
        return OPRT_OS_ADAPTER_MUTEX_CREAT_FAILED;
    }

    ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (0 != ret) {
        return OPRT_OS_ADAPTER_MUTEX_CREAT_FAILED;
    }

    ret = pthread_mutex_init(&(mutex_manage->mutex), &attr);
    if (0 != ret) {
        return OPRT_OS_ADAPTER_MUTEX_CREAT_FAILED;
    }

    ret = pthread_mutexattr_destroy(&attr);
    if (0 != ret) {
        return OPRT_OS_ADAPTER_MUTEX_RELEASE_FAILED;
    }

    *handle = (MUTEX_HANDLE)mutex_manage;

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
OPERATE_RET port_mutex_lock(const MUTEX_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    P_PORT_MUTEX_MANAGE mutex_manage;
    mutex_manage = (P_PORT_MUTEX_MANAGE)handle;

    int ret;
    ret = pthread_mutex_lock(&(mutex_manage->mutex));
    if (ret != 0) {
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
OPERATE_RET port_mutex_unlock(const MUTEX_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    P_PORT_MUTEX_MANAGE mutex_manage;
    mutex_manage = (P_PORT_MUTEX_MANAGE)handle;

    int ret;
    ret = pthread_mutex_unlock(&(mutex_manage->mutex));
    if (ret != 0) {
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
OPERATE_RET port_mutex_release(const MUTEX_HANDLE handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    P_PORT_MUTEX_MANAGE mutex_manage;
    mutex_manage = (P_PORT_MUTEX_MANAGE)handle;

    int ret;
    ret = pthread_mutex_destroy(&(mutex_manage->mutex));
    if (ret != 0) {
        return OPRT_OS_ADAPTER_MUTEX_RELEASE_FAILED;
    }

    port_system_free(handle);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * thread implementation (card-private)
 * --------------------------------------------------------------------------- */
typedef struct {
    pthread_t id;
    THREAD_FUNC_T func;
    void *arg;
} THREAD_DATA;

static void *__thread_wrap_func(void *arg)
{
    THREAD_DATA *thread_data = (THREAD_DATA *)arg;
    if (thread_data && thread_data->func) {
        thread_data->func(thread_data->arg);
    }

    return NULL;
}

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
    if (!thread) {
        return OPRT_INVALID_PARM;
    }

    int ret = 0;
    THREAD_DATA *thread_data = (THREAD_DATA *)port_system_malloc(sizeof(THREAD_DATA));
    if (thread_data == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    memset(thread_data, 0, sizeof(THREAD_DATA));
    thread_data->id = 0;
    thread_data->func = func;
    thread_data->arg = arg;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    ret = pthread_create(&(thread_data->id), &attr, __thread_wrap_func, thread_data);
    pthread_attr_destroy(&attr);
    if (0 != ret) {
        return OPRT_OS_ADAPTER_THRD_CREAT_FAILED;
    }

    *thread = (THREAD_HANDLE)thread_data;
    return ret;
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

    THREAD_DATA *thread_data = (THREAD_DATA *)thread;
    port_system_free(thread_data);

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

    prctl(PR_SET_NAME, name);
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

    THREAD_DATA *thread_data = (THREAD_DATA *)thread;

    *is_self = pthread_equal(thread_data->id, pthread_self());
    return OPRT_OK;
}

/**
* @brief Get the handle of the calling thread
*
* @param[out] thread: thread handle
*
* @note p2p-core's pjproject (os_core_tuyaos.c) uses this through
*       core_thread_get_id to match its TLS rows: it only needs an opaque
*       identity that stays the same for one thread, not the handle returned
*       by __thread_create (a THREAD_DATA* here, consumed by is_self only).
*       Linux returns pthread_self(). Unconditional card member.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
static OPERATE_RET __thread_get_id(THREAD_HANDLE *thread)
{
    if (NULL == thread) {
        return OPRT_INVALID_PARM;
    }

    *thread = (THREAD_HANDLE)pthread_self();
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * queue implementation (card-private)
 * --------------------------------------------------------------------------- */
typedef struct rpa_queue_t {
    void **data;
    volatile uint32_t nelts; /**< # elements */
    uint32_t in;             /**< next empty location */
    uint32_t out;            /**< next filled location */
    uint32_t bounds;         /**< max size of queue */
    uint32_t full_waiters;
    uint32_t empty_waiters;
    pthread_mutex_t *one_big_mutex;
    pthread_cond_t *not_empty;
    pthread_cond_t *not_full;
    int terminated;
} rpa_queue_t;

#define RPA_WAIT_NONE 0
#define RPA_WAIT_FOREVER -1

#define rpa_queue_full(queue) ((queue)->nelts == (queue)->bounds)
#define rpa_queue_empty(queue) ((queue)->nelts == 0)

static void set_timeout(struct timespec *abstime, int wait_ms)
{
    clock_gettime(CLOCK_REALTIME, abstime);
    /* add seconds */
    abstime->tv_sec += (wait_ms / 1000);
    /* add and carry microseconds */
    long ms = abstime->tv_nsec / 1000000L;
    ms += wait_ms % 1000;
    while (ms > 1000) {
        ms -= 1000;
        abstime->tv_sec += 1;
    }
    abstime->tv_nsec = ms * 1000000L;
}

static void rpa_queue_destroy(rpa_queue_t *queue)
{
    /* Ignore errors here, we can't do anything about them anyway. */
    pthread_cond_destroy(queue->not_empty);
    pthread_cond_destroy(queue->not_full);
    pthread_mutex_destroy(queue->one_big_mutex);
}

static BOOL_T rpa_queue_create(rpa_queue_t **q, uint32_t queue_capacity)
{
    rpa_queue_t *queue;
    queue = malloc(sizeof(rpa_queue_t));
    if (!queue) {
        return false;
    }
    *q = queue;
    memset(queue, 0, sizeof(rpa_queue_t));

    if (!(queue->one_big_mutex = malloc(sizeof(pthread_mutex_t))))
        return false;
    if (!(queue->not_empty = malloc(sizeof(pthread_cond_t))))
        return false;
    if (!(queue->not_full = malloc(sizeof(pthread_cond_t))))
        return false;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    int rv = pthread_mutex_init(queue->one_big_mutex, &attr);
    if (rv != 0) {
        goto error;
    }

    rv = pthread_cond_init(queue->not_empty, NULL);
    if (rv != 0) {
        goto error;
    }

    rv = pthread_cond_init(queue->not_full, NULL);
    if (rv != 0) {
        goto error;
    }

    /* Set all the data in the queue to NULL */
    queue->data = malloc(queue_capacity * sizeof(void *));
    queue->bounds = queue_capacity;
    queue->nelts = 0;
    queue->in = 0;
    queue->out = 0;
    queue->terminated = 0;
    queue->full_waiters = 0;
    queue->empty_waiters = 0;

    return true;

error:
    free(queue);
    return false;
}

static BOOL_T rpa_queue_trypush(rpa_queue_t *queue, void *data)
{
    BOOL_T rv;

    if (queue->terminated) {
        return false; /* no more elements ever again */
    }

    rv = pthread_mutex_lock(queue->one_big_mutex);
    if (rv != 0) {
        return false;
    }

    if (rpa_queue_full(queue)) {
        rv = pthread_mutex_unlock(queue->one_big_mutex);
        return false; // EAGAIN;
    }

    queue->data[queue->in] = data;
    queue->in++;
    if (queue->in >= queue->bounds) {
        queue->in -= queue->bounds;
    }
    queue->nelts++;

    if (queue->empty_waiters) {
        rv = pthread_cond_signal(queue->not_empty);
        if (rv != 0) {
            pthread_mutex_unlock(queue->one_big_mutex);
            return false;
        }
    }

    pthread_mutex_unlock(queue->one_big_mutex);
    return true;
}

static BOOL_T rpa_queue_timedpush(rpa_queue_t *queue, void *data, int wait_ms)
{
    BOOL_T rv;

    if (wait_ms == RPA_WAIT_NONE)
        return rpa_queue_trypush(queue, data);

    if (queue->terminated) {
        return false; /* no more elements ever again */
    }

    rv = pthread_mutex_lock(queue->one_big_mutex);
    if (rv != 0) {
        return false;
    }

    if (rpa_queue_full(queue)) {
        if (!queue->terminated) {
            queue->full_waiters++;
            if (wait_ms == RPA_WAIT_FOREVER) {
                rv = pthread_cond_wait(queue->not_full, queue->one_big_mutex);
            } else {
                struct timespec abstime;
                set_timeout(&abstime, wait_ms);
                rv = pthread_cond_timedwait(queue->not_full, queue->one_big_mutex, &abstime);
            }
            queue->full_waiters--;
            if (rv != 0) {
                pthread_mutex_unlock(queue->one_big_mutex);
                return false;
            }
        }
        /* If we wake up and it's still full, then we were interrupted */
        if (rpa_queue_full(queue)) {
            rv = pthread_mutex_unlock(queue->one_big_mutex);
            if (rv != 0) {
                return false;
            }
            if (queue->terminated) {
                return false; /* no more elements ever again */
            } else {
                return false; // EINTR;
            }
        }
    }

    queue->data[queue->in] = data;
    queue->in++;
    if (queue->in >= queue->bounds) {
        queue->in -= queue->bounds;
    }
    queue->nelts++;

    if (queue->empty_waiters) {
        rv = pthread_cond_signal(queue->not_empty);
        if (rv != 0) {
            pthread_mutex_unlock(queue->one_big_mutex);
            return false;
        }
    }

    pthread_mutex_unlock(queue->one_big_mutex);
    return true;
}

static BOOL_T rpa_queue_trypop(rpa_queue_t *queue, void **data)
{
    BOOL_T rv;

    if (queue->terminated) {
        return false; /* no more elements ever again */
    }

    rv = pthread_mutex_lock(queue->one_big_mutex);
    if (rv != 0) {
        return false;
    }

    if (rpa_queue_empty(queue)) {
        rv = pthread_mutex_unlock(queue->one_big_mutex);
        return false; // EAGAIN;
    }

    *data = queue->data[queue->out];
    queue->nelts--;

    queue->out++;
    if (queue->out >= queue->bounds) {
        queue->out -= queue->bounds;
    }
    if (queue->full_waiters) {
        rv = pthread_cond_signal(queue->not_full);
        if (rv != 0) {
            pthread_mutex_unlock(queue->one_big_mutex);
            return false;
        }
    }

    pthread_mutex_unlock(queue->one_big_mutex);
    return true;
}

static BOOL_T rpa_queue_timedpop(rpa_queue_t *queue, void **data, int wait_ms)
{
    BOOL_T rv;

    if (wait_ms == RPA_WAIT_NONE)
        return rpa_queue_trypop(queue, data);

    if (queue->terminated) {
        return false; /* no more elements ever again */
    }

    rv = pthread_mutex_lock(queue->one_big_mutex);
    if (rv != 0) {
        return false;
    }

    /* Keep waiting until we wake up and find that the queue is not empty. */
    if (rpa_queue_empty(queue)) {
        if (!queue->terminated) {
            queue->empty_waiters++;
            if (wait_ms == RPA_WAIT_FOREVER) {
                rv = pthread_cond_wait(queue->not_empty, queue->one_big_mutex);
            } else {
                struct timespec abstime;
                set_timeout(&abstime, wait_ms);
                rv = pthread_cond_timedwait(queue->not_empty, queue->one_big_mutex, &abstime);
            }
            queue->empty_waiters--;
            if (rv != 0) {
                pthread_mutex_unlock(queue->one_big_mutex);
                return false;
            }
        }
        /* If we wake up and it's still empty, then we were interrupted */
        if (rpa_queue_empty(queue)) {
            rv = pthread_mutex_unlock(queue->one_big_mutex);
            if (rv != 0) {
                return false;
            }
            if (queue->terminated) {
                return false; /* no more elements ever again */
            } else {
                return false; // EINTR;
            }
        }
    }

    *data = queue->data[queue->out];
    queue->nelts--;

    queue->out++;
    if (queue->out >= queue->bounds) {
        queue->out -= queue->bounds;
    }
    if (queue->full_waiters) {
        rv = pthread_cond_signal(queue->not_full);
        if (rv != 0) {
            pthread_mutex_unlock(queue->one_big_mutex);
            return false;
        }
    }

    pthread_mutex_unlock(queue->one_big_mutex);
    return true;
}

typedef struct {
    rpa_queue_t *queue;
    int msgsize;
} PORT_QUEUE_T;

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
    PORT_QUEUE_T *queue = NULL;

    if ((NULL == handle) || (0 == msgsize) || (0 == msgcount)) {
        return OPRT_INVALID_PARM;
    }

    queue = (PORT_QUEUE_T *)malloc(sizeof(PORT_QUEUE_T));
    if (!queue) {
        return OPRT_MALLOC_FAILED;
    }

    if (!rpa_queue_create(&queue->queue, msgcount)) {
        return OPRT_OS_ADAPTER_QUEUE_CREAT_FAILED;
    }
    queue->msgsize = msgsize;

    *handle = (QUEUE_HANDLE)queue;

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

    PORT_QUEUE_T *queue = (PORT_QUEUE_T *)handle;
    int wait_ms = 0;

    void *buf = (void *)malloc(queue->msgsize);
    if (NULL == buf) {
        return OPRT_MALLOC_FAILED;
    }

    memcpy(buf, (void *)data, queue->msgsize);

    if (timeout == PORT_QUEUE_WAIT_FOREVER) {
        wait_ms = RPA_WAIT_FOREVER;
    } else {
        wait_ms = timeout;
    }

    if (!rpa_queue_timedpush(queue->queue, buf, wait_ms)) {
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

    PORT_QUEUE_T *queue = (PORT_QUEUE_T *)handle;
    void *buf = NULL;
    int wait_ms;

    if (timeout == PORT_QUEUE_WAIT_FOREVER) {
        wait_ms = RPA_WAIT_FOREVER;
    } else {
        wait_ms = timeout;
    }

    if (!rpa_queue_timedpop(queue->queue, (void **)&buf, wait_ms)) {
        return OPRT_OS_ADAPTER_QUEUE_RECV_FAIL;
    }

    if (buf) {
        memcpy((void *)msg, buf, queue->msgsize);
        free(buf);
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

    PORT_QUEUE_T *queue = (PORT_QUEUE_T *)handle;
    rpa_queue_destroy(queue->queue);
    free(queue);
}

/* ---------------------------------------------------------------------------
 * Platform ops card: assembled from the implementations in this file and
 * referenced by core's core_api.c through core_api_get_ops(); the
 * semaphore/mutex/queue wrappers there, plus core_system.c and core_thread.c,
 * dispatch through that accessor. Log output is read directly from the card by
 * the application callback; OTA is callback-injected and not part of this card.
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
    .mutex_create_init = port_mutex_create_init,
    .mutex_lock = port_mutex_lock,
    .mutex_unlock = port_mutex_unlock,
    .mutex_release = port_mutex_release,
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
