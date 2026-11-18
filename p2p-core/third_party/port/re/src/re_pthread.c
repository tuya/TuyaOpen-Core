/**
 * @file re_pthread.c
 * @brief Portable pthread subset for libre, built on the TuyaOpen CORE API.
 *
 * libre is written against POSIX threads. Some C libraries declare pthread
 * but implement nothing usable, so this file provides the subset libre uses
 * on top of core_thread, core_mutex, core_semaphore and core_thread TLS.
 * Hosts with a native pthread keep using it; the calls inside libre are
 * mapped to re_pthread_* by re_platform_config.h (included from re_types.h),
 * so both can coexist.
 *
 * The pthread object types differ between C libraries (handle-sized values vs.
 * larger unions). A pointer to our own bookkeeping structure is therefore
 * stored through memcpy (never a cast), and the compile-time checks below make
 * sure every type is large enough to hold it.
 *
 * @copyright Copyright (c) 2026 Tuya Inc. All Rights Reserved.
 */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pthread.h"
#include "core_thread.h"
#include "core_mutex.h"
#include "core_semaphore.h"
#include "core_system.h"

#ifndef RE_TUYAOPEN_THREAD_STACK
#define RE_TUYAOPEN_THREAD_STACK (8 * 1024)
#endif

/* The CORE thread id is a THREAD_HANDLE; pthread_t must be able to carry it */
typedef char re_pthread_t_fits[(sizeof(pthread_t) >= sizeof(void *)) ? 1 : -1];
/* Our bookkeeping pointer is stored in the pthread object itself */
typedef char re_pthread_mutex_fits[(sizeof(pthread_mutex_t) >= sizeof(void *)) ? 1 : -1];
typedef char re_pthread_cond_fits[(sizeof(pthread_cond_t) >= sizeof(void *)) ? 1 : -1];
/* The attribute object carries one int (recursive flag) */
typedef char re_pthread_mutexattr_fits[(sizeof(pthread_mutexattr_t) >= sizeof(int)) ? 1 : -1];
/* pthread_once state is kept as one int in the platform object */
typedef char re_pthread_once_fits[(sizeof(pthread_once_t) >= sizeof(int)) ? 1 : -1];

struct re_pthread_mutex {
    MUTEX_HANDLE lock;
    THREAD_HANDLE owner;
    int count;
    int recursive;
};

struct re_pthread_cond {
    SEM_HANDLE sem;
};

struct re_thread_start {
    void *(*fn)(void *);
    void *arg;
    SEM_HANDLE done;
    THREAD_HANDLE handle;
    pthread_t id;
    int detached;
    void *ret;
    struct re_thread_start *next;
};

static MUTEX_HANDLE s_lock;
static int s_lock_state; /* 0 = not created, 1 = creating, 2 = ready */
static struct re_thread_start *s_threads;

/* ---------------------------------------------------------------------------
 * Platform-object storage helpers
 * ------------------------------------------------------------------------- */

/* Zero and all-ones are reserved: an untouched object reads as "invalid" */
#define RE_PTHREAD_SLOT_INVALID ((uintptr_t)~(uintptr_t)0)

static void *slot_load(const void *storage, size_t size)
{
    uintptr_t v = 0;

    if (storage == NULL || size < sizeof(v)) {
        return NULL;
    }
    memcpy(&v, storage, sizeof(v));
    if (v == 0 || v == RE_PTHREAD_SLOT_INVALID) {
        return NULL;
    }
    return (void *)v;
}

static void slot_store(void *storage, size_t size, void *p)
{
    uintptr_t v = (uintptr_t)p;

    memset(storage, 0, size);
    memcpy(storage, &v, sizeof(v));
}

static void slot_clear(void *storage, size_t size)
{
    memset(storage, 0, size);
}

static pthread_t id_from_handle(THREAD_HANDLE h)
{
    pthread_t id;

    memset(&id, 0, sizeof(id));
    memcpy(&id, &h, sizeof(h));
    return id;
}

static THREAD_HANDLE handle_from_id(pthread_t id)
{
    THREAD_HANDLE h = NULL;

    memcpy(&h, &id, sizeof(h));
    return h;
}

static int id_equal(pthread_t a, pthread_t b)
{
    return (memcmp(&a, &b, sizeof(a)) == 0) ? 1 : 0;
}

