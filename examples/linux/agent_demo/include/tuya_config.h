/**
 * @file tuya_config.h
 * @brief agent_demo credential configuration: TUYA_PRODUCT_ID, TUYA_OPENSDK_UUID
 *        and TUYA_OPENSDK_AUTHKEY.
 *
 * Override priority: -D compile macros > environment variables >
 * tuya_config_secrets.h > the defaults in this file. The secrets header is
 * probed with __has_include and is not tracked by git.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef TUYA_CONFIG_H_
#define TUYA_CONFIG_H_

#if __has_include("tuya_config_secrets.h")
#include "tuya_config_secrets.h"
#endif

/**
 * @brief configure the product information
 *
 * TUYA_PRODUCT_ID: PID, create on the Tuya IoT platform
 * TUYA_DEVICE_UUID: UUID, create on the Tuya IoT platform
 * TUYA_DEVICE_AUTHKEY: AUTHKEY, create on the Tuya IoT platform
 *
 * warning: please replace these production information with your product id
 * and license, otherwise the demo cannot work.
 */
// clang-format off
#ifndef TUYA_PRODUCT_ID
#define TUYA_PRODUCT_ID      "alon7qgyjj8yus74"
#endif

#ifndef TUYA_OPENSDK_UUID
#define TUYA_OPENSDK_UUID      "uuidxxxxxxxxxxxxxxxx"
#endif
#ifndef TUYA_OPENSDK_AUTHKEY
#define TUYA_OPENSDK_AUTHKEY   "keyxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
#endif

// clang-format on

#endif
