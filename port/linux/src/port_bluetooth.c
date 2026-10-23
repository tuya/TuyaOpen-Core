/**
 * @file port_bluetooth.c
 * @brief Linux BLE implementation (bluez): port_ble_* GAP/GATT entry points.
 *
 * Implements the port_ble_* interface declared in port_bluetooth.h. The stack
 * itself lives in bt_dbus_api.c (bluez GDBus backend: adapter/GATT application
 * objects and event callbacks) and the raw HCI advertising commands live in
 * bt_hci_adv_api.c (hci_send_req on the default controller). This file keeps
 * the GAP/GATT callback pointers, starts advertising through
 * bluez_inc_start_adv (the HCI advertising parameters and enable are applied
 * from the bluez advertisement-success callback), stops it through HCI, and
 * adds the GATT service table through bluez_inc_add_gatt after ensuring the
 * backend is initialized (bluez_inc_init is idempotent).
 *
 * Coverage: stack init, GAP advertisement, GATT service add and GATT value
 * notify are backed by the bluez/GDBus backend (stack deinit is a no-op); the
 * GAP address/connection-parameter-update/rssi entry points,
 * stack_gatt_link and gatts_value_set/exchange_mtu_reply return
 * OPRT_NOT_SUPPORTED (7 of the 18 entry points). Logging goes through printf
 * only; this layer has no core headers. The GATT client / central and scan
 * entry points were removed together with the card members: the supported role
 * is peripheral only.
 *
 * @copyright Copyright 2020-2021 Tuya Inc. All Rights Reserved.
 *
 */

#include "core_bluetooth_ops.h"
#include "port_bluetooth.h"
#include "tuya_cloud_types.h"
#include "tuya_error_code.h"
#include <stdio.h>

//#include "tuya_bluez_api.h"
/* Logging goes through printf only; this file includes no core headers
 * (see the port_bt block in port/CMakeLists.txt). */

// Forward declarations for functions defined in bt_dbus_api.c
void bluez_inc_init(CORE_BLE_GAP_EVT_FUNC_CB gap_evt_cb, CORE_BLE_GATT_EVT_FUNC_CB gatt_evt_cb);
void bluez_inc_deinit(void);
void bluez_inc_start_adv(void);
void bluez_inc_disconnect(void);
void bluez_inc_add_gatt(CORE_BLE_GATTS_PARAMS_T *p_service);
int bluez_inc_gatt_value_notify(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length);

// Forward declarations for functions defined in bt_hci_adv_api.c
int tuya_hci_le_set_adv_enable(bool enable);
int tuya_hci_le_set_adv_params(uint16_t min_interval, uint16_t max_interval, uint8_t advtype);
int tuya_hci_le_set_adv_data(uint8_t *p_data, uint8_t length);
int tuya_hci_le_set_scan_rsp_data(uint8_t *p_data, uint8_t length);

static CORE_BLE_GAP_EVT_FUNC_CB  __gap_evt_cb  = NULL;
static CORE_BLE_GATT_EVT_FUNC_CB __gatt_evt_cb = NULL;

static uint16_t tuya_conn_handle ={0};

void tuya_gap_init_callback()
{
    CORE_BLE_GAP_PARAMS_EVT_T event;

    memset(&event, 0, SIZEOF(CORE_BLE_GAP_PARAMS_EVT_T));

    event.result = 0;
    event.type = CORE_BLE_EVT_STACK_INIT;
    event.conn_handle            = tuya_conn_handle;//BLE_CONN_HANDLE;
                                                    //
    event.gap_event.connect.role = CORE_BLE_ROLE_SERVER;

    if (__gap_evt_cb) {
        __gap_evt_cb(&event);
    }
}

/**
 * @brief   Function for initializing the ble stack
 * @param   role                Indicate the role for ble stack.
 *                              role = 1: ble peripheral    @CORE_BLE_ROLE_SERVER
 *                              (peripheral only; the central/client role is unsupported)
 * @return  SUCCESS             Initialized successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_stack_init(uint8_t role)
{
    printf("port_ble_stack_init, role=%d",role);
    bluez_inc_init(__gap_evt_cb, __gatt_evt_cb);

    tuya_gap_init_callback();
    return OPRT_OK;
}

/**
 * @brief   Function for de-initializing the ble stack features
 * @param   role                 Indicate the role for ble stack.
 *                               role = 1: ble peripheral (peripheral only)
 * @return  SUCCESS             Deinitialized successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_stack_deinit(uint8_t role)
{
    printf("port_ble_stack_deinit, role=%d",role);
    bluez_inc_deinit();
    return OPRT_OK;
}

/**
 * @brief   Function for getting the GATT Link-Support.
 * @param   p_link              return gatt link                 
 * @return  SUCCESS             Support Gatt Link
 *          ERROR               Only Beacon or Mesh Beacon, Not Support Gatt Link.
 * */
