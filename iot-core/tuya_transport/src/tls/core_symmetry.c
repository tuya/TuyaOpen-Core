/**
 * @file core_symmetry.c
 * @brief This file provides the implementation of AES encryption and decryption
 * functionalities. It includes functions for performing encryption and
 * decryption operations in various modes (ECB, CBC, CTR), the PKCS7 padding
 * helpers, and the one-shot buffer level helpers.
 *
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_iot_config.h"
#include "core_symmetry.h"
#include "core_log.h"
#include "core_memory.h"

/**
 * @brief Performs AES-CTR encryption or decryption using the provided context
 * and parameters.
 *
 * This function encrypts or decrypts the input data using the AES-CTR mode of
 * operation.
 *
 * @param ctx The handle to the AES-CTR context.
 * @param length The length of the input data.
 * @param nc_off The offset for the nonce counter.
 * @param nonce_counter The nonce counter value.
 * @param stream_block The stream block value.
 * @param input The input data to be encrypted or decrypted.
 * @param output The output buffer to store the encrypted or decrypted data.
 *
 * @return The result of the operation.
 */
OPERATE_RET core_aes_crypt_ctr(CORE_SYMMETRY_HANDLE ctx, size_t length, size_t *nc_off, uint8_t nonce_counter[16],
                              uint8_t stream_block[16], uint8_t *input, uint8_t *output)
{
    OPERATE_RET ret;
    int32_t c, i;
    size_t n;

    if ((nc_off == NULL) || (nonce_counter == NULL) || (stream_block == NULL)) {
        ret = OPRT_INVALID_PARM;
        goto exit;
    }

    n = *nc_off;

    if (n > 0x0F) {
        ret = OPRT_INVALID_PARM;
        goto exit;
    }

    while (length--) {
        if (n == 0) {
            if ((ret = core_aes_crypt_ecb(ctx, SYMMETRY_ENCRYPT, 16, nonce_counter, stream_block)) != OPRT_OK) {
                goto exit;
            }

            for (i = 16; i > 0; i--)
                if (++nonce_counter[i - 1] != 0) {
                    break;
                }
        }
        c = *input++;
        *output++ = (uint8_t)(c ^ stream_block[n]);

        n = (n + 1) & 0x0F;
    }

    *nc_off = n;
    ret = OPRT_OK;

exit:

    return (ret);
}

/**
 * @brief Encodes data using AES-128 ECB mode.
 *
 * This function encodes the input data using AES-128 ECB mode with the provided
 * key.
 *
 * @param data Pointer to the input data to be encoded.
 * @param len Length of the input data.
 * @param ec_data Pointer to the output encoded data.
 * @param key Pointer to the AES-128 key.
 *
 * @return OPERATE_RET Returns the status of the encoding operation.
 */
