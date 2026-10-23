/**
 * @file core_bluetooth_def.h
 * @brief CORE BLE public type set: device addresses, advertising / scan /
 *        connection parameters, GAP and GATT event structures and callbacks,
 *        and the Tuya BLE service / characteristic UUIDs and index macros.
 *
 * A types-only contract header: core's public API iot-core/ble/core_bluetooth.h
 * and the ble/ implementation include it, and the card contract
 * core_bluetooth_ops.h in the same directory reuses CORE_BLE_DATA_T and the
 * related structures. Neither core nor the port defines anything here.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __CORE_BLUETOOTH_DEF_H__
#define __CORE_BLUETOOTH_DEF_H__

#include "tuya_cloud_types.h"
#include "tuya_error_code.h"

/**< Tuya Needed Definitions And Macro */
/* Tuya Ble Command Service UUID First-Version */
#define CORE_BLE_CMD_SERVICE_UUID_V1 (0x1910)
/* Tuya Ble Write Characteristic UUID First-Version*/
#define CORE_BLE_CMD_WRITE_CHAR_UUID_V1 (0x2B11)
/* Tuya Ble Notify Characteristic UUID First-Version*/
#define CORE_BLE_CMD_NOTIFY_CHAR_UUID_V1 (0x2B10)

/* Tuya Ble Bulk Service UUID First-Version */
#define CORE_BLE_BULK_SERVICE_UUID_V1 (0x1920)
/* Tuya Ble Write Characteristic UUID First-Version*/
#define CORE_BLE_BULK_WRITE_CHAR_UUID_V1 (0x2B23)
/* Tuya Ble Notify Characteristic UUID First-Version*/
#define CORE_BLE_BULK_NOTIFY_CHAR_UUID_V1 (0x2B24)

/* Tuya Ble Command Service UUID Second-Version */
#define CORE_BLE_CMD_SERVICE_UUID_V2 (0xFD50)
/* Tuya Ble Write Characteristic UUID Second-Version*/
#define CORE_BLE_CMD_WRITE_CHAR_UUID_V2 (0x0001)
#define CORE_BLE_CMD_WRITE_CHAR_UUID128_V2                                                                              \
    {                                                                                                                  \
        0xD0, 0x07, 0x9B, 0x5F, 0x80, 0x00, 0x01, 0x80, 0x01, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00                 \
    }
/* Tuya Ble Notify Characteristic UUID Second-Version*/
#define CORE_BLE_CMD_NOTIFY_CHAR_UUID_V2 (0x0002)
#define CORE_BLE_CMD_NOTIFY_CHAR_UUID128_V2                                                                             \
    {                                                                                                                  \
        0xD0, 0x07, 0x9B, 0x5F, 0x80, 0x00, 0x01, 0x80, 0x01, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00                 \
    }
/* Tuya Ble Read Characteristic UUID Second-Version*/
#define CORE_BLE_CMD_READ_CHAR_UUID_V2 (0x0003)
#define CORE_BLE_CMD_READ_CHAR_UUID128_V2                                                                               \
    {                                                                                                                  \
        0xD0, 0x07, 0x9B, 0x5F, 0x80, 0x00, 0x01, 0x80, 0x01, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00                 \
    }

/* Tuya Ble Service UUID Scan UUID */
#define CORE_BLE_SVC_SCAN_UUID_V1 (0xA201)
#define CORE_BLE_SVC_SCAN_UUID_V2 (0xA300)
#define CORE_BLE_SVC_SCAN_UUID_V3 (0xFD50)

/* Tuya Ble Service Characteristic Index */
#define CORE_COMMON_WRITE_CHAR_INDEX  (0)
#define CORE_COMMON_NOTIFY_CHAR_INDEX (1)
#define CORE_COMMON_READ_CHAR_INDEX   (2)