/* Lazily create the shim's own lock without pthread_once (that would need a
 * lock itself) and with an atomic state so concurrent first calls are safe. */
static void ensure_lock(void)
{
    if (__atomic_load_n(&s_lock_state, __ATOMIC_ACQUIRE) == 2) {
        return;
    }
    if (__atomic_exchange_n(&s_lock_state, 1, __ATOMIC_ACQ_REL) == 0) {
        (void)core_mutex_create_init(&s_lock);
        __atomic_store_n(&s_lock_state, 2, __ATOMIC_RELEASE);
        return;
    }
    while (__atomic_load_n(&s_lock_state, __ATOMIC_ACQUIRE) != 2) {
        core_system_sleep(1);
    }
}

static THREAD_HANDLE self_handle(void)
{
    THREAD_HANDLE id = NULL;

    (void)core_thread_get_id(&id);
    return id;
}

static void thread_entry(void *arg)
{
    struct re_thread_start *st = arg;

    st->ret = st->fn(st->arg);
    core_semaphore_post(st->done);

    /* core_thread has no safe self-delete primitive: keep the small
     * completion record (and its handle) allocated. Detach is not used by the
     * P2P subset. */
}

/* ---------------------------------------------------------------------------
 * Thread API
 * ------------------------------------------------------------------------- */

int re_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                      void *(*start_routine)(void *), void *arg)
{
    THREAD_CFG_T cfg;
    struct re_thread_start *st;
    THREAD_HANDLE handle = NULL;
    OPERATE_RET rt;

    (void)attr;
    if (thread == NULL || start_routine == NULL) {
        return EINVAL;
    }

    st = calloc(1, sizeof(*st));
    if (st == NULL) {
        return ENOMEM;
    }
    st->fn = start_routine;
    st->arg = arg;
    if (core_semaphore_create_init(&st->done, 0, 1) != OPRT_OK) {
        free(st);
        return ENOMEM;
    }

    ensure_lock();
    core_mutex_lock(s_lock);
    st->next = s_threads;
    s_threads = st;
    core_mutex_unlock(s_lock);

    memset(&cfg, 0, sizeof(cfg));
    cfg.stackDepth = RE_TUYAOPEN_THREAD_STACK;
    cfg.priority = THREAD_PRIO_2;
    cfg.thrdname = "re";

    rt = core_thread_create_and_start(&handle, NULL, NULL, thread_entry, st, &cfg);
    if (rt != OPRT_OK) {
        ensure_lock();
        core_mutex_lock(s_lock);
        if (s_threads == st) {
            s_threads = st->next;
        } else {
            struct re_thread_start *p = s_threads;
            while (p != NULL && p->next != st) {
                p = p->next;
            }
            if (p != NULL) {
                p->next = st->next;
            }
        }
        core_mutex_unlock(s_lock);
        core_semaphore_release(st->done);
        free(st);
        return EAGAIN;
    }

    ensure_lock();
    core_mutex_lock(s_lock);
    st->handle = handle;
    st->id = id_from_handle(handle);
    core_mutex_unlock(s_lock);

    *thread = st->id;
    return 0;
}

int re_pthread_join(pthread_t thread, void **retval)
{
    struct re_thread_start *st;
    struct re_thread_start *prev = NULL;
    struct re_thread_start *p;

    ensure_lock();
    core_mutex_lock(s_lock);
    for (p = s_threads; p != NULL; prev = p, p = p->next) {
        if (id_equal(p->id, thread)) {
            break;
        }
    }
    if (p == NULL || p->detached) {
        core_mutex_unlock(s_lock);
        return ESRCH;
    }
    st = p;
    if (prev != NULL) {
        prev->next = st->next;
    } else {
        s_threads = st->next;
    }
    core_mutex_unlock(s_lock);

    (void)core_semaphore_wait(st->done, SEM_WAIT_FOREVER);
    if (retval != NULL) {
        *retval = st->ret;
    }
    (void)core_thread_delete(st->handle);
    core_semaphore_release(st->done);
    free(st);
    return 0;
}

int re_pthread_detach(pthread_t thread)
{
    struct re_thread_start *st;

    ensure_lock();
    core_mutex_lock(s_lock);
    for (st = s_threads; st != NULL; st = st->next) {
        if (id_equal(st->id, thread)) {
            st->detached = 1;
            core_mutex_unlock(s_lock);
            return 0;
        }
    }
    core_mutex_unlock(s_lock);
    return ESRCH;
}

