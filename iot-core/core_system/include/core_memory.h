/**
 * @file core_memory.h
 * @brief CORE heap interface: core_malloc / core_free / core_calloc, the
 *        Malloc / Calloc / Free aliases.
 *
 * core_malloc and core_free dispatch to the CORE_API_OPS_T .malloc / .free
 * members assembled by the port. core_calloc allocates through core_malloc,
 * zeroes the block and returns NULL on a zero size or a nitems * size
 * overflow.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __CORE_MEMORY_H__
#define __CORE_MEMORY_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ********************* constant ( macro and enum ) *********************
 **********************************************************************/
#define Malloc(req_size)             core_malloc(req_size)
#define Calloc(req_count, req_size)  core_calloc(req_count, req_size)
#define Free(ptr)                    core_free(ptr)

/***********************************************************************
 ********************* struct ******************************************
 **********************************************************************/

/***********************************************************************
 ********************* variable ****************************************
 **********************************************************************/

/***********************************************************************
 ********************* function ****************************************
 **********************************************************************/

/**
 * @brief This API is used to alloc memory of system.
 *
 * @param[in] size: memory size
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
void *core_malloc(size_t size);

/**
 * @brief This API is used to free memory of system.
 *
 * @param[in] ptr: memory point
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
void core_free(void *ptr);

/**
 * @brief Allocate and clear the memory
 *
 * @param[in]       nitems      the numbers of memory block
 * @param[in]       size        the size of the memory block
 *
 * @return the zeroed memory address, or NULL if the allocation fails,
 *         nitems/size is 0, or nitems * size overflows
 */
void *core_calloc(size_t nitems, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __CORE_MEMORY_H__ */
