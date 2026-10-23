/**
 * @file core_x509.c
 * @brief Implementation of X.509 certificate utilities for Tuya SDK.
 *
 * This source file provides the implementation of functionalities related to
 * X.509 certificates within the Tuya SDK. It includes functions for checking if
 * a buffer contains a CA certificate in PEM format, converting PEM format
 * certificates to DER format, and other utilities that facilitate the handling
 * and manipulation of X.509 certificates. These functionalities are crucial for
 * secure communication and authentication in Tuya-based applications, ensuring
 * that devices and services can verify each other's identities through trusted
 * certificates.
 *
 * The implementation leverages the mbedtls library for parsing and handling
 * X.509 certificates, providing a robust and secure foundation for certificate
 * operations. This file is part of the Tuya IoT Development Platform and is
 * intended for use in Tuya-based applications, enhancing the security posture
 * of applications and devices within the Tuya ecosystem.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "core_api.h"
#include "core_x509.h"
#include "mbedtls/pem.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"

/**
 * @brief Checks if the given buffer contains a CA (Certificate Authority) in
 * PEM format.
 *
 * This function checks whether the provided buffer contains a CA certificate in
 * PEM (Privacy-Enhanced Mail) format. The buffer should contain the certificate
 * data, and the length of the buffer should be specified in the 'buflen'
 * parameter.
 *
 * @param buf Pointer to the buffer containing the certificate data.
 * @param buflen Length of the buffer in bytes.
 * @return BOOL_T Returns 'TRUE' if the buffer contains a CA certificate in PEM
 * format, 'FALSE' otherwise.
 */
BOOL_T tuya_x509_is_ca_pem_format(uint8_t *buf, uint32_t buflen)
{
    if (buflen != 0 && buf[buflen - 1] == '\0' && strstr((const char *)buf, "-----BEGIN CERTIFICATE-----") != NULL) {
        return TRUE;
    }

    return FALSE;
}

/**
 * @brief Converts a PEM-encoded X.509 certificate to DER format.
 *
 * This function takes a buffer containing a PEM-encoded X.509 certificate and
 * converts it to DER format. The converted DER certificate is stored in the
 * output buffer provided by the caller.
 *
 * @param buf Pointer to the buffer containing the PEM-encoded certificate.
 * @param buflen Length of the buffer containing the PEM-encoded certificate.
 * @param out Pointer to the output buffer where the converted DER certificate
 * will be stored.
 * @param outlen Pointer to the variable that will hold the length of the
 * converted DER certificate.
 *
 * @return OPERATE_RET Returns OPRT_OK if the conversion is successful, or an
 * error code if it fails.
 */
OPERATE_RET tuya_x509_pem2der(uint8_t *buf, uint32_t buflen, uint8_t **out, uint32_t *outlen)
{
    OPERATE_RET rt = OPRT_OK;
#if defined(MBEDTLS_PEM_PARSE_C)
    int32_t ret;
    mbedtls_pem_context pem;
    if (buf == NULL || out == NULL || buflen < 64) {
        return OPRT_INVALID_PARM;
    }

    size_t use_len;
    mbedtls_pem_init(&pem);

    /* If we get there, we know the string is null-terminated */
    ret = mbedtls_pem_read_buffer(&pem, "-----BEGIN CERTIFICATE-----", "-----END CERTIFICATE-----", buf, NULL, 0,
                                  &use_len);
    if (ret == 0) {
        /* Was PEM encoded */
        *out = core_malloc(pem.MBEDTLS_PRIVATE(buflen));
        if (NULL == *out) {
            PR_ERR("core_malloc err");
            goto EXIT;
        }
        memset(*out, 0, pem.MBEDTLS_PRIVATE(buflen));
        memcpy(*out, pem.MBEDTLS_PRIVATE(buf), pem.MBEDTLS_PRIVATE(buflen));
        *outlen = pem.MBEDTLS_PRIVATE(buflen);
    } else if (ret == MBEDTLS_ERR_PEM_BAD_INPUT_DATA) {
        rt = OPRT_COM_ERROR;
        goto EXIT;
    } else if (ret != MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT) {
        rt = OPRT_COM_ERROR;
        goto EXIT;
    }

EXIT:
    mbedtls_pem_free(&pem);
#endif

    return rt;
}

