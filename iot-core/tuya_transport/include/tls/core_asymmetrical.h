/**
 * @file core_asymmetrical.h
 * @brief Asymmetrical cryptography utilities for Tuya SDK.
 *
 * This header file provides the definitions and function prototypes for
 * asymmetrical cryptography operations within the Tuya SDK. It includes support
 * for key generation, conversion between different key formats, and other
 * cryptographic operations using the mbedtls library. The file defines macros
 * for specifying output modes (none, private, public) and formats (PEM, DER)
 * for cryptographic keys.
 *
 * The functions provided in this file allow for the generation of cryptographic
 * keys using RSA or ECC algorithms, conversion of mbedtls key contexts to and
 * from buffers in specified formats, and other utilities necessary for secure
 * communication and data protection in Tuya-based applications.
 *
 * @note This file is part of the Tuya IoT Development Platform and is intended
 * for use in Tuya-based applications. It relies on the mbedtls library for
 * cryptographic operations and is designed to be compatible with Tuya's IoT and
 * security protocols.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __CORE_ASYMMETRICAL_H__
#define __CORE_ASYMMETRICAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_iot_config.h"
#include "tuya_tls.h"
#include "mbedtls/rsa.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"

#define OUTPUT_MODE_NONE    0
#define OUTPUT_MODE_PRIVATE 1
#define OUTPUT_MODE_PUBLIC  2

#define OUTPUT_FORMAT_PEM 0
#define OUTPUT_FORMAT_DER 1

#ifdef __cplusplus
}
#endif

#endif
