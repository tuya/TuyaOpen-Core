/**
 * @file core_bluetooth.h
 * @brief CORE Bluetooth Low Energy layer: ops-card dispatch, GAP/GATT event
 * translation, the common Tuya GATT service and the public BLE API.
 *
 * The platform contract is the CORE_BLUETOOTH_OPS_T card
 * core_bluetooth_ops_platform (core_bluetooth_ops.h), reached through
 * core_bluetooth_get_ops(). Two dispatch macros route calls to it:
 * CORE_BLUETOOTH_EXEC_OP returns the card result directly, and
 * CORE_BLUETOOTH_EXEC_OP_VAL stores it in a variable; when the card or the
 * requested member is missing both log with PR_ERR and report
 * OPRT_NOT_SUPPORTED.
 *
 * The kernel GAP and GATT callbacks translate platform events into the
 * unified CORE_BLE_EVT_PARAMS_T and forward them to the event callback
 * registered by core_ble_bt_init(). GAP translation covers stack
 * init/deinit, connect, disconnect, advertisement report, connection
 * parameter request/update and RSSI; GATT translation covers MTU exchange,
 * notify/write/read/subscribe events (the GATT client discovery path was
 * removed: the supported role is peripheral only).
 *
 * core_ble_bt_init() registers the callbacks first, allocates the service and
 * characteristic tables with the contract's maximum sizes, builds the common
 * Tuya GATT service when the peripheral role is requested, and starts the
 * stack. With CORE_BLE_SERVICE_VERSION 2 the service uses a 16-bit UUID and
 * three 128-bit characteristics (write, notify and read); otherwise it uses
 * the version 1 16-bit layout with write and notify characteristics. The
 * Matter characteristic handles are filled on connect when
 * ENABLE_MATTER_TUYA_BLE_CHAR is set. Roles are mapped explicitly to the
 * card's SERVER/CLIENT bits; core_ble_bt_deinit() treats any role outside
 * PERIPHERAL/CENTRAL (BEACON and Mesh included) as unsupported.
 *
 * The remaining API covers address access, maximum link count, advertising
 * control, RSSI, connection parameters, disconnect and the GATT server
 * notify/send/read/MTU paths. The GATT client / central API was
 * removed together with the contract members. Before the service table exists, the notify
 * and read-update paths return OPRT_RESOURCE_NOT_READY instead of
 * dereferencing it.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __CORE_BLUETOOTH_H__
#define __CORE_BLUETOOTH_H__

#include "core_bluetooth_def.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Function for initializing the bluetooth
 * @param   [in] role:      set the ble/bt role
 *          [in] p_event:   Event handler provided by the user.
 * @return  SUCCESS         Initialized successfully.
 *          ERROR
 * */
OPERATE_RET core_ble_bt_init(CORE_BLE_ROLE_E role, const CORE_BLE_EVT_FUNC_CB ble_event);

/**
 * @brief   Function for deinitializing the bluetooth, or specify one role
 * @param   [in] role:      set the ble/bt role
 * @return  SUCCESS         Deinitialized successfully.
 *          ERROR
 * */
OPERATE_RET core_ble_bt_deinit(CORE_BLE_ROLE_E role);

/**
 * @brief   Set the local Bluetooth identity address.
 * @param   [in] p_addr:    pointer to local address
 * @return  SUCCESS         Set Address successfully.
 *          ERROR
 * */
OPERATE_RET core_ble_address_set(CORE_BLE_ADDR_T const *p_addr);

/**
 * @brief   Get the local Bluetooth identity address.
 * @param   [out] p_addr: pointer to local address
 * @return  SUCCESS         Set Address successfully.
 *          ERROR
 * */
OPERATE_RET core_ble_address_get(CORE_BLE_ADDR_T *p_addr);

/**
 * @brief   Get Max Link Size
 * @param   [out] max_mtu:  set mtu size.
 * @return  SUCCESS
 *          ERROR
 * */
OPERATE_RET core_ble_bt_link_max(uint16_t *p_maxlink);

/**
 * @brief   Start advertising
 * @param  [in] p_adv_param : pointer to advertising parameters, see
 * CORE_BLE_ADV_PARAMS_T for details
 * @return  SUCCESS             Successfully started advertising procedure.
 *          ERR_INVALID_STATE   Not in advertising state.
 * */
OPERATE_RET core_ble_advertising_start(CORE_BLE_ADV_PARAMS_T const *p_adv_param);

/**
 * @brief   Setting advertising data
 * @param   [in] p_adv : Data to be used in advertisement packets
 *          [in] p_rsp : Data to be used in scan response packets.
 * @return  SUCCESS             Successfully set advertising data.
 *          ERR_INVALID_param   Invalid parameter(s) supplied.
 * */
