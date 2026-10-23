/**
 * @file pj_sync_condition.c
 * @brief Condition-style sync via CORE (replaces pthread_cond)
 */
#include "pj_sync_condition.h"
#include "tuya_cloud_types.h"
#include <string.h>

/**
 * @brief Initialize sync condition
 */
int sync_cond_init(sync_cond_t *pSyncCond)
{
    if (pSyncCond == NULL) {
        return -1;
    }
    memset(pSyncCond, 0, sizeof(*pSyncCond));
    if (core_mutex_create_init(&pSyncCond->mutex) != OPRT_OK) {
        return -1;
    }
    if (core_semaphore_create_init(&pSyncCond->sem, 0, 64) != OPRT_OK) {
        core_mutex_release(pSyncCond->mutex);
        pSyncCond->mutex = NULL;
        return -1;
    }
    return 0;
}

/**
 * @brief Signal waiters that condition is met
 */
void sync_cond_notify(sync_cond_t *pSyncCond)
{
    if (pSyncCond == NULL) {
        return;
    }
    core_mutex_lock(pSyncCond->mutex);
    pSyncCond->condition_met = 1;
    core_semaphore_post(pSyncCond->sem);
    core_mutex_unlock(pSyncCond->mutex);
}

/**
 * @brief Wait until condition_met (auto-reset)
 */
void sync_cond_wait(sync_cond_t *pSyncCond)
{
    if (pSyncCond == NULL) {
        return;
    }
    core_mutex_lock(pSyncCond->mutex);
    while (pSyncCond->condition_met == 0) {
        pSyncCond->waiters++;
        core_mutex_unlock(pSyncCond->mutex);
        core_semaphore_wait(pSyncCond->sem, SEM_WAIT_FOREVER);
        core_mutex_lock(pSyncCond->mutex);
        if (pSyncCond->waiters > 0) {
            pSyncCond->waiters--;
        }
    }
    pSyncCond->condition_met = 0;
    core_mutex_unlock(pSyncCond->mutex);
}

/**
 * @brief Destroy sync condition
 */
void sync_cond_clean(sync_cond_t *pSyncCond)
{
    if (pSyncCond == NULL) {
        return;
    }
    if (pSyncCond->sem) {
        core_semaphore_release(pSyncCond->sem);
        pSyncCond->sem = NULL;
    }
    if (pSyncCond->mutex) {
        core_mutex_release(pSyncCond->mutex);
        pSyncCond->mutex = NULL;
    }
}
