/**
 * @file tuya_endpoint.c
 * @brief Cloud endpoint management: region/registration-key persistence and
 * endpoint lookup through iot-dns.
 *
 * A single static endpoint_management_t holds the region, the registration
 * key (environment, "pro" when empty) and the resolved tuya_endpoint_t
 * (atop host/port/path, mqtt host/port and the CA certificate).
 *
 * Region and registration key are stored in KV under "region" and
 * "regist_key" as raw string values and copied back by their actual length,
 * clamped to the destination capacity and explicitly terminated.
 *
 * The endpoint fields are persisted one KV key per field: strings under
 * atop.host, atop.path, mqtt.host (including the trailing '\0') and ports
 * under atop.port, mqtt.port (2-byte binary), plus the certificate under
 * endpoint.cert. Reads reject an overlong value instead of truncating it.
 * tuya_endpoint_remove() deletes the registration keys, the certificate, the
 * five domain field keys and the endpoint.domain key.
 *
 * tuya_endpoint_update() frees the certificate it already holds and resolves
 * the endpoint from the region and registration key through
 * iotdns_cloud_endpoint_get(); tuya_endpoint_update_auto_region() does the
 * same with the region left to the iot-dns service. tuya_endpoint_get()
 * returns the process-wide endpoint for readers.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_endpoint.h"
#include "core_api.h"

#include "core_kv.h"
#include "core_memory.h"

extern int iotdns_cloud_endpoint_get(const char *region, const char *env, tuya_endpoint_t *endpoint);

typedef struct {
    char region[MAX_LENGTH_REGION + 1];
    char regist_key[MAX_LENGTH_REGIST + 1];
    tuya_endpoint_t endpoint;
} endpoint_management_t;

static endpoint_management_t endpoint_mgr;

static int tuya_region_regist_key_write(const char *region, const char *regist_key)
{
    if (NULL == region || NULL == regist_key) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    /* Write kv storage */
    int ret = 0;
    ret = core_kv_set("region", (const uint8_t *)region, strlen(region));
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_set region, error:0x%02x", ret);
        return OPRT_KVS_WR_FAIL;
    }

    ret = core_kv_set("regist_key", (const uint8_t *)regist_key, strlen(regist_key));
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_set regist_key, error:0x%02x", ret);
        return OPRT_KVS_WR_FAIL;
    }

    return OPRT_OK;
}

static int tuya_region_regist_key_read(char *region, char *regist_key)
{
    if (NULL == region || NULL == regist_key) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    /* Read the region&env from kv storage */
    int ret = 0;
    size_t len = 0;
    uint8_t *value = NULL;
    ret = core_kv_get("region", &value, &len);
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_get region fail:0x%02x", ret);
        return OPRT_KVS_RD_FAIL;
    }
    size_t copy = (len < MAX_LENGTH_REGION) ? len : MAX_LENGTH_REGION;
    memcpy(region, value, copy);
    region[copy] = 0;
    core_free(value);

    ret = core_kv_get("regist_key", &value, &len);
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_get regist_key fail:0x%02x", ret);
        return OPRT_KVS_RD_FAIL;
    }
    /* regist_key comes from the cloud env field, its length is not locally
     * controlled */
    copy = (len < MAX_LENGTH_REGIST) ? len : MAX_LENGTH_REGIST;
    memcpy(regist_key, value, copy);
    regist_key[copy] = 0;
    core_free(value);

    return OPRT_OK;
}

/**
 * @brief Retrieves the certificate for the specified endpoint.
 *
 * This function retrieves the certificate for the specified endpoint from the
 * key-value store.
 *
 * @param[in] endpoint Pointer to the tuya_endpoint_t structure representing the
 * endpoint.
 * @return Operation result. Returns OPRT_OK on success, or an error code on
 * failure.
 */
int tuya_endpoint_cert_get(tuya_endpoint_t *endpoint)
{
    if (NULL == endpoint) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    int ret = core_kv_get("endpoint.cert", &endpoint->cert, &endpoint->cert_len);
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_get endpoint.cert fail:0x%02x", ret);
    }

    return ret;
}

/**
 * @brief Sets the certificate for the Tuya endpoint.
 *
 * This function sets the certificate for the Tuya endpoint by storing it in the
 * key-value store.
 *
 * @param endpoint Pointer to the Tuya endpoint structure.
 * @return Returns OPRT_OK on success, or an error code on failure.
 */
int tuya_endpoint_cert_set(tuya_endpoint_t *endpoint)
{
    if (NULL == endpoint) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    int ret = core_kv_set("endpoint.cert", endpoint->cert, endpoint->cert_len);
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_set endpoint.cert fail:0x%02x", ret);
    }

    return ret;
}

/* Read a string field key by key: the stored value includes the trailing
 * '\0'; an overlong value is an error rather than being truncated */