OPERATE_RET port_ble_stack_gatt_link(uint16_t *p_link)
{
    printf("port_ble_stack_gatt_link");   
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief   Register GAP Event Callback
 * @param   CORE_BLE_GAP_EVT_FUNC_CB Refer to @CORE_BLE_GAP_EVT_FUNC_CB
 * @return  SUCCESS         Register successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_gap_callback_register(const CORE_BLE_GAP_EVT_FUNC_CB gap_evt)
{
    printf("port_ble_gap_callback_register\n");      
    __gap_evt_cb = gap_evt;
    return OPRT_OK;
}

/**
 * @brief   Register GATT Event Callback
 * @param   CORE_BLE_GATT_EVT_FUNC_CB Refer to @CORE_BLE_GATT_EVT_FUNC_CB
 * @return  SUCCESS         Register successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_gatt_callback_register(const CORE_BLE_GATT_EVT_FUNC_CB gatt_evt)
{
    printf("port_ble_gatt_callback_register\n");
    __gatt_evt_cb = gatt_evt;
    return OPRT_OK;
}

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
OPERATE_RET port_ble_gap_addr_set(CORE_BLE_GAP_ADDR_T const *p_peer_addr)
{
    printf("port_ble_gap_addr_set");
    return OPRT_NOT_SUPPORTED;
}
 
/**
 * @brief   Get the local Bluetooth identity address.
 * @param   [out] p_peer_addr:  pointer to local address
 * @return  SUCCESS             Set Address successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_gap_address_get(CORE_BLE_GAP_ADDR_T *p_peer_addr)
{
    printf("port_ble_gap_address_get");
    return OPRT_NOT_SUPPORTED;
}
 
/**
 * @brief   Start advertising
 * @param   [in] p_adv_params : pointer to advertising parameters 
 * @return  SUCCESS
 *  ERROR
 * */ 
static uint16_t sg_min_adv_interval, sg_max_adv_interval; 
void binc_start_adv_success_cb(void) {
    tuya_hci_le_set_adv_enable(0);
    tuya_hci_le_set_adv_params(sg_min_adv_interval, sg_max_adv_interval, 0);
    tuya_hci_le_set_adv_enable(1);
}
OPERATE_RET port_ble_gap_adv_start(CORE_BLE_GAP_ADV_PARAMS_T const *p_adv_params)
{
	printf("port_ble_gap_adv_start\n");
    printf("port_ble_gap_adv_start:interval(%d,%d),type:%d", p_adv_params->adv_interval_min, p_adv_params->adv_interval_max, p_adv_params->adv_type);
 
    // 1) bluetoothd's advertising must be started or connections fail (bluetoothd checks it on connect)
    // 2) Setting the advertising/scan-response packets over HCI does not depend on the advertising enable state, but setting the advertising interval requires stopping advertising first
    // 3) bluetoothd's adv initialization includes setting the advertising interval and is asynchronous; wait until it fully completes before the HCI adv configuration, otherwise the interval gets overwritten
    //     - a plain delay does not work in practice    // usleep(100 * 1000); // pause for 100,000 us = 100 ms
    //     - find the advertising success callback in binc/adapter.c
    sg_min_adv_interval = p_adv_params->adv_interval_min;
    sg_max_adv_interval = p_adv_params->adv_interval_max;
    bluez_inc_start_adv();

    return OPRT_OK;
}

/**
 * @brief   Stop advertising
 * @param   void
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_stop(void)
{
    printf("port_ble_gap_adv_stop\n");
    tuya_hci_le_set_adv_enable(0);
   	return OPRT_OK;
}

/**
 * @brief   Setting advertising data
 * @param   [in] p_adv:         Data to be used in advertisement packets, and include adv data len
 *          [in] p_scan_rsp:    Data to be used in advertisement respond packets, and include rsp data len
 * @Note    Please Check p_adv and p_scan_rsp, if data->p_data == NULL or data->len == 0, we will not update these values.
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_rsp_data_set(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp)
{
    printf("port_ble_gap_adv_rsp_data_set\n"); 
    tuya_hci_le_set_adv_data(p_adv->p_data, p_adv->len);
    tuya_hci_le_set_scan_rsp_data(p_scan_rsp->p_data, p_scan_rsp->len);
    return OPRT_OK;
}

/**
 * @brief   Update advertising data
 * @param   [in] p_adv: Data    to be used in advertisement packets, and include adv data len
 *          [in] p_scan_rsp:    Data to be used in advertisement respond packets, and include rsp data len
 * @Note    Please Check p_adv and p_scan_rsp, if data->p_data == NULL or data->len == 0, we will not update these values.
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_rsp_data_update(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp)
{
    printf("port_ble_gap_adv_rsp_data_update\n");
    tuya_hci_le_set_adv_data(p_adv->p_data, p_adv->len);
    tuya_hci_le_set_scan_rsp_data(p_scan_rsp->p_data, p_scan_rsp->len);
    return OPRT_OK;
}

/**
 * @brief   Disconnect from peer
 * @param   [in] conn_handle:   the connection handle
 *          [in] hci_reason:    terminate reason
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_disconnect(uint16_t conn_handle, uint8_t hci_reason)
{
    printf("port_ble_gap_disconnect");               
    bluez_inc_disconnect();
    return OPRT_OK;
}

/**
 * @brief   Start to update connection parameters
 * @param   [in] conn_handle:   connection handle
 *          [in] p_conn_params: connection  parameters
 * @return  SUCCESS
 *          ERROR
 * */
OPERATE_RET port_ble_gap_conn_param_update(uint16_t conn_handle, CORE_BLE_GAP_CONN_PARAMS_T const *p_conn_params)
{
    printf("port_ble_gap_conn_param_update");
    return OPRT_NOT_SUPPORTED;
}
 
/**
 * @brief   Get the received signal strength for the last connection event.
 * @param   [in]conn_handle:    connection handle
 * @return  SUCCESS             Successfully read the RSSI.
 *          ERROR               No sample is available.
 * */
OPERATE_RET port_ble_gap_rssi_get(uint16_t conn_handle)
{
    printf("port_ble_gap_rssi_get");
    return OPRT_NOT_SUPPORTED;
}
 
/**
 * @brief   Add Ble Gatt Service
 * @param   [in] p_service: define the ble service
 *
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gatts_service_add(CORE_BLE_GATTS_PARAMS_T *p_service)
{
    // Some WiFi+BLE applications call this before the initialization call,
    // which makes it fail; the stack init is also called here (that function guards against double initialization)
    bluez_inc_init(__gap_evt_cb, __gatt_evt_cb);

#if 0  /* LOG: service/characteristic dump (off by default); set to 1 to inspect the GATT table */
    printf("port_ble_gatts_service_add\n");
    printf("svc_num=%d\n", p_service->svc_num);

    // Iterate over every service
    for (int i = 0; i < p_service->svc_num; i++) {
        CORE_BLE_SERVICE_PARAMS_T *p_svc = &(p_service->p_service[i]);

        printf("-------------------- Service[%d] --------------------\n", i);
        printf("handle = 0x%x\n", p_svc->handle);

        // Print the service UUID according to its type
        switch (p_svc->svc_uuid.uuid_type) {
            case CORE_BLE_UUID_TYPE_16:
                printf("svc_uuid = 0x%04x (16-bit)\n", p_svc->svc_uuid.uuid.uuid16);
                break;
            case CORE_BLE_UUID_TYPE_32:
                printf("svc_uuid = 0x%08x (32-bit)\n", p_svc->svc_uuid.uuid.uuid32);
                break;
            case CORE_BLE_UUID_TYPE_128:
                printf("svc_uuid = %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x (128-bit LE)\n",
                            p_svc->svc_uuid.uuid.uuid128[15], p_svc->svc_uuid.uuid.uuid128[14], p_svc->svc_uuid.uuid.uuid128[13], p_svc->svc_uuid.uuid.uuid128[12],
                            p_svc->svc_uuid.uuid.uuid128[11], p_svc->svc_uuid.uuid.uuid128[10],
                            p_svc->svc_uuid.uuid.uuid128[9], p_svc->svc_uuid.uuid.uuid128[8],
                            p_svc->svc_uuid.uuid.uuid128[7], p_svc->svc_uuid.uuid.uuid128[6],
                            p_svc->svc_uuid.uuid.uuid128[5], p_svc->svc_uuid.uuid.uuid128[4], p_svc->svc_uuid.uuid.uuid128[3], p_svc->svc_uuid.uuid.uuid128[2], p_svc->svc_uuid.uuid.uuid128[1], p_svc->svc_uuid.uuid.uuid128[0]);
                break;
            default:
                printf("svc_uuid = Unknown UUID Type\n");
                break;
        }

        printf("type = 0x%x\n", p_svc->type);
        printf("char_num = %d\n", p_svc->char_num);

        // Iterate over every characteristic of the current service
        for (int j = 0; j < p_svc->char_num; j++) {
            CORE_BLE_CHAR_PARAMS_T *p_char = &(p_svc->p_char[j]);
            
            printf("    -------------------- Char[%d] --------------------\n", j);
            printf("    handle = 0x%x\n", p_char->handle);
            
            // Print the characteristic UUID according to its type
            switch (p_char->char_uuid.uuid_type) {
                case CORE_BLE_UUID_TYPE_16:
                    printf("    char_uuid = 0x%04x (16-bit)\n", p_char->char_uuid.uuid.uuid16);
                    break;
                case CORE_BLE_UUID_TYPE_32:
                    printf("    char_uuid = 0x%08x (32-bit)", p_char->char_uuid.uuid.uuid32);
                    break;
                case CORE_BLE_UUID_TYPE_128:
                    printf("    char_uuid = %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x (128-bit LE)\n",
                                p_char->char_uuid.uuid.uuid128[15], p_char->char_uuid.uuid.uuid128[14], p_char->char_uuid.uuid.uuid128[13], p_char->char_uuid.uuid.uuid128[12],
                                p_char->char_uuid.uuid.uuid128[11], p_char->char_uuid.uuid.uuid128[10],
                                p_char->char_uuid.uuid.uuid128[9], p_char->char_uuid.uuid.uuid128[8],
                                p_char->char_uuid.uuid.uuid128[7], p_char->char_uuid.uuid.uuid128[6],
                                p_char->char_uuid.uuid.uuid128[5], p_char->char_uuid.uuid.uuid128[4], p_char->char_uuid.uuid.uuid128[3], p_char->char_uuid.uuid.uuid128[2], p_char->char_uuid.uuid.uuid128[1], p_char->char_uuid.uuid.uuid128[0]);
                    break;
                default:
                    printf("    char_uuid = Unknown UUID Type\n");
                    break;
            }
            printf("    property = 0x%x\n", p_char->property);
            printf("    permission = 0x%x\n", p_char->permission);
            printf("    value_len = %d\n", p_char->value_len);
        }
    }
#endif
    bluez_inc_add_gatt(p_service);
    return OPRT_OK;
}
 
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
OPERATE_RET port_ble_gatts_value_set(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    printf("port_ble_gatts_value_set");
    return OPRT_NOT_SUPPORTED;
}
 