/* Turn on tuya private ble char for iOS execution matter provisioning*/
#ifndef ENABLE_MATTER_TUYA_BLE_CHAR
#define ENABLE_MATTER_TUYA_BLE_CHAR (0)
#endif
#if (defined(ENABLE_MATTER_TUYA_BLE_CHAR) && (ENABLE_MATTER_TUYA_BLE_CHAR == 1))
#define CORE_MATTER_TUYA_T1_CHAR_INDEX (3)
#define CORE_MATTER_TUYA_T2_CHAR_INDEX (4)
#define CORE_MATTER_TUYA_T3_CHAR_INDEX (5)
#endif

#define DEFAULT_ADV_PARAMS(min, max)                                                                                   \
    {                                                                                                                  \
        .adv_interval_min = min, .adv_interval_max = max, .adv_type = CORE_BLE_ADV_TYPE_CS_UNDIR,                       \
    }
#define TUYAOS_BLE_DEFAULT_ADV_PARAM ((CORE_BLE_ADV_PARAMS_T *)(&(CORE_BLE_ADV_PARAMS_T)DEFAULT_ADV_PARAMS(30, 60)))

#define DEFAULT_SCAN_PARAMS(interval, window)                                                                          \
    {                                                                                                                  \
        .type = CORE_BLE_SCAN_TYPE_ACTIVE, .scan_interval = interval, .scan_window = window, .timeout = 0x0000,         \
        .filter_dup = 1,                                                                                               \
    }
#define TUYAOS_BLE_DEFAULT_SCAN_PARAM ((CORE_BLE_SCAN_PARAMS_T *)(&(CORE_BLE_SCAN_PARAMS_T)DEFAULT_SCAN_PARAMS(30, 30)))

#define DEFAULT_CONN_PARAMS(min, max)                                                                                  \
    {                                                                                                                  \
        .min_conn_interval = min, .max_conn_interval = max, .latency = 0, .conn_sup_timeout = 0x100,                   \
        .connection_timeout = 5,                                                                                       \
    }
#define TUYAOS_BLE_DEFAULT_CONN_PARAM ((CORE_BLE_CONN_PARAMS_T *)(&(CORE_BLE_CONN_PARAMS_T)DEFAULT_CONN_PARAMS(30, 60)))

/**< Define these parameters for advertising */
typedef enum {
    CORE_BLE_ADDR_TYPE_PUBLIC = 0x00, /**< public address  */
    CORE_BLE_ADDR_TYPE_RANDOM = 0x01, /**< random address  */
} CORE_BLE_ADDR_TYPE_E;

typedef enum {
    CORE_BLE_ADV_DATA,     /**< Adv Data - Only */
    CORE_BLE_RSP_DATA,     /**< Scan Response Data - Only */
    CORE_BLE_ADV_RSP_DATA, /**< Adv Data + Scan Response Data */
} CORE_BLE_ADV_DATA_TYPE_E;

typedef enum {
    CORE_BLE_ADV_TYPE_CS_UNDIR =
        0x01, /**< Connectable and scannable undirected advertising events. [Tuya Default Value]*/
    CORE_BLE_ADV_TYPE_CNS_DIR_HIGH_DUTY = 0x02, /**< Reserved  */
    CORE_BLE_ADV_TYPE_CNS_DIR = 0x03,           /**< Reserved  */
    CORE_BLE_ADV_TYPE_NCS_UNDIR = 0x04,         /**< Non-connectable scannable undirected advertising events. */
    CORE_BLE_ADV_TYPE_NCNS_UNDIR = 0x05,        /**< Non-connectable non-scannable undirected advertising events. */
} CORE_BLE_ADV_TYPE_E;

typedef struct {
    CORE_BLE_ADDR_TYPE_E type; /**< Mac Address Type, Refer to @ CORE_BLE_ADDR_TYPE_E */
    uint8_t addr[6];          /**< GAP Address, Address size, 6 bytes */
} CORE_BLE_ADDR_T;

