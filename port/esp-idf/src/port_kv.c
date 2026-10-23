/**
 * @file port_kv.c
 * @brief core_kv platform ops card: esp32 uses NVS (nvs_flash) for key-value
 *        storage
 *
 * init/set/get/del map onto the NVS namespace "tuya_kv". The storage medium
 * and record format are entirely port-internal details (see core_kv_ops.h),
 * with zero changes in core.
 *
 * key mapping: NVS keys are limited to 15 characters, while core's keys have no
 * length limit (for example "uuidxxxxxxxxxxxxxxxx.devid" is 26 characters and
 * "device_timer_tasks" is 18 characters, and the length of cloud-delivered
 * schemaIds is unbounded). Longer keys are folded into "h" + 14 hex characters
 * (the low 56 bits of an FNV-1a 64 hash) -- this is only a namespace mapping
 * for NVS keys (not for security), and at the per-device key volume (<100) the
 * collision probability is negligible. Keys of length <=15 are used as-is
 * (preserving readability for debugging).
 *
 * Convention: nvs_flash_init() is the application's responsibility (main.c);
 * this card only opens the namespace; set/get/del are rejected before init
 * (the NVS handle is not touched). NVS itself is thread-safe, so no extra port
 * layer mutex is added.
 *
 * Memory and logging go entirely through this layer (port_system_malloc /
 * PORT_LOG_ERR, see include/system/port_memory.h and include/common/port_log.h)
 * and **do not call core** (layering constraint: the port depends only
 * downwards, and core calls the port downwards only through cards).
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "core_kv_ops.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <string.h>

#include "port_log.h"
#include "port_memory.h"

#define KV_NVS_NAMESPACE "tuya_kv"
/* NVS keys are limited to 15 characters (excluding the trailing '\0', see the
 * nvs_flash documentation) */
#define KV_NVS_KEY_MAX 15

static nvs_handle_t s_kv_handle = 0;
static BOOL_T s_kv_opened = FALSE;

/* FNV-1a 64-bit (offset basis / prime are the algorithm's standard constants) */
#define KV_FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define KV_FNV_PRIME        0x100000001b3ULL

/**
 * @brief Map a core key to a valid NVS key (<=15 characters)
 *
 * @param[in] key: key passed in by core (non-NULL)
 * @param[out] out: output buffer (at least KV_NVS_KEY_MAX + 1 bytes)
 */
static void __kv_map_key(const char *key, char out[KV_NVS_KEY_MAX + 1])
{
    if (strlen(key) <= KV_NVS_KEY_MAX) {
        snprintf(out, KV_NVS_KEY_MAX + 1, "%s", key);
        return;
    }

    uint64_t hash = KV_FNV_OFFSET_BASIS;
    const uint8_t *p = (const uint8_t *)key;

    while (*p) {
        hash ^= (uint64_t)(*p++);
        hash *= KV_FNV_PRIME;
    }

    /* The 'h' prefix distinguishes mapped keys from original keys; take the low
     * 56 bits -> 14 hex characters */
    snprintf(out, KV_NVS_KEY_MAX + 1, "h%014llx", (unsigned long long)(hash & 0x00FFFFFFFFFFFFFFULL));
}

/**
 * @brief Initialize KV storage (open the NVS namespace)
 *
 * @note seed/key are accepted through the config but ignored: the NVS backend
 *       does no application-layer encryption. Repeated init is idempotent.
 *
 * @param kv_cfg configuration (must not be NULL)
 * @return OPRT_OK on success, a negative value on error
 */
static OPERATE_RET __kv_init(core_kv_cfg_t *kv_cfg)
{
    if (NULL == kv_cfg) {
        return OPRT_INVALID_PARM;
    }

    if (TRUE == s_kv_opened) {
        return OPRT_OK;
    }

    esp_err_t err = nvs_open(KV_NVS_NAMESPACE, NVS_READWRITE, &s_kv_handle);
    if (ESP_OK != err) {
        PORT_LOG_ERR("kv nvs_open failed: %d (nvs_flash_init called first?)", err);
        return OPRT_COM_ERROR;
    }

    s_kv_opened = TRUE;
    return OPRT_OK;
}