static int tuya_endpoint_kv_str_read(const char *key, char *buf, size_t buf_size)
{
    uint8_t *value = NULL;
    size_t len = 0;
    int ret = core_kv_get(key, &value, &len);
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_get %s error:%d", key, ret);
        return ret;
    }

    if (0 == buf_size || len > buf_size) {
        PR_ERR("core_kv_get %s value too long: %u > %u", key, (unsigned int)len, (unsigned int)buf_size);
        core_free(value);
        return OPRT_COM_ERROR;
    }

    if (0 == len) {
        buf[0] = 0; /* fallback terminator (when the port returns an empty value) */
    } else {
        memcpy(buf, value, len);
        buf[len - 1] = 0; /* fallback terminator; the last byte on the write side is already '\0', so this is idempotent */
    }
    core_free(value);

    return OPRT_OK;
}

/* Read a uint16 field key by key: the value is stored as 2-byte binary */
static int tuya_endpoint_kv_u16_read(const char *key, uint16_t *out)
{
    uint8_t *value = NULL;
    size_t len = 0;
    int ret = core_kv_get(key, &value, &len);
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_get %s error:%d", key, ret);
        return ret;
    }
    if (len != sizeof(*out)) {
        PR_ERR("core_kv_get %s length error:%u", key, (unsigned int)len);
        core_free(value);
        return OPRT_COM_ERROR;
    }

    memcpy(out, value, sizeof(*out));
    core_free(value);

    return OPRT_OK;
}

/**
 * @brief Retrieves the domain information for the endpoint.
 *
 * This function retrieves the domain information for the endpoint by reading
 * the key-value pairs from the key-value store, one key per field.
 *
 * @param[in] endpoint Pointer to the tuya_endpoint_t structure to store the
 * retrieved domain information.
 * @return Returns OPRT_OK on success, or an error code on failure.
 */
int tuya_endpoint_domain_get(tuya_endpoint_t *endpoint)
{
    if (NULL == endpoint) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    /* Read key by key (key name = field name): read all 5 fields and return
     * the first error code */
    int ret = tuya_endpoint_kv_str_read("atop.host", endpoint->atop.host, sizeof(endpoint->atop.host));
    int rt = tuya_endpoint_kv_u16_read("atop.port", &endpoint->atop.port);
    if (OPRT_OK == ret) {
        ret = rt;
    }
    rt = tuya_endpoint_kv_str_read("atop.path", endpoint->atop.path, sizeof(endpoint->atop.path));
    if (OPRT_OK == ret) {
        ret = rt;
    }
    rt = tuya_endpoint_kv_str_read("mqtt.host", endpoint->mqtt.host, sizeof(endpoint->mqtt.host));
    if (OPRT_OK == ret) {
        ret = rt;
    }
    rt = tuya_endpoint_kv_u16_read("mqtt.port", &endpoint->mqtt.port);
    if (OPRT_OK == ret) {
        ret = rt;
    }

    if (ret != OPRT_OK) {
        PR_ERR("core_kv_get endpoint domain error:%d", ret);
    }

    return ret;
}

/**
 * @brief Sets the domain for the Tuya endpoint.
 *
 * This function sets the domain for the Tuya endpoint by writing the endpoint
 * information into the key-value store, one key per field.
 *
 * @param endpoint Pointer to the tuya_endpoint_t structure containing the
 * endpoint information.
 * @return Returns OPRT_OK if the domain is set successfully, otherwise returns
 * an error code.
 */
int tuya_endpoint_domain_set(tuya_endpoint_t *endpoint)
{
    if (NULL == endpoint) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    /* Write key by key (key name = field name): strings include the trailing
     * '\0'; ports store the 2-byte fields as binary. Return the first error
     * code. */
    int ret = core_kv_set("atop.host", (const uint8_t *)endpoint->atop.host, strlen(endpoint->atop.host) + 1);
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_set atop.host error:%d", ret);
        return ret;
    }

    ret = core_kv_set("atop.port", (const uint8_t *)&endpoint->atop.port, sizeof(endpoint->atop.port));
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_set atop.port error:%d", ret);
        return ret;
    }

    ret = core_kv_set("atop.path", (const uint8_t *)endpoint->atop.path, strlen(endpoint->atop.path) + 1);
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_set atop.path error:%d", ret);
        return ret;
    }

    ret = core_kv_set("mqtt.host", (const uint8_t *)endpoint->mqtt.host, strlen(endpoint->mqtt.host) + 1);
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_set mqtt.host error:%d", ret);
        return ret;
    }

    ret = core_kv_set("mqtt.port", (const uint8_t *)&endpoint->mqtt.port, sizeof(endpoint->mqtt.port));
    if (ret != OPRT_OK) {
        PR_ERR("core_kv_set mqtt.port error:%d", ret);
        return ret;
    }

    return OPRT_OK;
}

/**
 * @brief Sets the region and registration key for the Tuya endpoint.
 *
 * This function sets the region and registration key for the Tuya endpoint by
 * writing them to the appropriate variables.
 *
 * @param region The region to set for the Tuya endpoint.
 * @param regist_key The registration key to set for the Tuya endpoint.
 * @return Returns OPRT_OK if the region and registration key are set
 * successfully, or OPRT_KVS_WR_FAIL if there was an error writing the region
 * and registration key.
 */