typedef struct {
    uint16_t adv_interval_min;   /**< Range: 0x0020 to 0x4000  Time = N * 0.625 msec Time Range: 20 ms to 10.24 sec */
    uint16_t adv_interval_max;   /**< Range: 0x0020 to 0x4000  Time = N * 0.625 msec Time Range: 20 ms to 10.24 sec */
    CORE_BLE_ADV_TYPE_E adv_type; /**< Adv Type, Refer to @ CORE_BLE_ADV_TYPE_E */
    CORE_BLE_ADDR_T direct_addr;  /**< For direct advertising, we need to set direct addr, otherise, fill NULL */
} CORE_BLE_ADV_PARAMS_T;

typedef struct {
    CORE_BLE_ADDR_T peer_addr;         /**< After Scan Adv, we can get peer mac and mac type */
    CORE_BLE_ADV_DATA_TYPE_E adv_type; /**< Point the advertising type, refer to @ CORE_BLE_ADV_DATA_TYPE_E */

    char rssi;        /**< After scan Adv, we can get advertising's rssi */
    uint8_t *p_data;  /**< After scan Adv, we can get advertising's data*/
    uint8_t data_len; /**< advertising data length */
} CORE_BLE_ADV_REPORT_T;

typedef struct {
    uint16_t len;    /**< Data length for p_data. */
    uint8_t *p_data; /**< Data Pointer. */
} CORE_BLE_DATA_T;

/**< Define these parameters for scanning */
typedef enum {
    CORE_BLE_SCAN_TYPE_ACTIVE,  /**< active scanning, we can scan the respond data, tuya default value */
    CORE_BLE_SCAN_TYPE_PASSIVE, /**< passive scanning */
} CORE_BLE_SCAN_TYPE_E;

typedef struct {
    CORE_BLE_SCAN_TYPE_E type;

    uint16_t scan_interval; /**< Range: 0x0004 to 0x4000 Time = N * 0.625 msec Time Range: 2.5 msec to 10.24 sec */
    uint16_t scan_window;   /**< Range: 0x0004 to 0x4000 Time = N * 0.625 msec Time Range: 2.5 msec to 10.24 seconds */
    uint16_t timeout;       /**< Scan timeout between 0x0001 and 0xFFFF in seconds, 0x0000 disables timeout. */
    uint8_t filter_dup;     /**< Duplicate filtering ENABLE or DISABLE) */
} CORE_BLE_SCAN_PARAMS_T;

/**< Define these parameters for connecting */
typedef struct {
    uint16_t conn_handle;
    uint16_t char_handle[6]; /**< Tuya Char Handle, It can reflect any characteristic, Max Characteristic refer to
                                @CORE_BLE_GATT_CHAR_MAX_NUM*/

    CORE_BLE_ADDR_T peer_addr; /**< Reserved, Only Connecetion Handle can stand for one specific device
                                  For Some Reason on Bluetooth Gateway, we need device's address.*/
} CORE_BLE_PEER_INFO_T;

typedef struct {
    CORE_BLE_PEER_INFO_T
    peer; /**< For each report, we will tell the host peer information. this info can be get after being connected*/
    CORE_BLE_DATA_T report; /**< Report Data */
} CORE_BLE_DATA_REPORT_T;

typedef struct {
    uint16_t
        min_conn_interval; /**< Range: 0x0006 to 0x0C80, Time = N * 1.25 msec, Time Range: 7.5 msec to 4 seconds. */
    uint16_t
        max_conn_interval;     /**< Range: 0x0006 to 0x0C80, Time = N * 1.25 msec, Time Range: 7.5 msec to 4 seconds. */
    uint16_t latency;          /**< Range: 0x0000 to 0x01F3 */
    uint16_t conn_sup_timeout; /**< Range: 0x000A to 0x0C80, Time = N * 10 msec, Time Range: 100 msec to 32 seconds */

    uint16_t connection_timeout; /**< Range: 0 to 5000ms [Tuya Ble Central Use]*/
} CORE_BLE_CONN_PARAMS_T;