static OPERATE_RET __kv_set(const char *key, const uint8_t *value, size_t length)
{
    char nvs_key[KV_NVS_KEY_MAX + 1];

    if (NULL == key || NULL == value || 0 == length) {
        return OPRT_INVALID_PARM;
    }
    if (FALSE == s_kv_opened) {
        PORT_LOG_ERR("kv not init, set %s rejected", key);
        return OPRT_COM_ERROR;
    }

    __kv_map_key(key, nvs_key);

    esp_err_t err = nvs_set_blob(s_kv_handle, nvs_key, value, length);
    if (ESP_OK != err) {
        PORT_LOG_ERR("kv set %s failed: %d", key, err);
        return OPRT_COM_ERROR;
    }

    err = nvs_commit(s_kv_handle);
    if (ESP_OK != err) {
        PORT_LOG_ERR("kv set %s commit failed: %d", key, err);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

static OPERATE_RET __kv_get(const char *key, uint8_t **value, size_t *length)
{
    char nvs_key[KV_NVS_KEY_MAX + 1];
    size_t len = 0;

    /* Contract (core_kv.h): on failure *value is set to NULL and *length to 0.
     * Zero them before parameter validation so the promise also holds when the
     * port is called directly (bypassing core_kv.c dispatch) */
    if (NULL != value) {
        *value = NULL;
    }
    if (NULL != length) {
        *length = 0;
    }

    if (NULL == key || NULL == value || NULL == length) {
        return OPRT_INVALID_PARM;
    }
    if (FALSE == s_kv_opened) {
        PORT_LOG_ERR("kv not init, get %s rejected", key);
        return OPRT_COM_ERROR;
    }

    __kv_map_key(key, nvs_key);

    esp_err_t err = nvs_get_blob(s_kv_handle, nvs_key, NULL, &len);
    if (ESP_ERR_NVS_NOT_FOUND == err) {
        return OPRT_NOT_FOUND;
    }
    if (ESP_OK != err) {
        PORT_LOG_ERR("kv get %s failed: %d", key, err);
        return OPRT_COM_ERROR;
    }

    /* Contract: the returned buffer includes a trailing '\0' (its length does
     * not include the '\0'), and the caller releases it through card dispatch
     * (port_system_malloc is the implementation body of the card's .free, so
     * allocation and release are paired same-source) */
    uint8_t *buf = (uint8_t *)port_system_malloc(len + 1);
    if (NULL == buf) {
        return OPRT_MALLOC_FAILED;
    }

    err = nvs_get_blob(s_kv_handle, nvs_key, buf, &len);
    if (ESP_OK != err) {
        PORT_LOG_ERR("kv get %s read failed: %d", key, err);
        port_system_free(buf);
        return OPRT_COM_ERROR;
    }
    buf[len] = '\0';

    *value = buf;
    *length = len;
    return OPRT_OK;
}

static OPERATE_RET __kv_del(const char *key)
{
    char nvs_key[KV_NVS_KEY_MAX + 1];

    if (NULL == key) {
        return OPRT_INVALID_PARM;
    }
    if (FALSE == s_kv_opened) {
        PORT_LOG_ERR("kv not init, del %s rejected", key);
        return OPRT_COM_ERROR;
    }

    __kv_map_key(key, nvs_key);

    esp_err_t err = nvs_erase_key(s_kv_handle, nvs_key);
    if (ESP_ERR_NVS_NOT_FOUND == err) {
        return OPRT_OK; /* contract: a missing key is treated as success */
    }
    if (ESP_OK != err) {
        PORT_LOG_ERR("kv del %s failed: %d", key, err);
        return OPRT_COM_ERROR;
    }

    err = nvs_commit(s_kv_handle);
    if (ESP_OK != err) {
        PORT_LOG_ERR("kv del %s commit failed: %d", key, err);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Platform ops card: assembled by this file; core's core_kv.c references it via
 * extern and completes dispatch.
 * --------------------------------------------------------------------------- */
CORE_KV_OPS_T core_kv_ops_platform = {
    .init = __kv_init,
    .set = __kv_set,
    .get = __kv_get,
    .del = __kv_del,
};