int tuya_endpoint_region_regist_set(const char *region, const char *regist_key)
{
    if (tuya_region_regist_key_write(region, regist_key) != OPRT_OK) {
        PR_ERR("region_regist_key_write error");
        return OPRT_KVS_WR_FAIL;
    }

    strncpy(endpoint_mgr.region, region, sizeof(endpoint_mgr.region) - 1);
    endpoint_mgr.region[sizeof(endpoint_mgr.region) - 1] = '\0';
    strncpy(endpoint_mgr.regist_key, regist_key, sizeof(endpoint_mgr.regist_key) - 1);
    endpoint_mgr.regist_key[sizeof(endpoint_mgr.regist_key) - 1] = '\0';
    return OPRT_OK;
}

/**
 * @brief Removes the endpoint configuration from the device.
 *
 * This function removes the endpoint configuration from the device by deleting
 * the corresponding key-value pairs from the key-value store.
 *
 * @return OPRT_OK if the endpoint configuration is successfully removed,
 * otherwise an error code.
 */
int tuya_endpoint_remove(void)
{
    core_kv_del("region");
    core_kv_del("regist_key");
    core_kv_del("endpoint.cert");
    core_kv_del("atop.host");
    core_kv_del("atop.port");
    core_kv_del("atop.path");
    core_kv_del("mqtt.host");
    core_kv_del("mqtt.port");
    core_kv_del("endpoint.domain");

    return OPRT_OK;
}

/**
 * @brief Initializes the Tuya endpoint.
 *
 * This function reads the storage region and registration key, and sets the
 * default online environment if necessary.
 *
 * @return The result of the initialization process.
 */
int tuya_endpoint_init(void)
{
    int ret;

    /* Read storage region & registration key */
    ret = tuya_region_regist_key_read(endpoint_mgr.region, endpoint_mgr.regist_key);
    PR_INFO("endpoint_mgr.region:%s", endpoint_mgr.region);
    PR_INFO("endpoint_mgr.regist_key:%s", endpoint_mgr.regist_key);

    /* Default online environment */
    if (endpoint_mgr.regist_key[0] == 0) {
        strncpy(endpoint_mgr.regist_key, "pro", sizeof(endpoint_mgr.regist_key) - 1);
        endpoint_mgr.regist_key[sizeof(endpoint_mgr.regist_key) - 1] = '\0';
    }

    return ret;
}

/**
 * @brief Updates the Tuya endpoint.
 *
 * This function updates the Tuya endpoint by retrieving the iot-dns domain data
 * using the specified region and registration key. If the iotdns has already
 * been called, the allocated certificate memory will be released before
 * updating the endpoint.
 *
 * @return The result of the endpoint update operation.
 *         Returns 0 on success, or an error code on failure.
 */
int tuya_endpoint_update(void)
{
    int ret;

    /* If iotdns has already been called,
     * the allocated certificate memory needs to be released. */
    if (endpoint_mgr.endpoint.cert != NULL && endpoint_mgr.endpoint.cert_len > 0) {
        PR_TRACE("Free endpoint already exist cert.");
        core_free(endpoint_mgr.endpoint.cert);
        endpoint_mgr.endpoint.cert = NULL;
        endpoint_mgr.endpoint.cert_len = 0;
    }
    /* Try to get the iot-dns domain data */
    ret = iotdns_cloud_endpoint_get(endpoint_mgr.region, endpoint_mgr.regist_key, &endpoint_mgr.endpoint);

    return ret;
}

/**
 * @brief Updates the auto region for the Tuya endpoint.
 *
 * This function updates the auto region for the Tuya endpoint by retrieving the
 * iot-dns domain data. If the iotdns has already been called, the allocated
 * certificate memory is released.
 *
 * @return The result of the operation. Returns 0 on success, or an error code
 * on failure.
 */
int tuya_endpoint_update_auto_region(void)
{
    int ret;

    /* If iotdns has already been called,
     * the allocated certificate memory needs to be released. */
    if (endpoint_mgr.endpoint.cert != NULL && endpoint_mgr.endpoint.cert_len > 0) {
        PR_TRACE("Free endpoint already exist cert.");
        core_free(endpoint_mgr.endpoint.cert);
        endpoint_mgr.endpoint.cert = NULL;
        endpoint_mgr.endpoint.cert_len = 0;
    }
    /* Try to get the iot-dns domain data */
    ret = iotdns_cloud_endpoint_get(NULL, endpoint_mgr.regist_key, &endpoint_mgr.endpoint);
    return ret;
}

/**
 * @brief Retrieves the Tuya endpoint.
 *
 * This function returns a pointer to the Tuya endpoint structure.
 *
 * @return A constant pointer to the Tuya endpoint structure.
 */
const tuya_endpoint_t *tuya_endpoint_get(void)
{
    return (const tuya_endpoint_t *)&endpoint_mgr.endpoint;
}
