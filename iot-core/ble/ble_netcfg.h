/**
 * @file ble_netcfg.h
 * @brief BLE network configuration channel: receives ssid/passwd/token from
 *        the APP over BLE and reports the result through a finish callback.
 *
 * The application layer starts it directly:
 *   tuya_ble_init -> ble_netcfg_start(finish_cb)
 * In the finish callback the application connects to the network itself and
 * publishes EVENT_LINK_ACTIVATE to complete binding activation.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef _BLE_NETCFG_H_
#define _BLE_NETCFG_H_

#include "tuya_cloud_types.h"

#ifndef WIFI_SSID_LEN
#define WIFI_SSID_LEN 32
#endif
#ifndef WIFI_PASSWD_LEN
#define WIFI_PASSWD_LEN 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Token length delivered by the APP over BLE */
#define BLE_NETCFG_TOKEN_LEN 16

/**
 * @brief BLE network configuration result
 *
 * Delivered by the APP over BLE during binding: ssid/passwd are used to join
 * the network, token is used for binding activation.
 */
typedef struct {
    /** ssid array */
    uint8_t ssid[WIFI_SSID_LEN + 1];
    /** length of ssid array */
    uint8_t s_len;
    /** password array */
    uint8_t passwd[WIFI_PASSWD_LEN + 1];
    /** length of password array */
    uint8_t p_len;
    /** token array */
    uint8_t token[BLE_NETCFG_TOKEN_LEN + 1];
    /** length of token array */
    uint8_t t_len;
} ble_netcfg_info_t;

/**
 * @brief BLE network configuration finish callback
 *
 * @param[in] info configuration result (ssid/passwd/token)
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
typedef int (*ble_netcfg_finish_cb_t)(ble_netcfg_info_t *info);

/**
 * @brief Start BLE network configuration: register the channel and refresh
 *        advertising
 *
 * @param[in] cb finish callback (must not be NULL)
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
int ble_netcfg_start(ble_netcfg_finish_cb_t cb);

/**
 * @brief Stop BLE network configuration: remove the channel (safe to call
 *        when not started)
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
int ble_netcfg_stop(void);

#ifdef __cplusplus
}
#endif
#endif // _BLE_NETCFG_H_
