/**
 * @file core_hash.c
 * @brief This file provides the implementations of SHA1, SHA224, SHA256, and
 * MD5 hash functions. It includes functions for directly computing the hash of
 * a given input buffer, as well as the HMAC (mac) wrappers built on top of
 * them.
 *
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_iot_config.h"
#include "core_hash.h"
#include "core_log.h"

/**
 * @brief          This function calculates the SHA-224 or SHA-256
 *                 checksum of a buffer.
 *
 *                 The function allocates the context, performs the
 *                 calculation, and frees the context.
 *
 *                 The SHA-256 result is calculated as
 *                 output = SHA-256(input buffer).
 *
 * @param[in] input:    The buffer holding the data. This must be a readable
 *                 buffer of length \p ilen Bytes.
 * @param[in] ilen:     The length of the input data in Bytes.
 * @param[out] output:   The SHA-224 or SHA-256 checksum result. This must
 *                 be a writable buffer of length \c 32 Bytes.
 * @param[in] is224:    Determines which function to use. This must be
 *                 either \c 0 for SHA-256, or \c 1 for SHA-224.
 *
 * @return         OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha256_ret(const uint8_t *input, size_t ilen, uint8_t output[32], int32_t is224)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    CORE_HASH_HANDLE ctx = NULL;

    if ((ret = core_sha256_create_init(&ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha256_starts_ret(ctx, is224)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha256_update_ret(ctx, input, ilen)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha256_finish_ret(ctx, output)) != OPRT_OK) {
        goto exit;
    }

exit:
    if (ctx) {
        ret |= core_sha256_free(ctx);
    }

    return (ret);
}
/**
 * @brief          This function calculates the MD5
 *                 checksum of a buffer.
 *
 *                 The function allocates the context, performs the
 *                 calculation, and frees the context.
 *
 *                 The MD5 result is calculated as
 *                 output = MD5(input buffer).
 *
 * @param[in] input:    The buffer holding the data. This must be a readable
 *                 buffer of length \p ilen Bytes.
 * @param[in] ilen:     The length of the input data in Bytes.
 * @param[out] output:   The SHA-224 or MD5 checksum result. This must
 *                 be a writable buffer of length \c 16 Bytes.
 *
 * @return         OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_md5_ret(const uint8_t *input, size_t ilen, uint8_t output[16])
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    CORE_HASH_HANDLE ctx = NULL;

    if ((ret = core_md5_create_init(&ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_md5_starts_ret(ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_md5_update_ret(ctx, input, ilen)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_md5_finish_ret(ctx, output)) != OPRT_OK) {
        goto exit;
    }

exit:
    if (ctx) {
        ret |= core_md5_free(ctx);
    }

    return (ret);
}

/**
 * @brief          This function calculates the sha1
 *                 checksum of a buffer.
 *
 *                 The function allocates the context, performs the
 *                 calculation, and frees the context.
 *
 *                 The sha1 result is calculated as
 *                 output = sha1(input buffer).
 *
 * @param[in] input:    The buffer holding the data. This must be a readable
 *                 buffer of length \p ilen Bytes.
 * @param[in] ilen:     The length of the input data in Bytes.
 * @param[out] output:   The SHA-224 or sha1 checksum result. This must
 *                 be a writable buffer of length \c 20 Bytes.
 *
 * @return         OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha1_ret(const uint8_t *input, size_t ilen, uint8_t output[20])
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    CORE_HASH_HANDLE ctx = NULL;

    if ((ret = core_sha1_create_init(&ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha1_starts_ret(ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha1_update_ret(ctx, input, ilen)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha1_finish_ret(ctx, output)) != OPRT_OK) {
        goto exit;
    }

exit:
    if (ctx) {
        ret |= core_sha1_free(ctx);
    }

    return (ret);
}

/**
 * @brief This function Create&initializes a sha256 maccontext.
 *
 * @param[out] hmac_handle: sha256_mac handle
 *
 * @note This API is used to create and init sha256_mac.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha256_mac_create_init(core_hash_mac_context_t *hmac_handle)
{
    if (hmac_handle == NULL) {
        return OPRT_INVALID_PARM;
    }

    memset(hmac_handle, 0, sizeof(core_hash_mac_context_t));
    return core_sha256_create_init(&(hmac_handle->ctx));
}
/**
 * @brief This function clears a sha256 mac context.
 *
 * @param[in] hmac_handle: sha256 mac handle
 *
 * @note This API is used to release sha256.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha256_mac_free(core_hash_mac_context_t *hmac_handle)
{
    OPERATE_RET ret = OPRT_OK;

    if (hmac_handle != NULL) {
        ret = core_sha256_free(hmac_handle->ctx);
        memset(hmac_handle, 0, sizeof(core_hash_mac_context_t));
    }

    return ret;
}
/**
 * @brief This function starts a sha256 mac checksum
 *                 calculation.
 *
 * @param[in] hmac_handle: The context to use. This must be initialized.
 * @param[in] key:    key
 * @param[in] keylen: keylen
 *
 * @note This API is used to start sha256 mac.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha256_mac_starts(core_hash_mac_context_t *hmac_handle, const uint8_t *key, size_t keylen)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    uint8_t sum[32];
    uint8_t *ipad, *opad;
    size_t i;
    CORE_HASH_HANDLE ctx;

    ctx = hmac_handle->ctx;

    if (keylen > 64) {
        if ((ret = core_sha256_ret(key, keylen, sum, 0)) != OPRT_OK) {
            goto cleanup;
        }
        keylen = 32;
        key = sum;
    }

    ipad = hmac_handle->ipad;
    opad = hmac_handle->opad;

    memset(ipad, 0x36, sizeof(hmac_handle->ipad));
    memset(opad, 0x5C, sizeof(hmac_handle->opad));

    for (i = 0; i < keylen; i++) {
        ipad[i] = (uint8_t)(ipad[i] ^ key[i]);
        opad[i] = (uint8_t)(opad[i] ^ key[i]);
    }

    if ((ret = core_sha256_starts_ret(ctx, 0)) != OPRT_OK) {
        goto cleanup;
    }

    if ((ret = core_sha256_update_ret(ctx, ipad, 64)) != OPRT_OK) {
        goto cleanup;
    }

cleanup:
    memset(sum, 0, sizeof(sum));

    return (ret);
}
/**
 * @brief This function feeds an input buffer into an ongoing
 *                 sha256 mac checksum calculation.
 *
 * @param[in] hmac_handle: The context to use. This must be initialized.
 * @param[in] input:    The buffer holding the data. This must be a readable
 *                 buffer of length \p ilen Bytes.
 * @param[in] ilen:     The length of the input data in Bytes.
 *
 * @note This API is used to update sha256 mac.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha256_mac_update(core_hash_mac_context_t *hmac_handle, const uint8_t *input, size_t ilen)
{
    return core_sha256_update_ret(hmac_handle->ctx, input, ilen);
}
/**
 * @brief This function finishes the sha256 mac operation, and writes
 *                 the result to the output buffer.
 *
 * @param[in] hmac_handle: The context to use. This must be initialized.
 * @param[out] output:   The sha256 mac checksum result.
 *                 This must be a writable buffer of length \c 32 Bytes.
 *
 * @note This API is used to out sha256 mac result.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha256_mac_finish(core_hash_mac_context_t *hmac_handle, uint8_t *output)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    uint8_t tmp[32];
    uint8_t *opad;
    CORE_HASH_HANDLE ctx = NULL;

    ctx = hmac_handle->ctx;
    opad = hmac_handle->opad;

    if ((ret = core_sha256_finish_ret(hmac_handle->ctx, tmp)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha256_starts_ret(ctx, 0)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha256_update_ret(ctx, opad, 64)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha256_update_ret(ctx, tmp, 32)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha256_finish_ret(hmac_handle->ctx, output)) != OPRT_OK) {
        goto exit;
    }

    memset(tmp, 0, sizeof(tmp));

exit:

    return ret;
}
/**
 * @brief          This function calculates the SHA-256 MAC
 *                 checksum of a buffer.
 *
 *                 The function allocates the context, performs the
 *                 calculation, and frees the context.
 *
 *                 The SHA-256 result is calculated as
 *                 output = SHA-256 MAC.
 *
 * @param[in] key:    The buffer holding the data. This must be a readable
 *                 buffer of length \p keylen Bytes.
 * @param[in] keylen:     The length of the key data in Bytes.*
 * @param[in] input:    The buffer holding the data. This must be a readable
 *                 buffer of length \p ilen Bytes.
 * @param[in] ilen:     The length of the input data in Bytes.
 * @param[out] output:   The SHA-256 MAC checksum result. This must
 *                 be a writable buffer of length \c 32 Bytes.
 *
 * @return         OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha256_mac(const uint8_t *key, size_t keylen, const uint8_t *input, size_t ilen, uint8_t *output)
{
    core_hash_mac_context_t hmac_handle;
    OPERATE_RET ret = OPRT_COM_ERROR;

    if ((ret = core_sha256_mac_create_init(&hmac_handle)) != OPRT_OK) {
        goto cleanup;
    }

    if ((ret = core_sha256_mac_starts(&hmac_handle, key, keylen)) != OPRT_OK) {
        goto cleanup;
    }
    if ((ret = core_sha256_mac_update(&hmac_handle, input, ilen)) != OPRT_OK) {
        goto cleanup;
    }
    if ((ret = core_sha256_mac_finish(&hmac_handle, output)) != OPRT_OK) {
        goto cleanup;
    }

cleanup:
    ret |= core_sha256_mac_free(&hmac_handle);

    return (ret);
}

/**
 * @brief This function Create&initializes a sha1 maccontext.
 *
 * @param[out] hmac_handle: sha1_mac handle
 *
 * @note This API is used to create and init sha1_mac.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha1_mac_create_init(core_hash_mac_context_t *hmac_handle)
{
    if (hmac_handle == NULL) {
        return OPRT_INVALID_PARM;
    }

    memset(hmac_handle, 0, sizeof(core_hash_mac_context_t));
    return core_sha1_create_init(&(hmac_handle->ctx));
}
/**
 * @brief This function clears a sha1 mac context.
 *
 * @param[in] hmac_handle: sha1 mac handle
 *
 * @note This API is used to release sha1.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha1_mac_free(core_hash_mac_context_t *hmac_handle)
{
    OPERATE_RET ret = OPRT_OK;

    if (hmac_handle != NULL) {
        ret = core_sha1_free(hmac_handle->ctx);
        memset(hmac_handle, 0, sizeof(core_hash_mac_context_t));
    }

    return ret;
}
/**
 * @brief This function starts a sha1 mac checksum
 *                 calculation.
 *
 * @param[in] hmac_handle: The context to use. This must be initialized.
 * @param[in] key:    key
 * @param[in] keylen: keylen
 *
 * @note This API is used to start sha1 mac.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha1_mac_starts(core_hash_mac_context_t *hmac_handle, const uint8_t *key, size_t keylen)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    uint8_t sum[20];
    uint8_t *ipad, *opad;
    size_t i;
    CORE_HASH_HANDLE ctx;

    ctx = hmac_handle->ctx;

    if (keylen > 64) {
        if ((ret = core_sha1_ret(key, keylen, sum)) != OPRT_OK) {
            goto cleanup;
        }
        keylen = 20;
        key = sum;
    }

    ipad = hmac_handle->ipad;
    opad = hmac_handle->opad;

    memset(ipad, 0x36, sizeof(hmac_handle->ipad));
    memset(opad, 0x5C, sizeof(hmac_handle->opad));

    for (i = 0; i < keylen; i++) {
        ipad[i] = (uint8_t)(ipad[i] ^ key[i]);
        opad[i] = (uint8_t)(opad[i] ^ key[i]);
    }

    if ((ret = core_sha1_starts_ret(ctx)) != OPRT_OK) {
        goto cleanup;
    }

    if ((ret = core_sha1_update_ret(ctx, ipad, 64)) != OPRT_OK) {
        goto cleanup;
    }

cleanup:
    memset(sum, 0, sizeof(sum));

    return (ret);
}
/**
 * @brief This function feeds an input buffer into an ongoing
 *                 sha1 mac checksum calculation.
 *
 * @param[in] hmac_handle: The context to use. This must be initialized.
 * @param[in] input:    The buffer holding the data. This must be a readable
 *                 buffer of length \p ilen Bytes.
 * @param[in] ilen:     The length of the input data in Bytes.
 *
 * @note This API is used to update sha1 mac.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha1_mac_update(core_hash_mac_context_t *hmac_handle, const uint8_t *input, size_t ilen)
{
    return core_sha1_update_ret(hmac_handle->ctx, input, ilen);
}
/**
 * @brief This function finishes the sha1 mac operation, and writes
 *                 the result to the output buffer.
 *
 * @param[in] hmac_handle: The context to use. This must be initialized.
 * @param[out] output:   The sha1 mac checksum result.
 *                 This must be a writable buffer of length \c 32 Bytes.
 *
 * @note This API is used to out sha1 mac result.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha1_mac_finish(core_hash_mac_context_t *hmac_handle, uint8_t *output)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    uint8_t tmp[20];
    uint8_t *opad;
    CORE_HASH_HANDLE ctx = NULL;

    ctx = hmac_handle->ctx;
    opad = hmac_handle->opad;

    if ((ret = core_sha1_finish_ret(hmac_handle->ctx, tmp)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha1_starts_ret(ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha1_update_ret(ctx, opad, 64)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha1_update_ret(ctx, tmp, 20)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_sha1_finish_ret(hmac_handle->ctx, output)) != OPRT_OK) {
        goto exit;
    }

    memset(tmp, 0, sizeof(tmp));

exit:

    return ret;
}
/**
 * @brief          This function calculates the SHA-256 MAC
 *                 checksum of a buffer.
 *
 *                 The function allocates the context, performs the
 *                 calculation, and frees the context.
 *
 *                 The SHA-256 result is calculated as
 *                 output = SHA-256 MAC.
 *
 * @param[in] key:    The buffer holding the data. This must be a readable
 *                 buffer of length \p keylen Bytes.
 * @param[in] keylen:     The length of the key data in Bytes.*
 * @param[in] input:    The buffer holding the data. This must be a readable
 *                 buffer of length \p ilen Bytes.
 * @param[in] ilen:     The length of the input data in Bytes.
 * @param[out] output:   The SHA-256 MAC checksum result. This must
 *                 be a writable buffer of length \c 32 Bytes.
 *
 * @return         OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET core_sha1_mac(const uint8_t *key, size_t keylen, const uint8_t *input, size_t ilen, uint8_t *output)
{
    core_hash_mac_context_t hmac_handle;
    OPERATE_RET ret = OPRT_COM_ERROR;

    if ((ret = core_sha1_mac_create_init(&hmac_handle)) != OPRT_OK) {
        goto cleanup;
    }

    if ((ret = core_sha1_mac_starts(&hmac_handle, key, keylen)) != OPRT_OK) {
        goto cleanup;
    }
    if ((ret = core_sha1_mac_update(&hmac_handle, input, ilen)) != OPRT_OK) {
        goto cleanup;
    }
    if ((ret = core_sha1_mac_finish(&hmac_handle, output)) != OPRT_OK) {
        goto cleanup;
    }

cleanup:
    ret |= core_sha1_mac_free(&hmac_handle);

    return (ret);
}
