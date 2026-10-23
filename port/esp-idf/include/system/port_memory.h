/**
 * @file port_memory.h
 * @brief Port memory allocator (port-internal interface): the implementation
 *        behind the card's .malloc / .free members.
 *
 * Used across files inside the port (buffer allocation in port_kv.c). core
 * never calls this layer directly: core-layer malloc / free dispatch through
 * the card, and a buffer allocated here may be released with core_free because
 * both sides end in this file. Allocation and release must be paired through
 * the same allocator.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __PORT_MEMORY_H__
#define __PORT_MEMORY_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void *port_system_malloc(SIZE_T size);
void  port_system_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* __PORT_MEMORY_H__ */
