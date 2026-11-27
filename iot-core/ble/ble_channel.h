/**
 * @file ble_channel.h
 * @brief Defines the BLE channel management interface for Tuya's BLE SDK.
 *
 * This header file provides the definitions and function prototypes for
 * managing BLE channels within the Tuya BLE SDK. It includes the declaration of
 * the BLE channel type enumeration, function pointer types for channel
 * callbacks, and the API functions for adding, deleting, and processing BLE
 * channels. These channels facilitate the structured communication and data
 * exchange between the BLE device and its connected peers, supporting specific
 * functionalities like network configuration. The API defined in this file is
 * essential for developers working on BLE-based applications that require
 * efficient and organized data transmission mechanisms.
 *
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __BLE_CHANNEL_H__
#define __BLE_CHANNEL_H__

#include "ble_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLE_CHANNLE_NETCFG       = 1,
    BLE_CHANNLE_WIFI_LST     = 3,
    BLE_CHANNLE_NETCFG_STAT  = 4,
    BLE_CHANNLE_DEV_LOG      = 5,
    BLE_CHANNLE_GET_AP_NAME  = 6,
    BLE_CHANNLE_WIFI_PROTECT = 15,
    BLE_CHANNEL_MAX          = 0x0010,
} ble_channel_type_t;

/** 4-byte uplink transparent reply header: 2B flag + 2B big-endian subcmd */
#define BLE_CHANNEL_REPLY_HDR_LEN 4

/**
 * @brief Callback of a BLE channel.
 *
 * @param[in] data      the payload, it is NOT NUL terminated
 * @param[in] len       length of @p data in bytes
 * @param[in] user_data the private data given to @ref ble_channel_add
 */
typedef void (*ble_channel_fn_t)(void *data, uint32_t len, void *user_data);

/**
 * @brief Adds a BLE channel.
 *
 * This function adds a BLE channel of the specified type and associates it with
 * the provided callback function.
 *
 * @param type The type of the BLE channel.
 * @param fn The callback function to be associated with the BLE channel.
 * @param priv_data A pointer to private data that will be passed to the
 * callback function.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int ble_channel_add(ble_channel_type_t type, ble_channel_fn_t fn, void *priv_data);
int ble_channel_del(ble_channel_type_t type);

/**
 * @brief Resets and frees any in-progress downlink subpacket reassembly buffer.
 */
void ble_channel_reset_rx(void);

/**
 * @brief Clears registered channels, capability state, and pending buffers.
 */
void ble_channel_deinit(void);

/**
 * @brief Checks whether a callback is registered for the given channel type.
 */
bool ble_channel_is_registered(ble_channel_type_t type);

/**
 * @brief Sets whether the WiFi hotspot list capability bit should be advertised.
 */
void ble_channel_wifi_list_cap_set(bool enable);

/**
 * @brief Builds the BLE CombosFlag bitmask from registered transparent channels.
 */
uint8_t ble_channel_combos_flag_get(void);

/**
 * @brief Sends a response to the app over the large data transparent channel.
 *
 * @param[in] type  the uplink frame type, e.g. FRM_UPLINK_TRANSPARENT_SPEC_REQ
 * @param[in] data  the frame, owned by this function from now on
 * @param[in] len   length of @p data, the first two bytes are the channel type
 */
void ble_channle_ack(uint16_t type, uint8_t *data, uint32_t len);

/**
 * @brief Sends a pre-allocated reply buffer (with BLE_CHANNEL_REPLY_HDR_LEN prefix) on 0x801F.
 *
 * Ownership of @p buf transfers to ble_channle_ack() which frees it after sending.
 */
void ble_channel_reply_buf(uint16_t subcmd, uint8_t *buf, uint32_t payload_len);

/**
 * @brief Copies @p data and sends it as an uplink transparent reply on 0x801F.
 */
OPERATE_RET ble_channel_reply(uint16_t subcmd, const uint8_t *data, uint32_t len);

/**
 * @brief Processes the BLE session channel.
 *
 * This function is responsible for processing the BLE session channel by
 * handling the specified request packet.
 *
 * @param req The request packet to be processed.
 * @param priv_data A pointer to private data associated with the BLE session
 * channel.
 */
void ble_session_channel_process(ble_packet_t *req, void *priv_data);

#ifdef __cplusplus
}
#endif

#endif
