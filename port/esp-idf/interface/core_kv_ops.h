/**
 * @file core_kv_ops.h
 * @brief Contract header for the CORE key-value ops card: CORE_KV_OPS_T and the
 *        core_kv_cfg_t initialization configuration.
 *
 * A types-only contract. core's core_kv.h includes this header and dispatches
 * core_kv_init / core_kv_set / core_kv_get / core_kv_del through the card, which
 * the port assembles as core_kv_ops_platform. On a successful get the returned
 * buffer is heap memory released with core_free. core_kv_cfg_t carries a 16-char
 * seed and key; an NVS-backed port ignores them because NVS applies no
 * application-layer encryption, while a flash filesystem port can derive its
 * AES-128 IV and key from them.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __CORE_KV_OPS_H__
#define __CORE_KV_OPS_H__

#include "tuya_cloud_types.h"
#include "tuya_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CORE_LV_KEY_LEN 16

/**
 * @brief KV initialization configuration: seed/key are 16 characters each; the
 *        port implementation decides their use
 *        (the Linux port derives the AES-128 IV and key through SHA256;
 *        NVS-type platforms may ignore them)
 */
typedef struct {
    char seed[CORE_LV_KEY_LEN + 1];
    char key[CORE_LV_KEY_LEN + 1];
} core_kv_cfg_t;

/**
 * @brief core_kv platform ops: members map one-to-one to the port's kv
 *        implementation
 */
typedef struct {
    /* Mount the medium / open the chip's KV; see core_kv_init in core_kv.h */
    OPERATE_RET (*init)(core_kv_cfg_t *kv_cfg);
    /* Write a key-value pair; see core_kv_set in core_kv.h */
    OPERATE_RET (*set)(const char *key, const uint8_t *value, size_t length);
    /* Read a key-value pair: on success *value is heap memory (released by the caller with core_free); see core_kv_get in core_kv.h */
    OPERATE_RET (*get)(const char *key, uint8_t **value, size_t *length);
    /* Delete a key; see core_kv_del in core_kv.h */
    OPERATE_RET (*del)(const char *key);
} CORE_KV_OPS_T;

#ifdef __cplusplus
}
#endif

#endif /* __CORE_KV_OPS_H__ */
