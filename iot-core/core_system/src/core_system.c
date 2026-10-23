/**
 * @file core_system.c
 * @brief CORE memory interface: allocation and deallocation through the
 * platform ops card.
 *
 * core_malloc(), core_free() and core_calloc() resolve the platform ops card
 * through core_api_get_ops() and dispatch to its .malloc/.free members; a zero
 * size yields NULL, and a failed allocation is logged with the return address
 * and the requested size. core_calloc() builds zeroed memory from core_malloc()
 * plus memset with an overflow check on the element count.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "core_system.h"
#include "core_log.h"
#include "core_memory.h"
#include "core_api_ops.h"
#include "core_api.h"

/**
 * @brief Allocates a block of memory through the platform ops card.
 *
 * A zero size returns NULL without touching the card. A failed allocation is
 * logged with the caller return address and the requested size.
 *
 * @param size The size of the memory block to allocate.
 * @return A pointer to the allocated memory block, or NULL on failure.
 */
void *core_malloc(size_t size)
{
    if (0 == size) {
        return NULL;
    }

    void *ptr = NULL;
    CORE_API_OPS_T *ops = core_api_get_ops();
    if (NULL != ops && ops->malloc) {
        ptr = ops->malloc(size);
    }
    if (NULL == ptr) {
        PR_ERR("0x%x malloc failed:0x%x", __builtin_return_address(0), size);
    }

    return ptr;
}

/**
 * @brief Frees a block of memory through the platform ops card.
 *
 * NULL is ignored. Memory must come from core_malloc()/core_calloc() so that it
 * is released by the same allocator.
 *
 * @param ptr Pointer to the memory block to be freed.
 */
void core_free(void *ptr)
{
    if (NULL == ptr) {
        return;
    }

    CORE_API_OPS_T *ops = core_api_get_ops();
    if (NULL != ops && ops->free) {
        ops->free(ptr);
    }
}

/**
 * Allocates memory for an array of elements, initialized to zero.
 *
 * This function allocates memory for an array of elements, where each element
 * is of size 'size'. The memory is initialized to zero.
 *
 * @param nitems The number of elements to allocate memory for.
 * @param size The size of each element in bytes.
 * @return A pointer to the allocated memory, or NULL if the allocation fails
 *         (including a zero nitems/size or a multiplication overflow).
 */
void *core_calloc(size_t nitems, size_t size)
{
    /* Overflow guard, matching libc/mbedtls calloc semantics (NULL on
     * overflow). A zero nitems/size makes total 0, and core_malloc(0) already
     * returns NULL, so the zero-size case needs no separate branch. */
    if (size != 0 && nitems > SIZE_MAX / size) {
        return NULL;
    }

    size_t total = nitems * size;
    void *ptr = core_malloc(total);
    if (NULL != ptr) {
        memset(ptr, 0, total);
    }

    return ptr;
}

/**
 * @brief Sleeps for the specified amount of time in milliseconds.
 *
 * This function is used to put the system to sleep for the specified amount of
 * time.
 *
 * @param time_ms The time to sleep in milliseconds.
 */
void core_system_sleep(uint32_t time_ms)
{
    CORE_API_OPS_T *ops = core_api_get_ops();
    if (NULL != ops && ops->sleep) {
        ops->sleep(time_ms);
    }
}

/**
 * @brief Resets the CORE system.
 *
 * This function calls the `port_system_reset()` function to reset the CORE
 * system.
 *
 * @note This function should be called when a system reset is required.
 */
void core_system_reset(void)
{
    CORE_API_OPS_T *ops = core_api_get_ops();
    if (NULL != ops && ops->reset) {
        ops->reset();
    }
}

SYS_TIME_T g_sys_time_offset = 0; // used for debug
/**
 * @brief Get the current system time in milliseconds.
 *
 * This function returns the current system time in milliseconds by calling the
 * `port_system_get_millisecond()` function and adding the system time offset.
 *
 * @return The current system time in milliseconds.
 */
SYS_TIME_T core_system_get_millisecond(void)
{
    CORE_API_OPS_T *ops = core_api_get_ops();
    if (NULL != ops && ops->get_millisecond) {
        return ops->get_millisecond() + g_sys_time_offset;
    }

    return g_sys_time_offset;
}

/**
 * @brief Get a random number within the specified range.
 *
 * This function returns a random number within the specified range.
 *
 * @param range The range within which the random number should be generated.
 * @return The generated random number.
 */
int core_system_get_random(uint32_t range)
{
    CORE_API_OPS_T *ops = core_api_get_ops();
    if (NULL != ops && ops->get_random) {
        return ops->get_random(range);
    }

    return 0;
}

