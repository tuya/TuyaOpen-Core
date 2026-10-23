/**
 * @file core_kv.h
 * @brief core_kv platform dispatch: forwards the four interfaces to the port's
 *        KV implementation (core_kv_ops_platform).
 *
 * iot-core holds no KV logic -- storage medium, record format, encryption and
 * serialization all live in the port (see the core_kv_ops.h contract).
 * Dispatch goes through CORE_KV_EXEC_OP, which checks the card and member
 * before calling them and returns the given default otherwise.
 *
 * core_kv_get() clears *value to NULL and *length to 0 before dispatching, so
 * callers observe that contract even when the operation is unavailable.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#ifndef __CORE_KV_H__
#define __CORE_KV_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

/* Contract layer (supplied by the selected platform's port): core_kv_cfg_t and
 * CORE_KV_OPS_T are defined in port/interface/core_kv_ops.h -- core consumes the
 * contract, never defines it (layering: port does not depend on core; core
 * takes the contract header from the host-injected include path). */
#include "core_kv_ops.h"

/**
 * @brief Get the platform ops of core_kv
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
CORE_KV_OPS_T *core_kv_get_ops(void);

/**
 * @brief Initialize KV storage (mount the medium / open the chip KV)
 *
 * @note Must run before any other interface, from a single thread, exactly
 *       once; after a failed init the other interfaces are unusable (see the
 *       init contract in the file header).
 *
 * @param kv_cfg configuration (must not be NULL)
 * @return OPRT_OK on success; negative values are error codes
 */
int core_kv_init(core_kv_cfg_t *kv_cfg);

/**
 * @brief Write a key-value pair (overwrites an existing key)
 *
 * @param key key (C string, non-NULL)
 * @param value value (arbitrary binary, non-NULL)
 * @param length value length in bytes, **must be > 0**: 0 is rejected as an
 *               invalid parameter with OPRT_INVALID_PARM (an empty value is
 *               meaningless -- core_kv_get always returns a value containing
 *               the terminating '\0', so the length is at least 1)
 * @return OPRT_OK on success; OPRT_INVALID_PARM for invalid parameters; other
 *         negative values are error codes
 */
int core_kv_set(const char *key, const uint8_t *value, size_t length);

/**
 * @brief Read a key-value pair
 *
 * On success *value is heap memory (including the terminating '\0', released
 * by the caller with core_free) and *length is the data length (excluding
 * '\0'); on failure *value is NULL and *length is 0.
 *
 * @param key key (C string, non-NULL)
 * @param value output: heap pointer to the value
 * @param length output: value length
 * @return OPRT_OK on success, OPRT_NOT_FOUND if the key does not exist, other
 *         negative values are error codes
 */
int core_kv_get(const char *key, uint8_t **value, size_t *length);

/**
 * @brief Delete a key (a missing key is treated as success)
 *
 * @param key key (C string, non-NULL)
 * @return OPRT_OK on success; negative values are error codes
 */
int core_kv_del(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* __CORE_KV_H__ */
