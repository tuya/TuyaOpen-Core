/**
 * @file bc_msg_queue.c
 * @brief Message queue — CORE mutex/semaphore (no pthread)
 * @version 1.1
 * @date 2026-08-06
 * @copyright Copyright (c) Tuya Inc.
 */
#include "bc_msg_queue.h"
#include "core_mutex.h"
#include "core_semaphore.h"
#include "core_memory.h"
#include "tuya_cloud_types.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct bc_msg_node {
    struct bc_msg_node *next;
    struct bc_msg_node *prev;
    int type;
    void *data;
    int len;
} bc_msg_node_t;

struct bc_msg_queue {
    bc_msg_node_t *next;
    bc_msg_node_t *prev;
    MUTEX_HANDLE lock;
    SEM_HANDLE sem;
    int waiters;
    int used_size;
    int close_flag;
};

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Create an empty message queue
 */
bc_msg_queue_t *bc_msg_queue_create(void)
{
    bc_msg_queue_t *q;

    q = (bc_msg_queue_t *)core_calloc(1, sizeof(*q));
    if (q == NULL) {
        return NULL;
    }
    q->next = (bc_msg_node_t *)q;
    q->prev = (bc_msg_node_t *)q;
    if (core_mutex_create_init(&q->lock) != OPRT_OK) {
        core_free(q);
        return NULL;
    }
    if (core_semaphore_create_init(&q->sem, 0, 64) != OPRT_OK) {
        core_mutex_release(q->lock);
        core_free(q);
        return NULL;
    }
    return q;
}

/**
 * @brief Free one node and its payload
 */
static void __bc_msg_node_free(bc_msg_node_t *n)
{
    if (n == NULL) {
        return;
    }
    if (n->data != NULL) {
        core_free(n->data);
        n->data = NULL;
    }
    core_free(n);
}

/**
 * @brief Wake waiters (broadcast-ish via posts)
 */
static void __bc_msg_queue_wake_all(bc_msg_queue_t *q)
{
    int i;
    int n = q->waiters;
    for (i = 0; i < n; i++) {
        core_semaphore_post(q->sem);
    }
}

/**
 * @brief Mark queue closed and wake blocked pop
 */
void bc_msg_queue_close(bc_msg_queue_t *q)
{
    if (q == NULL) {
        return;
    }
    core_mutex_lock(q->lock);
    q->close_flag = 1;
    __bc_msg_queue_wake_all(q);
    core_mutex_unlock(q->lock);
}

/**
 * @brief Destroy queue and free remaining nodes
 */
void bc_msg_queue_destroy(bc_msg_queue_t *q)
{
    bc_msg_node_t *n;
    bc_msg_node_t *next;

    if (q == NULL) {
        return;
    }
    bc_msg_queue_close(q);
    core_mutex_lock(q->lock);
    n = q->next;
    while (n != (bc_msg_node_t *)q) {
        next = n->next;
        q->used_size -= n->len;
        __bc_msg_node_free(n);
        n = next;
    }
    q->next = (bc_msg_node_t *)q;
    q->prev = (bc_msg_node_t *)q;
    q->used_size = 0;
    core_mutex_unlock(q->lock);
    if (q->sem) {
        core_semaphore_release(q->sem);
        q->sem = NULL;
    }
    if (q->lock) {
        core_mutex_release(q->lock);
        q->lock = NULL;
    }
    core_free(q);
}

/**
 * @brief Get queued payload bytes
 */
int bc_msg_queue_get_length(bc_msg_queue_t *q)
{
    int used;

    if (q == NULL) {
        return 0;
    }
    core_mutex_lock(q->lock);
    used = q->used_size;
    core_mutex_unlock(q->lock);
    return used;
}

/**
 * @brief Push a message copy to queue tail
 */
int bc_msg_queue_push_back(bc_msg_queue_t *q, int type, const void *data, int len)
{
    bc_msg_node_t *n;

    if ((q == NULL) || (data == NULL) || (len <= 0) || (len > BC_MSG_SIZE_MAX)) {
        return -1;
    }
    n = (bc_msg_node_t *)core_calloc(1, sizeof(*n));
    if (n == NULL) {
        return -1;
    }
    n->data = core_malloc((size_t)len);
    if (n->data == NULL) {
        core_free(n);
        return -1;
    }
    memcpy(n->data, data, (size_t)len);
    n->type = type;
    n->len = len;

    core_mutex_lock(q->lock);
    if (q->close_flag != 0) {
        core_mutex_unlock(q->lock);
        __bc_msg_node_free(n);
        return -1;
    }
    n->next = (bc_msg_node_t *)q;
    n->prev = q->prev;
    q->prev->next = n;
    q->prev = n;
    q->used_size += len;
    core_semaphore_post(q->sem);
    core_mutex_unlock(q->lock);
    return 0;
}

/**
 * @brief Pop front message (blocking until data or closed)
 */
int bc_msg_queue_pop_front(bc_msg_queue_t *q, int *type, void *buf, int *len)
{
    bc_msg_node_t *n;
    int copy_len;
    int cap;

    if ((q == NULL) || (buf == NULL) || (len == NULL) || (*len <= 0)) {
        return -1;
    }
    cap = *len;
    core_mutex_lock(q->lock);
    for (;;) {
        if (q->close_flag != 0 && q->next == (bc_msg_node_t *)q) {
            core_mutex_unlock(q->lock);
            return -1;
        }
        if (q->next != (bc_msg_node_t *)q) {
            break;
        }
        q->waiters++;
        core_mutex_unlock(q->lock);
        core_semaphore_wait(q->sem, SEM_WAIT_FOREVER);
        core_mutex_lock(q->lock);
        if (q->waiters > 0) {
            q->waiters--;
        }
    }
    n = q->next;
    if (type != NULL) {
        *type = n->type;
    }
    copy_len = n->len;
    if (copy_len > cap) {
        memcpy(buf, n->data, (size_t)cap);
        memmove(n->data, (char *)n->data + cap, (size_t)(copy_len - cap));
        n->len = copy_len - cap;
        q->used_size -= cap;
        *len = cap;
        core_semaphore_post(q->sem);
        core_mutex_unlock(q->lock);
        return cap;
    }
    memcpy(buf, n->data, (size_t)copy_len);
    n->next->prev = n->prev;
    n->prev->next = n->next;
    q->used_size -= copy_len;
    *len = copy_len;
    core_mutex_unlock(q->lock);
    __bc_msg_node_free(n);
    return copy_len;
}