pthread_t re_pthread_self(void)
{
    return id_from_handle(self_handle());
}

int re_pthread_equal(pthread_t t1, pthread_t t2)
{
    return id_equal(t1, t2);
}

void re_pthread_exit(void *retval)
{
    (void)retval;
    /* Not used by the P2P subset; returning from the thread function is the
     * supported exit path. */
    for (;;) {
        core_system_sleep(1000);
    }
}

int re_pthread_setname_np(pthread_t thread, const char *name)
{
    (void)thread;
    (void)name;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Mutex API
 * ------------------------------------------------------------------------- */

/* The attribute object only carries the recursive flag into mutex_init */
static void attr_store(pthread_mutexattr_t *attr, int recursive)
{
    memset(attr, 0, sizeof(*attr));
    memcpy(attr, &recursive, sizeof(recursive));
}

static int attr_recursive(const pthread_mutexattr_t *attr)
{
    int v = 0;

    memcpy(&v, attr, sizeof(v));
    return (v == 1) ? 1 : 0;
}

int re_pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    if (attr == NULL) {
        return EINVAL;
    }
    attr_store(attr, 0);
    return 0;
}

int re_pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    if (attr == NULL) {
        return EINVAL;
    }
    attr_store(attr, (type == PTHREAD_MUTEX_RECURSIVE) ? 1 : 0);
    return 0;
}

int re_pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    if (attr != NULL) {
        attr_store(attr, 0);
    }
    return 0;
}

static struct re_pthread_mutex *mutex_ptr(pthread_mutex_t *mutex)
{
    return (struct re_pthread_mutex *)slot_load(mutex, sizeof(*mutex));
}

int re_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    struct re_pthread_mutex *m;

    if (mutex == NULL) {
        return EINVAL;
    }
    m = calloc(1, sizeof(*m));
    if (m == NULL) {
        return ENOMEM;
    }
    if (core_mutex_create_init(&m->lock) != OPRT_OK) {
        free(m);
        return ENOMEM;
    }
    m->recursive = (attr != NULL && attr_recursive(attr)) ? 1 : 0;
    slot_store(mutex, sizeof(*mutex), m);
    return 0;
}

int re_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    struct re_pthread_mutex *m = mutex_ptr(mutex);
    THREAD_HANDLE self;

    if (m == NULL) {
        return EINVAL;
    }
    self = self_handle();
    if (m->recursive && m->owner == self) {
        m->count++;
        return 0;
    }
    if (core_mutex_lock(m->lock) != OPRT_OK) {
        return EINVAL;
    }
    m->owner = self;
    m->count = 1;
    return 0;
}

int re_pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    /* core_mutex has no trylock; the P2P subset does not call this. Treat it
     * as a normal lock rather than returning a false busy. */
    return re_pthread_mutex_lock(mutex);
}

int re_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    struct re_pthread_mutex *m = mutex_ptr(mutex);
    THREAD_HANDLE self;

    if (m == NULL) {
        return EINVAL;
    }
    self = self_handle();
    if (m->recursive && m->owner == self && m->count > 1) {
        m->count--;
        return 0;
    }
    m->owner = NULL;
    m->count = 0;
    return (core_mutex_unlock(m->lock) == OPRT_OK) ? 0 : EINVAL;
}

int re_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    struct re_pthread_mutex *m = mutex_ptr(mutex);

    if (m == NULL) {
        return EINVAL;
    }
    (void)core_mutex_release(m->lock);
    free(m);
    slot_clear(mutex, sizeof(*mutex));
    return 0;
}

/* ---------------------------------------------------------------------------
 * Condition variable API
 * ------------------------------------------------------------------------- */

static struct re_pthread_cond *cond_ptr(pthread_cond_t *cond)
{
    return (struct re_pthread_cond *)slot_load(cond, sizeof(*cond));
}

int re_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    struct re_pthread_cond *c;

    (void)attr;
    if (cond == NULL) {
        return EINVAL;
    }
    c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return ENOMEM;
    }
    if (core_semaphore_create_init(&c->sem, 0, 1) != OPRT_OK) {
        free(c);
        return ENOMEM;
    }
    slot_store(cond, sizeof(*cond), c);
    return 0;
}