typedef struct {
    CORE_BLE_PEER_INFO_T peer; /**< Connection handle on which the event occured.*/

    CORE_BLE_CONN_PARAMS_T
    conn_param; /**< [Optional] After connection is established, we will get connection parameters.*/
    int result; /**< Connection Result. */
} CORE_BLE_CONNECT_EVT_T;

typedef struct {
    CORE_BLE_PEER_INFO_T peer; /**< Disconnection handle on which the event occured.*/
    uint8_t reason;           /**< Disconnection Reason */
} CORE_BLE_DISCONNECT_EVT_T;

typedef struct {
    uint16_t conn_handle; /**< After ble peripheral send a notification, we will get the result and connection handle */
    uint16_t
        char_handle; /**< After ble peripheral send a notification, we will get the result and characteristic handle */

    int result; /**< Notification Result */
} CORE_BLE_NOTIFY_TX_EVT_T;

typedef struct {
    uint16_t conn_handle; /**< After try to do get_rssi, we will get connection handle */
    char rssi;            /**< After try to do get_rssi, we will get rssi */
} CORE_BLE_CONN_RSSI_EVT_T;

typedef struct {
    uint16_t conn_handle; /**< After try to do exchange mtu, we will get connection handle */
    uint16_t mtu;         /**< Get the mtu */
} CORE_BLE_EXCHANGE_MTU_EVT_T;

typedef struct {
    uint16_t conn_handle;       /**< Connection Handle */
    CORE_BLE_CONN_PARAMS_T conn; /**< Connection Parameters @ CORE_BLE_CONN_PARAMS_T  */
} CORE_BLE_CONN_PARAM_EVT_T;

/**< Define these parameters for ble event */
typedef enum {
    CORE_BLE_ROLE_PERIPERAL =
        0x0001, /**< Ble Peripheral Mode. Will Combine with Tuya Ble SDK, and do server's operations */
    CORE_BLE_ROLE_CENTRAL =
        0x0002, /**< Ble Central Mode. Will Combine with Tuya Bluetooth Gateway SDK, and do client's operations */
    CORE_BLE_ROLE_BEACON = 0x0004, /**< Ble Beacon Mode, will init with ble Peripheral */

    CORE_MESH_ROLE_ADV_PROVISIONER = 0x0100,  /**< Bluetooth Mesh Provisioner Mode, Support PB-ADV */
    CORE_MESH_ROLE_GATT_PROVISIONER = 0x0200, /**< Bluetooth Mesh Provisioner Mode, Support PB-GATT */
    CORE_MESH_ROLE_NODE = 0x0400,             /**< Bluetooth Mesh Node Mode*/
} CORE_BLE_ROLE_E;

typedef enum {
    CORE_BLE_STACK_INIT = 0x01, /**< Successfully init ble/bt stack */

    CORE_BLE_STACK_DEINIT, /**< Successfully deinit ble/bt stack */

    CORE_BLE_EVT_PERIPHERAL_CONNECT, /**< Connected as peripheral role */

    CORE_BLE_EVT_CENTRAL_CONNECT_DISCOVERY, /**< Connected as central role, include mesh gatt, And Complete the service
                                              discovery  */

    CORE_BLE_EVT_DISCONNECT, /**< Disconnected */

    CORE_BLE_EVT_ADV_REPORT, /**< Scan result report */

    CORE_BLE_EVT_CONN_PARAM_REQ, /**< Parameter update request */

    CORE_BLE_EVT_CONN_PARAM_UPDATE, /**< Parameter update successfully */

    CORE_BLE_EVT_CONN_RSSI, /**< Got RSSI value of link peer device */

    CORE_BLE_EVT_MTU_REQUEST, /**< MTU exchange request event, For Ble peripheral, we need to do reply*/

    CORE_BLE_EVT_MTU_RSP, /**< MTU exchange respond event, For Ble Central, the ble central has finished the MTU-Request
                          */

    CORE_BLE_EVT_NOTIFY_TX, /**< [Ble peripheral] Transfer data Callback, Will Report Success Or Fail */

    CORE_BLE_EVT_WRITE_REQ, /**< [Ble Peripheral] Get Client-Write Char Request */

    CORE_BLE_EVT_NOTIFY_RX, /**< [Ble Central] Get Notification data */

    CORE_BLE_EVT_READ_RX, /**< [Ble Central] Receive data from reading char */

    CORE_BLE_EVT_SUBSCRIBE, /**< [Ble Peripheral] Event Subscribe */

    CORE_BLE_EVT_READ_CHAR, /**< [Ble Peripheral] Read Char Event*/
} CORE_BLE_EVT_TYPE_E;