/**
 * @brief   Notify an attribute value.
 * @param   [in] conn_handle    Connection handle.
 * @param   [in] char_handle    Attribute handle.
 *          [in] p_data         Notify Values
 *          [in] length         Value Length
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gatts_value_notify(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    printf("port_ble_gatts_value_notify");
    return bluez_inc_gatt_value_notify(conn_handle, char_handle, p_data, length);
}
 
/**
 * @brief   Reply to an ATT_MTU exchange request by sending an Exchange MTU Response to the client.
 * @param   [in] conn_handle    Connection handle.
 *          [in] server_rx_mtu  mtu size.
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gatts_exchange_mtu_reply(uint16_t conn_handle, uint16_t server_rx_mtu)
{
    printf("port_ble_gatts_exchange_mtu_reply"); 
    return OPRT_NOT_SUPPORTED;
}

/* ---------------------------------------------------------------------------
 * Platform ops card: core's core_bluetooth.c references it via extern and
 * dispatches core_ble_*. The members below are the binding to port_ble_*.
 * --------------------------------------------------------------------------- */
CORE_BLUETOOTH_OPS_T core_bluetooth_ops_platform = {
    /* stack */
    .stack_init              = port_ble_stack_init,
    .stack_deinit            = port_ble_stack_deinit,
    .stack_gatt_link         = port_ble_stack_gatt_link,
    /* Event registration (must precede stack_init, see the file header) */
    .gap_callback_register   = port_ble_gap_callback_register,
    .gatt_callback_register  = port_ble_gatt_callback_register,
    /* GAP */
    .gap_addr_set            = port_ble_gap_addr_set,
    .gap_address_get         = port_ble_gap_address_get,
    .gap_adv_start           = port_ble_gap_adv_start,
    .gap_adv_stop            = port_ble_gap_adv_stop,
    .gap_adv_rsp_data_set    = port_ble_gap_adv_rsp_data_set,
    .gap_adv_rsp_data_update = port_ble_gap_adv_rsp_data_update,
    .gap_disconnect          = port_ble_gap_disconnect,
    .gap_conn_param_update   = port_ble_gap_conn_param_update,
    .gap_rssi_get            = port_ble_gap_rssi_get,
    /* GATT server */
    .gatts_service_add        = port_ble_gatts_service_add,
    .gatts_value_set          = port_ble_gatts_value_set,
    .gatts_value_notify       = port_ble_gatts_value_notify,
    .gatts_exchange_mtu_reply = port_ble_gatts_exchange_mtu_reply,
};