OPERATE_RET core_ble_advertising_data_set(CORE_BLE_DATA_T *p_adv, CORE_BLE_DATA_T *p_scan_rsp);

/**
 * @brief   Stop advertising
 * @param   void
 * @return  SUCCESS             Successfully stopped advertising procedure.
 *          ERR_INVALID_STATE   Not in advertising state.
 * */
OPERATE_RET core_ble_advertising_stop(void);

/**
 * @brief   Updating advertising data
 * @param   [in] p_adv :        Data to be used in advertisement packets
 *          [in] p_rsp :        Data to be used in scan response packets.
 * @return  SUCCESS             Successfully update advertising data.
 *          ERR_INVALID_STATE   Invalid parameter(s) supplied.
 * @Note:   If being in advertising, we will do update adv+rsp data.
 *          If not being in advertising, we will only do set adv+rsp data.
 * */
OPERATE_RET core_ble_advertising_data_update(CORE_BLE_DATA_T *p_adv, CORE_BLE_DATA_T *p_scan_rsp);

/**
 * @brief   Get the received signal strength for the last connection event.
 * @param   [in]peer:       conn_handle Connection handle.
 * @return  SUCCESS         Successfully read the RSSI.
 *          ERROR           No sample is available.
 *          The rssi reading is given through CORE_BLE_EVT_CONN_RSSI event !!!!!!
 * */
OPERATE_RET core_ble_rssi_get(const CORE_BLE_PEER_INFO_T peer);

/**
 * @brief   Update connection parameters.
 * @details In the central role this will initiate a Link Layer connection
 * parameter update procedure, otherwise in the peripheral role, this will send
 * the corresponding L2CAP request and wait for the central to perform the
 * procedure..
 * @param :
 *          [in] conn_param: connect parameters including interval, peer info
 * @return  SUCCESS         Successfully initiated connecting procedure.
 *
 *          The connection parameters updating is given through
 * CORE_BLE_EVT_CONN_PARAM_UPDATE event !!!!!!
 */
OPERATE_RET
core_ble_conn_param_update(const CORE_BLE_PEER_INFO_T peer, CORE_BLE_CONN_PARAMS_T const *p_conn_params);

/**
 * @brief   Disconnect from peer
 * @param   [in] conn_handle: the connection handle
 * @return  SUCCESS         Successfully disconnected.
 *          ERROR STATE     Not in connnection.
 *          ERROR PARAMETER
 * @note    Both of ble central and ble peripheral can call this function.
 *              For ble peripheral, we only need to fill connect handle.
 *               For ble central, we need to fill connect handle and peer addr.
 * */
OPERATE_RET core_ble_disconnect(const CORE_BLE_PEER_INFO_T peer);

/**
 * @brief   [Ble Peripheral] Notify Command characteristic value
 * @param   [in] p_data: pointer to data, refer to CORE_BLE_DATA_T
 * @return  SUCCESS
 *          ERROR Refer to platform error code
 * */
OPERATE_RET core_ble_server_common_send(CORE_BLE_DATA_T *p_data);

/**
 * @brief   [Ble Peripheral] Notify Command characteristic value by handle
 * @param   [in] p_data: pointer to data, refer to CORE_BLE_DATA_T
 * @return  SUCCESS
 *          ERROR Refer to platform error code
 * */
OPERATE_RET core_ble_server_common_notify(uint16_t index, CORE_BLE_DATA_T *p_data);

/**
 * @brief   [Ble Peripheral] Set Read Command characteristic value by
 * characteristic handle
 * @param   [in] index: char index
 * @param   [in] p_data: pointer to data, refer to CORE_BLE_DATA_T
 * @return  SUCCESS
 *          ERROR Refer to platform error code
 * */
OPERATE_RET core_ble_server_common_read_update_ext(uint16_t index, CORE_BLE_DATA_T *p_data);

/**
 * @brief   [Ble Peripheral] Set Read Command characteristic value
 * @param   [in] p_data: pointer to data, refer to CORE_BLE_DATA_T
 * @return  SUCCESS
 *          ERROR Refer to platform error code
 * */
OPERATE_RET core_ble_server_common_read_update(CORE_BLE_DATA_T *p_data);
/**
 * @brief   [Ble Peripheral] Reply to an ATT_MTU exchange request by sending an
 * Exchange MTU Response to the client.
 * @param   [in] att_mtu: mtu size
 * @return  SUCCESS
 *          ERROR Refer to platform error code
 *          The MTU negotiation is given through CORE_BLE_EVT_MTU_REQUEST event
 * !!!!!!
 * */
OPERATE_RET core_ble_server_exchange_mtu_reply(const CORE_BLE_PEER_INFO_T peer, uint16_t server_mtu);
#ifdef __cplusplus
}
#endif

#endif