typedef struct {
    uint16_t conn_handle;
    uint16_t char_handle; /**< Specify one characteristic handle */
    uint8_t reason;
    uint8_t prev_notify : 1;   /**< previously subscribed */
    uint8_t cur_notify : 1;    /** currently notifications. */
    uint8_t prev_indicate : 1; /** previously indications. */
    uint8_t cur_indicate : 1;  /** currently subscribed to indications. */
} CORE_BLE_SUBSCRBE_EVT_T;

typedef struct {
    uint16_t conn_handle;
    uint16_t char_handle; /**< Specify one characteristic handle */
    uint16_t offset;      /**< Char Offset */
} CORE_BLE_READ_CHAR_EVT_T;

typedef struct {
    CORE_BLE_EVT_TYPE_E type;

    union {
        uint8_t init;                        /**< Show init states */
        CORE_BLE_CONNECT_EVT_T connect;       /**< Receive connect callback, This value can be used with
                                                CORE_BLE_EVT_PERIPHERAL_CONNECT and CORE_BLE_EVT_CENTRAL_CONNECT_DISCOVERY */
        CORE_BLE_DISCONNECT_EVT_T disconnect; /**< Receive disconnect callback */
        CORE_BLE_ADV_REPORT_T adv_report;     /**< Receive Adv and Respond report */
        CORE_BLE_CONN_PARAM_EVT_T conn_param; /**< We will update connect parameters.This value can be used with
                                                CORE_BLE_EVT_CONN_PARAM_REQ and CORE_BLE_EVT_CONN_PARAM_UPDATE */
        CORE_BLE_EXCHANGE_MTU_EVT_T exchange_mtu; /**< This value can be used with CORE_BLE_EVT_MTU_REQUEST and CORE_BLE_EVT_MTU_RSP */
        CORE_BLE_CONN_RSSI_EVT_T link_rssi;     /**< Peer device RSSI value */
        CORE_BLE_NOTIFY_TX_EVT_T notify_result; /**< [Ble Peripheral] This value can be used with CORE_BLE_EVT_NOTIFY_TX
                                                  after Ble Peripheral send a notification to peer. */
        CORE_BLE_DATA_REPORT_T write_report;    /**< This value can be used with CORE_BLE_EVT_WRITE_REQ */
        CORE_BLE_DATA_REPORT_T data_report;     /**< This value can be used with CORE_BLE_EVT_NOTIFY_RX */
        CORE_BLE_DATA_REPORT_T data_read; /**< After we do read attr in central mode, we will get the callback from bluetooth Kernel */
        CORE_BLE_SUBSCRBE_EVT_T subscribe;  /**< used with CORE_BLE_EVT_SUBSCRIBE*/
        CORE_BLE_READ_CHAR_EVT_T char_read; /**< read char event, used with CORE_BLE_EVT_READ_CHAR*/
    } ble_event;
} CORE_BLE_EVT_PARAMS_T;

/**< Define Gap Event Callback for ble peripheral and central */
typedef void (*CORE_BLE_EVT_FUNC_CB)(CORE_BLE_EVT_PARAMS_T *p_event);

#endif