/**
 * @brief Retrieves the serial number from an X.509 certificate.
 *
 * This function retrieves the serial number from an X.509 certificate and
 * stores it in the provided buffer.
 *
 * @param buf Pointer to the buffer where the serial number will be stored.
 * @param buflen The length of the buffer.
 * @param out Pointer to the output buffer where the serial number will be
 * copied.
 * @param outlen Pointer to the variable that will store the length of the
 * serial number.
 *
 * @return OPERATE_RET Returns the status of the operation.
 */
OPERATE_RET tuya_x509_get_serial(uint8_t *buf, uint32_t buflen, uint8_t out[32], uint32_t *outlen)
{
    OPERATE_RET rt = OPRT_OK;

#if (TUYA_SECURITY_LEVEL >= TUYA_SL_1)
    int ret;
    mbedtls_x509_crt cert;
    uint32_t nr;
    if (buf == NULL || out == NULL) {
        return (OPRT_INVALID_PARM);
    }

    mbedtls_x509_crt_init(&cert);
    ret = mbedtls_x509_crt_parse(&cert, buf, buflen);
    if (ret != 0) {
        PR_ERR("x509_crt_parse fail ret = %d", ret);
        rt = OPRT_COM_ERROR;
        goto EXIT;
    }

    nr = (cert.serial.len <= 32) ? cert.serial.len : 28;
    memcpy(out, cert.serial.p, nr);
    *outlen = nr;

EXIT:

    mbedtls_x509_crt_free(&cert);
#endif

    return rt;
}

/**
 * @brief Retrieves the fingerprint of an X509 certificate.
 *
 * This function calculates the fingerprint of an X509 certificate and stores it
 * in the provided output buffer.
 *
 * @param buf Pointer to the X509 certificate buffer.
 * @param buflen Length of the X509 certificate buffer.
 * @param type The type of fingerprint to calculate.
 * @param out Pointer to the output buffer where the fingerprint will be stored.
 * @param outlen Pointer to the variable that will store the length of the
 * calculated fingerprint.
 *
 * @return OPERATE_RET Returns OPRT_OK on success, or an error code on failure.
 */
OPERATE_RET tuya_x509_get_fingerprint(uint8_t *buf, uint32_t buflen, X509_fingerprint type, uint8_t out[64],
                                      uint32_t *outlen)
{
    OPERATE_RET rt = OPRT_OK;
    int32_t ret = OPRT_OK;
    BOOL_T pem_format = FALSE;
    uint8_t *p = NULL;
    uint32_t size;

    if (buf == NULL || out == NULL || buflen < 64) {
        return (OPRT_INVALID_PARM);
    }

    p = buf;
    size = buflen;

    pem_format = tuya_x509_is_ca_pem_format(buf, buflen);
    if (pem_format) {
        ret = tuya_x509_pem2der(buf, buflen, &p, &size);
        if (OPRT_OK != ret) {
            PR_ERR("pem to der err,ret:%d", ret);
            return ret;
        }
    }

    if (type == X509_fingerprint_SHA256) {
        ret = mbedtls_sha256(p, size, out, 0);
        *outlen = 32;
    } else if (type == X509_fingerprint_SHA1) {
        ret = mbedtls_sha1(p, size, out);
        *outlen = 16;
    } else {
        rt = OPRT_INVALID_PARM;
    }

    if (ret != 0) {
        rt = OPRT_COM_ERROR;
    }

    if (pem_format) {
        core_free(p);
    }

    return rt;
}
