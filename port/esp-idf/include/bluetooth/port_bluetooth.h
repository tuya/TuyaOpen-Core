#ifndef __PORT_BLUETOOTH_H__
#define __PORT_BLUETOOTH_H__

#include "core_bluetooth_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Function for initializing the ble stack
 * @param   role                Indicate the role for ble stack.
 *                              role = 1: ble peripheral    @CORE_BLE_ROLE_SERVER
 *                              (peripheral only; the central/client role is unsupported)
 * @return  SUCCESS             Initialized successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_stack_init(uint8_t role);

/**
 * @brief   Function for de-initializing the ble stack features
 * @param   role                 Indicate the role for ble stack.
 *                               role = 1: ble peripheral (peripheral only)
 * @return  SUCCESS             Deinitialized successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_stack_deinit(uint8_t role);

/**
 * @brief   Function for getting the GATT Link-Support.
 * @param   p_link              return gatt link                 
 * @return  SUCCESS             Support Gatt Link
 *          ERROR               Only Beacon or Mesh Beacon, Not Support Gatt Link.
 * */
OPERATE_RET port_ble_stack_gatt_link(uint16_t *p_link);

/**
 * @brief   Register GAP Event Callback
 * @param   CORE_BLE_GAP_EVT_FUNC_CB Refer to @CORE_BLE_GAP_EVT_FUNC_CB
 * @return  SUCCESS         Register successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_gap_callback_register(const CORE_BLE_GAP_EVT_FUNC_CB gap_evt);

/**
 * @brief   Register GATT Event Callback
 * @param   CORE_BLE_GATT_EVT_FUNC_CB Refer to @CORE_BLE_GATT_EVT_FUNC_CB
 * @return  SUCCESS         Register successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_gatt_callback_register(const CORE_BLE_GATT_EVT_FUNC_CB gatt_evt);

/******************************************************************************************************************************/
/** @brief Define All GAP Interface
 */
/**
 * @brief   Set the local Bluetooth identity address.
 *          The local Bluetooth identity address is the address that identifies this device to other peers.
 *          The address type must be either @ref CORE_BLE_GAP_ADDR_TYPE_PUBLIC or @ref CORE_BLE_GAP_ADDR_TYPE_RANDOM.
 * @param   [in] p_peer_addr:   pointer to local address parameters 
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_addr_set(CORE_BLE_GAP_ADDR_T const *p_peer_addr);
 
/**
 * @brief   Get the local Bluetooth identity address.
 * @param   [out] p_peer_addr:  pointer to local address
 * @return  SUCCESS             Set Address successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_gap_address_get(CORE_BLE_GAP_ADDR_T *p_peer_addr);
 
/**
 * @brief   Start advertising
 * @param   [in] p_adv_params : pointer to advertising parameters 
 * @return  SUCCESS
 *  ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_start(CORE_BLE_GAP_ADV_PARAMS_T const *p_adv_params);

/**
 * @brief   Stop advertising
 * @param   void
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_stop(void);

/**
 * @brief   Setting advertising data
 * @param   [in] p_adv:         Data to be used in advertisement packets, and include adv data len
 *          [in] p_scan_rsp:    Data to be used in advertisement respond packets, and include rsp data len
 * @Note    Please Check p_adv and p_scan_rsp, if data->p_data == NULL or data->length == 0, we will not update these values.
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_rsp_data_set(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp);

/**
 * @brief   Update advertising data
 * @param   [in] p_adv: Data    to be used in advertisement packets, and include adv data len
 *          [in] p_scan_rsp:    Data to be used in advertisement respond packets, and include rsp data len
 * @Note    Please Check p_adv and p_scan_rsp, if data->p_data == NULL or data->length == 0, we will not update these values.
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_rsp_data_update(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp);

/**
 * @brief   Disconnect from peer
 * @param   [in] conn_handle:   the connection handle
 *          [in] hci_reason:    terminate reason
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_disconnect(uint16_t conn_handle, uint8_t hci_reason);

/**
 * @brief   Start to update connection parameters
 * @param   [in] conn_handle:   connection handle
 *          [in] p_conn_params: connection  parameters
 * @return  SUCCESS
 *          ERROR
 * */
OPERATE_RET port_ble_gap_conn_param_update(uint16_t conn_handle, CORE_BLE_GAP_CONN_PARAMS_T const *p_conn_params);

/**
 * @brief   Get the received signal strength for the last connection event.
 * @param   [in]conn_handle:    connection handle
 * @return  SUCCESS             Successfully read the RSSI.
 *          ERROR               No sample is available.
 * */
OPERATE_RET port_ble_gap_rssi_get(uint16_t conn_handle);

/**
 * @brief   Add Ble Gatt Service
 * @param   [in] p_service: define the ble service
 *
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gatts_service_add(CORE_BLE_GATTS_PARAMS_T *p_service);

/**
 * @brief   Set the value of a given attribute. After Config Tuya Read-Char, we can update read-value at any time.
 * @param   [in] conn_handle    Connection handle.
 *          [in] char_handle    Attribute handle.
 *          [in,out] p_value    Attribute value information.
 * @return  SUCCESS
 *          ERROR
 *
 * @note Values other than system attributes can be set at any time, regardless of whether any active connections exist. 
 * */ 
OPERATE_RET port_ble_gatts_value_set(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length);

/**
 * @brief   Notify an attribute value.
 * @param   [in] conn_handle    Connection handle.
 * @param   [in] char_handle    Attribute handle.
 *          [in] p_data         Notify Values
 *          [in] length         Value Length
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gatts_value_notify(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length);

/**
 * @brief   Reply to an ATT_MTU exchange request by sending an Exchange MTU Response to the client.
 * @param   [in] conn_handle    Connection handle.
 *          [in] server_rx_mtu  mtu size.
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gatts_exchange_mtu_reply(uint16_t conn_handle, uint16_t server_rx_mtu);

#ifdef __cplusplus
}
#endif

#endif