OPERATE_RET core_aes128_ecb_encode_raw(uint8_t *data, size_t len, uint8_t *ec_data, uint8_t *key)
{
    OPERATE_RET ret;
    CORE_SYMMETRY_HANDLE ctx = NULL;

    if ((ret = core_aes_create_init(&ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_setkey_enc(ctx, key, 128)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_crypt_ecb(ctx, SYMMETRY_ENCRYPT, len, data, ec_data)) != OPRT_OK) {
        goto exit;
    }

exit:
    if (ctx) {
        ret = core_aes_free(ctx);
    }

    return (ret);
}

/**
 * @brief Decrypts data using AES-128 ECB mode.
 *
 * This function decrypts the input data using AES-128 ECB mode with the
 * provided key.
 *
 * @param data The input data to be decrypted.
 * @param len The length of the input data.
 * @param dec_data The buffer to store the decrypted data.
 * @param key The key used for decryption.
 *
 * @return The result of the decryption operation.
 *         - OPRT_OK if the decryption was successful.
 *         - An error code if an error occurred during decryption.
 */
OPERATE_RET core_aes128_ecb_decode_raw(uint8_t *data, size_t len, uint8_t *dec_data, uint8_t *key)
{
    OPERATE_RET ret;
    CORE_SYMMETRY_HANDLE ctx = NULL;

    if ((ret = core_aes_create_init(&ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_setkey_dec(ctx, key, 128)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_crypt_ecb(ctx, SYMMETRY_DECRYPT, len, data, dec_data)) != OPRT_OK) {
        goto exit;
    }

exit:
    if (ctx) {
        ret = core_aes_free(ctx);
    }

    return (ret);
}

/**
 * @brief Encrypts data using AES-128 in CBC mode.
 *
 * This function takes the input data, key, initialization vector (IV), and
 * encrypts the data using AES-128 in CBC mode.
 *
 * @param data The input data to be encrypted.
 * @param len The length of the input data.
 * @param key The encryption key.
 * @param iv The initialization vector (IV).
 * @param ec_data The encrypted output data.
 * @return The result of the encryption operation.
 *         - OPRT_OK if the encryption was successful.
 *         - An error code if the encryption failed.
 */
OPERATE_RET core_aes128_cbc_encode_raw(uint8_t *data, size_t len, uint8_t *key, uint8_t *iv, uint8_t *ec_data)
{
    OPERATE_RET ret;
    CORE_SYMMETRY_HANDLE ctx = NULL;

    if ((ret = core_aes_create_init(&ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_setkey_enc(ctx, key, 128)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_crypt_cbc(ctx, SYMMETRY_ENCRYPT, len, iv, data, ec_data)) != OPRT_OK) {
        goto exit;
    }

exit:
    if (ctx) {
        ret = core_aes_free(ctx);
    }

    return (ret);
}

/**
 * @brief Decrypts data using AES-128 CBC mode.
 *
 * This function decrypts the input data using the AES-128 CBC mode with the
 * provided key and initialization vector (IV).
 *
 * @param data The input data to be decrypted.
 * @param len The length of the input data.
 * @param key The AES-128 decryption key.
 * @param iv The initialization vector (IV) for CBC mode.
 * @param dec_data The buffer to store the decrypted data.
 * @return The result of the decryption operation.
 *         - OPRT_OK: Decryption was successful.
 *         - Other error codes: Decryption failed.
 */
OPERATE_RET core_aes128_cbc_decode_raw(uint8_t *data, size_t len, uint8_t *key, uint8_t *iv, uint8_t *dec_data)
{
    OPERATE_RET ret;
    CORE_SYMMETRY_HANDLE ctx = NULL;

    if ((ret = core_aes_create_init(&ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_setkey_dec(ctx, key, 128)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_crypt_cbc(ctx, SYMMETRY_DECRYPT, len, iv, data, dec_data)) != OPRT_OK) {
        goto exit;
    }

exit:
    if (ctx) {
        ret = core_aes_free(ctx);
    }

    return (ret);
}

/**
 * @brief Encrypts data using AES-256 in CBC mode.
 *
 * This function takes the input data, key, initialization vector (IV), and
 * encrypts the data using AES-256 in CBC mode.
 *
 * @param data Pointer to the data to be encrypted.
 * @param len Length of the data to be encrypted.
 * @param key Pointer to the AES-256 key.
 * @param iv Pointer to the initialization vector (IV).
 * @param ec_data Pointer to the encrypted data output.
 *
 * @return OPERATE_RET Returns the status of the encryption operation.
 */
OPERATE_RET core_aes256_cbc_encode_raw(uint8_t *data, size_t len, uint8_t *key, uint8_t *iv, uint8_t *ec_data)
{
    OPERATE_RET ret;
    CORE_SYMMETRY_HANDLE ctx = NULL;

    if ((ret = core_aes_create_init(&ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_setkey_enc(ctx, key, 256)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_crypt_cbc(ctx, SYMMETRY_ENCRYPT, len, iv, data, ec_data)) != OPRT_OK) {
        goto exit;
    }

exit:
    if (ctx) {
        ret = core_aes_free(ctx);
    }

    return (ret);
}

/**
 * @brief Decrypts data using AES-256 in CBC mode.
 *
 * This function takes the input data, key, initialization vector (IV), and
 * decrypts the data using AES-256 in CBC mode.
 *
 * @param data Pointer to the input data to be decrypted.
 * @param len Length of the input data.
 * @param key Pointer to the AES-256 key.
 * @param iv Pointer to the initialization vector (IV).
 * @param dec_data Pointer to the decrypted data.
 * @return OPERATE_RET Returns the status of the decryption operation.
 */
OPERATE_RET core_aes256_cbc_decode_raw(uint8_t *data, size_t len, uint8_t *key, uint8_t *iv, uint8_t *dec_data)
{
    OPERATE_RET ret;
    CORE_SYMMETRY_HANDLE ctx = NULL;

    if ((ret = core_aes_create_init(&ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_setkey_dec(ctx, key, 256)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_crypt_cbc(ctx, SYMMETRY_DECRYPT, len, iv, data, dec_data)) != OPRT_OK) {
        goto exit;
    }

exit:
    if (ctx) {
        ret = core_aes_free(ctx);
    }

    return (ret);
}

/**
 * @brief Performs AES-256 CTR mode encryption/decryption on the input data.
 *
 * This function takes the input data, key, nonce counter, and stream block as
 * parameters and performs AES-256 CTR mode encryption/decryption on the input
 * data. The result is stored in the output buffer.
 *
 * @param input The input data to be encrypted/decrypted.
 * @param len The length of the input data.
 * @param key The AES-256 key used for encryption/decryption.
 * @param nc_off The offset of the nonce counter.
 * @param nonce_counter The nonce counter used for encryption/decryption.
 * @param stream_block The stream block used for encryption/decryption.
 * @param output The output buffer to store the encrypted/decrypted data.
 * @return The result of the operation.
 */
OPERATE_RET core_aes256_ctr_raw(uint8_t *input, size_t len, uint8_t *key, size_t *nc_off, uint8_t nonce_counter[16],
                               uint8_t stream_block[16], uint8_t *output)
{
    OPERATE_RET ret;
    CORE_SYMMETRY_HANDLE ctx = NULL;

    if ((ret = core_aes_create_init(&ctx)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_setkey_enc(ctx, key, 256)) != OPRT_OK) {
        goto exit;
    }

    if ((ret = core_aes_crypt_ctr(ctx, len, nc_off, nonce_counter, stream_block, input, output)) != OPRT_OK) {
        goto exit;
    }

exit:
    if (ctx) {
        ret = core_aes_free(ctx);
    }

    return (ret);
}

static void add_pkcs_padding(uint8_t *output, size_t output_len, size_t data_len)
{
    size_t padding_len = output_len - data_len;
    uint8_t i;

    // PKCS7 (and PKCS5) padding: fill with ll bytes, with ll = padding_len
    for (i = 0; i < padding_len; i++) {
        output[data_len + i] = (uint8_t)padding_len;
    }
}

static uint32_t __Add_Pkcs(uint8_t *p, uint32_t len)
{
    // The tail is filled with pkcs7, returning the length after filling
    int cz = 16 - len % 16;

    add_pkcs_padding(p, len + cz, len);

    return (len + cz);
}

/**
 * @brief Performs PKCS7 padding on a buffer.
 *
 * This function applies PKCS7 padding to the given buffer to ensure that the
 * length of the buffer is a multiple of 16 bytes.
 *
 * @param p_buffer Pointer to the buffer to be padded.
 * @param length   Length of the buffer.
 *
 * @return The new length of the buffer after padding.
 */
uint32_t core_pkcs7padding_buffer(uint8_t *p_buffer, uint32_t length)
{
    return __Add_Pkcs(p_buffer, length);
}

/**
 * @brief Calculates the actual length of decrypted data after AES decryption.
 *
 * This function takes in the decrypted data and its length as input and
 * calculates the actual length of the decrypted data after AES decryption.
 *
 * @param dec_data Pointer to the decrypted data.
 * @param dec_data_len Length of the decrypted data.
 * @return The actual length of the decrypted data after AES decryption.
 */
int32_t core_aes_get_actual_length(uint8_t *dec_data, uint32_t dec_data_len)
{
    if ((NULL == dec_data) || (0 == dec_data_len) || (dec_data_len % 16 != 0)) {
        return OPRT_INVALID_PARM;
    }

    uint8_t lastdata_val = dec_data[dec_data_len - 1];
    if (lastdata_val > 16) {
        return -2;
    }

    if (dec_data_len <= lastdata_val) {
        return -3;
    }

    return dec_data_len - lastdata_val;
}

/**
 * @brief Encodes data using AES-128 ECB mode.
 *
 * This function encodes the input data using AES-128 ECB mode with the provided
 * key.
 *
 * @param data Pointer to the input data to be encoded.
 * @param len Length of the input data.
 * @param ec_data Pointer to the output encoded data.
 * @param key Pointer to the AES-128 key.
 *
 * @return OPERATE_RET Returns the status of the encoding operation.
 */
OPERATE_RET core_aes128_ecb_encode(uint8_t *data, uint32_t len, uint8_t **ec_data, uint32_t *ec_len, uint8_t *key)
{
    if (NULL == data || NULL == key || NULL == ec_data || NULL == ec_len || 0 == len) {
        return OPRT_INVALID_PARM;
    }

    *ec_data = core_malloc(len + 16);
    if (NULL == *ec_data) {
        return OPRT_MALLOC_FAILED;
    }

    memcpy(*ec_data, data, len);
    uint32_t pkcs7Len = __Add_Pkcs(*ec_data, len);
    *ec_len = pkcs7Len;

    OPERATE_RET ret = core_aes128_ecb_encode_raw(*ec_data, pkcs7Len, *ec_data, key);
    if (ret != OPRT_OK) {
        core_free(*ec_data);
        *ec_data = NULL;
        return ret;
    }
    return OPRT_OK;
}

/**
 * @brief Decrypts data using AES-128 ECB mode.
 *
 * This function decrypts the input data using AES-128 ECB mode with the
 * provided key.
 *
 * @param data The input data to be decrypted.
 * @param len The length of the input data.
 * @param dec_data The buffer to store the decrypted data.
 * @param key The encryption key used for decryption.
 *
 * @return The result of the decryption operation.
 *         - OPRT_OK: Decryption was successful.
 *         - Other error codes: Decryption failed.
 */
OPERATE_RET core_aes128_ecb_decode(uint8_t *data, uint32_t len, uint8_t **dec_data, uint32_t *dec_len, uint8_t *key)
{
    if (NULL == data || 0 == len || NULL == key || NULL == dec_data || NULL == dec_len) {
        return OPRT_INVALID_PARM;
    }

    *dec_len = len;
    *dec_data = (uint8_t *)core_malloc(len + 1);
    if (NULL == *dec_data) {
        return OPRT_MALLOC_FAILED;
    }

    memcpy(*dec_data, data, len);
    (*dec_data)[len] = 0;
    OPERATE_RET ret = core_aes128_ecb_decode_raw(*dec_data, len, *dec_data, key);
    if (ret != OPRT_OK) {
        core_free(*dec_data);
        *dec_data = NULL;
        return ret;
    }
    (*dec_data)[len] = 0;
    return OPRT_OK;
}

/**
 * @brief Encrypts data using AES-128 in CBC mode.
 *
 * This function takes the input data, key, initialization vector (IV), and
 * encrypts the data using AES-128 in CBC mode.
 *
 * @param data The input data to be encrypted.
 * @param len The length of the input data.
 * @param key The encryption key.
 * @param iv The initialization vector (IV).
 * @param ec_data The encrypted data output.
 * @return The result of the encryption operation.
 */
OPERATE_RET core_aes128_cbc_encode(uint8_t *data, uint32_t len, uint8_t *key, uint8_t *iv, uint8_t **ec_data,
                                  uint32_t *ec_len)
{
    if (NULL == data || 0 == len || NULL == key || NULL == iv || NULL == ec_data || NULL == ec_len) {
        return OPRT_INVALID_PARM;
    }

    *ec_data = core_malloc(len + 16);
    if (NULL == *ec_data) {
        return OPRT_MALLOC_FAILED;
    }
    memcpy(*ec_data, data, len);
    uint32_t pkcs7Len = __Add_Pkcs(*ec_data, len);
    *ec_len = pkcs7Len;

    OPERATE_RET ret = core_aes128_cbc_encode_raw(*ec_data, pkcs7Len, key, iv, *ec_data);
    if (ret != OPRT_OK) {
        core_free(*ec_data);
        *ec_data = NULL;
        return ret;
    }
    return OPRT_OK;
}

/**
 * @brief Decrypts data using AES-128 CBC mode.
 *
 * This function decrypts the input data using AES-128 CBC mode with the
 * provided key and initialization vector (IV).
 *
 * @param data The input data to be decrypted.
 * @param len The length of the input data.
 * @param key The AES-128 encryption key.
 * @param iv The initialization vector (IV) for CBC mode.
 * @param dec_data The buffer to store the decrypted data.
 *
 * @return The result of the decryption operation.
 *         - OPRT_OK: Decryption was successful.
 *         - Other error codes: Decryption failed.
 */
OPERATE_RET core_aes128_cbc_decode(uint8_t *data, uint32_t len, uint8_t *key, uint8_t *iv, uint8_t **dec_data,
                                  uint32_t *dec_len)
{
    if (NULL == data || 0 == len || NULL == key || NULL == iv || NULL == dec_data || NULL == dec_len) {
        return OPRT_INVALID_PARM;
    }

    *dec_len = len;
    *dec_data = (uint8_t *)core_malloc(len + 1);
    if (NULL == *dec_data) {
        return OPRT_MALLOC_FAILED;
    }

    memcpy(*dec_data, data, len);
    (*dec_data)[len] = 0;
    OPERATE_RET ret = core_aes128_cbc_decode_raw(*dec_data, len, key, iv, *dec_data);
    if (ret != OPRT_OK) {
        core_free(*dec_data);
        *dec_data = NULL;
        return ret;
    }
    (*dec_data)[len] = 0;
    return OPRT_OK;
}

/**
 * @brief Frees the memory allocated for AES data.
 *
 * This function frees the memory allocated for AES data.
 *
 * @param data Pointer to the AES data to be freed.
 * @return OPERATE_RET Returns OPERATE_RET_OK if the memory is successfully
 * freed, otherwise returns an error code.
 */
OPERATE_RET core_aes_free_data(uint8_t *data)
{
    if (data == NULL) {
        return OPRT_INVALID_PARM;
    }

    core_free(data);
    return OPRT_OK;
}