int re_pthread_cond_signal(pthread_cond_t *cond)
{
    struct re_pthread_cond *c = cond_ptr(cond);

    if (c == NULL) {
        return EINVAL;
    }
    return (core_semaphore_post(c->sem) == OPRT_OK) ? 0 : EINVAL;
}

int re_pthread_cond_broadcast(pthread_cond_t *cond)
{
    /* Single-waiter semaphore: async.c has one waiter; broadcast maps to one
     * post. The P2P subset never creates an async worker. */
    return re_pthread_cond_signal(cond);
}

int re_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    struct re_pthread_cond *c = cond_ptr(cond);
    int err;

    if (c == NULL || mutex == NULL) {
        return EINVAL;
    }
    (void)re_pthread_mutex_unlock(mutex);
    err = (core_semaphore_wait(c->sem, SEM_WAIT_FOREVER) == OPRT_OK) ? 0 : EINVAL;
    (void)re_pthread_mutex_lock(mutex);
    return err;
}

int re_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                              const struct timespec *abstime)
{
    struct re_pthread_cond *c = cond_ptr(cond);
    uint32_t ms;
    time_t now;
    int err;

    if (c == NULL || mutex == NULL || abstime == NULL) {
        return EINVAL;
    }
    now = time(NULL);
    if (abstime->tv_sec <= now) {
        return ETIMEDOUT;
    }
    ms = (uint32_t)((abstime->tv_sec - now) * 1000ULL +
                    (abstime->tv_nsec / 1000000ULL));
    (void)re_pthread_mutex_unlock(mutex);
    err = (core_semaphore_wait(c->sem, ms) == OPRT_OK) ? 0 : ETIMEDOUT;
    (void)re_pthread_mutex_lock(mutex);
    return err;
}

int re_pthread_cond_destroy(pthread_cond_t *cond)
{
    struct re_pthread_cond *c = cond_ptr(cond);

    if (c == NULL) {
        return EINVAL;
    }
    (void)core_semaphore_release(c->sem);
    free(c);
    slot_clear(cond, sizeof(*cond));
    return 0;
}

/* ---------------------------------------------------------------------------
 * Thread-specific data: backed by core_thread TLS so the destructors run on
 * every core thread's exit (including threads libre did not create itself).
 * ------------------------------------------------------------------------- */

int re_pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    THREAD_TLS_KEY core_key = 0;
    OPERATE_RET ret;

    if (key == NULL) {
        return EINVAL;
    }
    ret = core_thread_tls_key_create(&core_key, destructor);
    if (ret != OPRT_OK) {
        return EAGAIN;
    }
    *key = (pthread_key_t)core_key;
    return 0;
}

int re_pthread_setspecific(pthread_key_t key, const void *value)
{
    return (core_thread_tls_set((THREAD_TLS_KEY)key, (void *)value) == OPRT_OK) ? 0 : EINVAL;
}

void *re_pthread_getspecific(pthread_key_t key)
{
    return core_thread_tls_get((THREAD_TLS_KEY)key);
}

int re_pthread_key_delete(pthread_key_t key)
{
    return (core_thread_tls_key_delete((THREAD_TLS_KEY)key) == OPRT_OK) ? 0 : EINVAL;
}

/* ---------------------------------------------------------------------------
 * One-time initialization
 *
 * The once_t layout differs between C libraries (struct vs. int), so the
 * state is kept as one int inside the caller's object and updated atomically.
 * ------------------------------------------------------------------------- */
#define RE_PTHREAD_ONCE_RUNNING 0x5A5A5A5A
#define RE_PTHREAD_ONCE_DONE    0x5A5A5A5B

int re_pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    int state;

    if (once_control == NULL || init_routine == NULL) {
        return EINVAL;
    }

    memcpy(&state, once_control, sizeof(state));
    for (;;) {
        int prev = state;

        if (state == RE_PTHREAD_ONCE_DONE) {
            return 0;
        }
        if (state != RE_PTHREAD_ONCE_RUNNING) {
            if (__atomic_compare_exchange_n((int *)once_control, &prev,
                                            RE_PTHREAD_ONCE_RUNNING, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                init_routine();
                __atomic_store_n((int *)once_control, RE_PTHREAD_ONCE_DONE,
                                 __ATOMIC_RELEASE);
                return 0;
            }
            state = prev;
            continue;
        }
        core_system_sleep(1);
        state = __atomic_load_n((int *)once_control, __ATOMIC_ACQUIRE);
    }
}
