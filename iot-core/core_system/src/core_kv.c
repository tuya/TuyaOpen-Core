/**
 * @file core_kv.c
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
#include "core_kv.h"
#include "core_kv_ops.h"
#include "core_log.h"

/* Platform ops card (defined in the port's port_kv.c), dispatched by core
 * through the getter */
extern CORE_KV_OPS_T core_kv_ops_platform;

CORE_KV_OPS_T *core_kv_get_ops(void)
{
    return &core_kv_ops_platform;
}

#define CORE_KV_EXEC_OP(op_name, default_ret, ...)                                                                      \
    do {                                                                                                               \
        CORE_KV_OPS_T *ops = core_kv_get_ops();                                                                          \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("CORE KV operation %s not available", #op_name);                                                         \
        return default_ret;                                                                                            \
    } while (0)

int core_kv_init(core_kv_cfg_t *kv_cfg)
{
    CORE_KV_EXEC_OP(init, OPRT_COM_ERROR, kv_cfg);
}

int core_kv_set(const char *key, const uint8_t *value, size_t length)
{
    CORE_KV_EXEC_OP(set, OPRT_COM_ERROR, key, value, length);
}

int core_kv_get(const char *key, uint8_t **value, size_t *length)
{
    /* Contract: on failure *value is NULL and *length is 0 (also when the ops
     * card is unavailable) */
    if (NULL != value) {
        *value = NULL;
    }
    if (NULL != length) {
        *length = 0;
    }
    CORE_KV_EXEC_OP(get, OPRT_COM_ERROR, key, value, length);
}

int core_kv_del(const char *key)
{
    CORE_KV_EXEC_OP(del, OPRT_COM_ERROR, key);
}
