/**
 * @file core_bluetooth_ops.h
 * @brief Contract header for the CORE BLE ops card: CORE_BLUETOOTH_OPS_T and the
 *        CORE_BLE_* type set used by its member signatures.
 *
 * A types-only contract between core and port. The port assembles
 * core_bluetooth_ops_platform from CORE_BLUETOOTH_OPS_T, and core's
 * iot-core/ble/core_bluetooth.c dispatches all BLE operations through it. The
 * card has 18 members covering stack control, GAP and GATT server (the GATT
 * client / central and scan members were removed: the supported role is
 * peripheral only, which neither scans nor initiates connections); member signatures use only the CORE_BLE_* types defined here and in
 * core_bluetooth_def.h.
 *
 * Conventions:
 * - the port adapter (linux: port/src/port_bluetooth.c, card section) wires the
 *   members to its implementation by byte-identical layout plus pointer casts,
 *   backed by compile-time sizeof/offsetof assertions (a drifting layout fails
 *   the build);
 * - callback registration (gap_callback_register / gatt_callback_register)
 *   must precede stack_init and gatts_service_add, because the bluez layer
 *   latches the callback pointers on first use;
 * - gatts_service_add takes a non-const argument (the port writes back the
 *   characteristic handles);
 * - a platform without BLE leaves the card members NULL and
 *   core_bluetooth.c degrades gracefully (dispatch returns OPRT_NOT_SUPPORTED).
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __CORE_BLUETOOTH_OPS_H__
#define __CORE_BLUETOOTH_OPS_H__

#include "core_bluetooth_def.h"   /* public type set (reused for CORE_BLE_DATA_T etc.) */

#ifndef __CORE_BLUETOOTH_OPS_TYPES_H__
#define __CORE_BLUETOOTH_OPS_TYPES_H__

#include "tuya_cloud_types.h"
#include "tuya_error_code.h"

/* Note: this type set carries the CORE_BLE_* prefix so card member signatures
 * use CORE types only; the port adapter wires it to the platform BLE types by
 * byte-identical layout plus a cast (see the layout assertion convention in
 * the file header). Where this set overlaps the public set
 * (core_bluetooth_def.h), the names are chosen as follows: DATA_T reuses the
 * public CORE_BLE_DATA_T; the report/subscribe/read event types use a GATT_
 * prefix; the ADV report enumerators use a GAP_ prefix; scan parameters use a
 * bitfield instead of SCAN_TYPE macros. */

#define CORE_BLE_ROLE_SERVER                                                 (0x01)  /**< Gatt Server Role, for ble peripheral.*/
#define CORE_BLE_ROLE_CLIENT                                                 (0x02)  /**< Gatt Client Role, for ble central. */

#define CORE_BLE_GAP_ADDR_TYPE_PUBLIC                                        (0x00)  /**< Public (identity) address.*/
#define CORE_BLE_GAP_ADDR_TYPE_RANDOM                                        (0x01)  /**< Random (identity) address. */


#define CORE_BLE_GAP_ADV_SET_DATA_SIZE_MAX                                   (31)    /**< Maximum data length for an advertising set.
                                                                                        If more advertising data is required, use extended advertising instead. */
#define CORE_BLE_GAP_ADV_SET_DATA_SIZE_EXTENDED_MAX_SUPPORTED                (255)   /**< Maximum supported data length for an extended advertising set. */

/**
 * Advertising types defined in Bluetooth Core Specification v5.0, Vol 6, Part B, Section 4.4.2.
 * */
#define CORE_BLE_GAP_ADV_TYPE_CONN_SCANNABLE_UNDIRECTED                      (0x01)  /**< CONN and scannable undirected
                                                                                        advertising events. */
#define CORE_BLE_GAP_ADV_TYPE_CONN_NONSCANNABLE_DIR_HIGHDUTY_CYCLE           (0x02)  /**< CONN non-scannable directed advertising
                                                                                        events. Advertising interval is less that 3.75 ms.
                                                                                        Use this type for fast reconnections.
                                                                                        @note Advertising data is not supported. */
#define CORE_BLE_GAP_ADV_TYPE_CONN_NONSCANNABLE_DIRECTED                     (0x03)  /**< CONN non-scannable directed advertising
                                                                                        events.
                                                                                        @note Advertising data is not supported. */
#define CORE_BLE_GAP_ADV_TYPE_NONCONN_SCANNABLE_UNDIRECTED                   (0x04)  /**< Non-CONN scannable undirected
                                                                                        advertising events. */
#define CORE_BLE_GAP_ADV_TYPE_NONCONN_NONSCANNABLE_UNDIRECTED                (0x05)  /**< Non-CONN non-scannable undirected
                                                                                        advertising events. */
#define CORE_BLE_GAP_ADV_TYPE_EXTENDED_CONN_NONSCANNABLE_UNDIRECTED          (0x06)  /**< CONN non-scannable undirected advertising
                                                                                        events using extended advertising PDUs. */
#define CORE_BLE_GAP_ADV_TYPE_EXTENDED_CONN_NONSCANNABLE_DIRECTED            (0x07)  /**< CONN non-scannable directed advertising
                                                                                        events using extended advertising PDUs. */
#define CORE_BLE_GAP_ADV_TYPE_EXTENDED_NONCONN_SCANNABLE_UNDIRECTED          (0x08)  /**< Non-CONN scannable undirected advertising
                                                                                        events using extended advertising PDUs.
                                                                                        @note Only scan response data is supported. */
#define CORE_BLE_GAP_ADV_TYPE_EXTENDED_NONCONN_SCANNABLE_DIRECTED            (0x09)  /**< Non-CONN scannable directed advertising
                                                                                        events using extended advertising PDUs.
                                                                                        @note Only scan response data is supported. */
#define CORE_BLE_GAP_ADV_TYPE_EXTENDED_NONCONN_NONSCANNABLE_UNDIRECTED       (0x0A)  /**< Non-CONN non-scannable undirected advertising
                                                                                        events using extended advertising PDUs. */
#define CORE_BLE_GAP_ADV_TYPE_EXTENDED_NONCONN_NONSCANNABLE_DIRECTED         (0x0B)  /**< Non-CONN non-scannable directed advertising
                                                                                        events using extended advertising PDUs. */

#define CORE_BLE_GAP_PHY_AUTO                                                (0x00)  /**< Automatic PHY selection.*/
#define CORE_BLE_GAP_PHY_1MBPS                                               (0x01)  /**< 1 Mbps PHY. */
#define CORE_BLE_GAP_PHY_2MBPS                                               (0x02)  /**< 2 Mbps PHY. */
#define CORE_BLE_GAP_PHY_CODED                                               (0x04)  /**< Coded PHY. */

#define CORE_BLE_GAP_ADV_STATE_IDLE                                          (0x00)  /**< Idle, no advertising */
#define CORE_BLE_GAP_ADV_STATE_START                                         (0x01)  /**< Start Advertising. A temporary state, haven't received the result.*/
#define CORE_BLE_GAP_ADV_STATE_ADVERTISING                                   (0x02)  /**< Advertising State */
#define CORE_BLE_GAP_ADV_STATE_STOP                                          (0x04)  /**< Stop Advertising. A temporary state, haven't received the result.*/

#define CORE_BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION                       (0x13)  /**< Remote User Terminated Connection. */
#define CORE_BLE_HCI_REMOTE_DEV_TERMINATION_DUE_TO_LOW_RESOURCES             (0x14)  /**< Remote Device Terminated Connection due to low resources.*/
#define CORE_BLE_HCI_REMOTE_DEV_TERMINATION_DUE_TO_POWER_OFF                 (0x15)  /**< Remote Device Terminated Connection due to power off. */
#define CORE_BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION                        (0x16)  /**< Local Host Terminated Connection. */
            
#define CORE_BLE_GATT_SERVICE_MAX_NUM                                        (6)     /**< [20220614] Increase maximum service into 6 */
#define CORE_BLE_GATT_CHAR_MAX_NUM                                           (6)

#define CORE_BLE_GATT_INVALID_HANDLE                                         (0xFFFF) /**< Invalid Connect Handle */
                    
/**@brief GAP address parameters. */
typedef struct {
    uint8_t     type;                                   /**< Mac Address Type, Refer to @ CORE_BLE_GAP_ADDR_TYPE_PUBLIC or CORE_BLE_GAP_ADDR_TYPE_RANDOM*/
    uint8_t     addr[6];                                /**< Mac Address, Address size, 6 bytes */
} CORE_BLE_GAP_ADDR_T;

 typedef enum {
    CORE_BLE_GAP_ADV_DATA,                                   /**< Adv Data - Only */
    CORE_BLE_GAP_RSP_DATA,                                   /**< Scan Response Data - Only */
    CORE_BLE_GAP_ADV_RSP_DATA,                               /**< Adv Data + Scan Response Data */
    CORE_BLE_GAP_NONCONN_ADV_DATA,                           /**< None-Connectable Adv Data - Only */
    CORE_BLE_GAP_EXTENDED_ADV_DATA,                          /**< [Bluetooth 5.0]Extended Adv Data - Only */
} CORE_BLE_GAP_ADV_TYPE_E;

/**@brief GAP advertising parameters. */
typedef struct {
    uint8_t                 adv_type;                   /**< Adv Type. Refer to CORE_BLE_GAP_ADV_TYPE_CONN_SCANNABLE_UNDIRECTED etc.*/
    CORE_BLE_GAP_ADDR_T      direct_addr;                /**< For Directed Advertising, you can fill in direct address */
    
    uint16_t                adv_interval_min;           /**< Range: 0x0020 to 0x4000  Time = N * 0.625 msec Time Range: 20 ms to 10.24 sec */
    uint16_t                adv_interval_max;           /**< Range: 0x0020 to 0x4000  Time = N * 0.625 msec Time Range: 20 ms to 10.24 sec */
    uint8_t                 adv_channel_map;            /**< Advertising Channel Map, 0x01 = adv channel index 37,  0x02 = adv channel index 38,
                                                                0x04 = adv channel index 39. Default Value: 0x07*/
} CORE_BLE_GAP_ADV_PARAMS_T;

/**@brief GAP adv report parameters. */
typedef struct {
    CORE_BLE_GAP_ADV_TYPE_E  adv_type;                   /**< Advertising report type. Refer to @CORE_BLE_GAP_ADV_TYPE_E */
    CORE_BLE_GAP_ADDR_T      peer_addr;                  /**< Bluetooth address of the peer device. */
    char                  rssi;                       /**< Received Signal Strength Indication in dBm of the last packet received. */
    uint8_t                 channel_index;              /**< Channel Index on which the last advertising packet is received (37-39).channel index = 37, it means that we do advertisement in channel 37. */
    CORE_BLE_DATA_T          data;                       /**< Received advertising or scan response data.  */
} CORE_BLE_GAP_ADV_REPORT_T;

/**@brief GAP scanning parameters. */
typedef struct {
    uint8_t                 extended;                   /**< If 1, the scanner will accept extended advertising packets.
                                                            If set to 0, the scanner will not receive advertising packets
                                                            on secondary advertising channels, and will not be able
                                                            to receive long advertising PDUs. */
    uint8_t                 active : 1;                 /**< [Tuya Need]!!!! If 1, perform active scanning by sending scan requests. */
    uint8_t                 scan_phys;                  /**< Refer to @CORE_BLE_GAP_PHY_1MBPS. CORE_BLE_GAP_PHY_2MBPS*/
    uint16_t                interval;                   /**< Scan interval in 625 us units. */
    uint16_t                window;                     /**< Scan window in 625 us units. */
    uint16_t                timeout;                    /**< Scan timeout in 10 ms units. */
    uint8_t                 scan_channel_map;           /**< Scan Channel Index, refer to @CORE_BLE_GAP_ADV_PARAMS_T*/
} CORE_BLE_GAP_SCAN_PARAMS_T;

/** @brief  Definition of LE connection request parameter.*/
typedef struct {
    uint16_t                conn_interval_min;          /**< Minimum value for the connection interval.  */
    uint16_t                conn_interval_max;          /**< Maximum value for the connection interval. */
    uint16_t                conn_latency;               /**< Slave latency for the connection in number of connection events.*/
    uint16_t                conn_sup_timeout;           /**< Supervision timeout for the LE Link.*/
    
    uint16_t                connection_timeout;
} CORE_BLE_GAP_CONN_PARAMS_T;

typedef enum {
    CORE_BLE_UUID_TYPE_16,                               /**< UUID 16 bit */
    CORE_BLE_UUID_TYPE_32,                               /**< UUID 32 bit */
    CORE_BLE_UUID_TYPE_128,                              /**< UUID 128 bit */
} CORE_BLE_UUID_TYPE_E;

/** @brief  Bluetooth Low Energy UUID type, encapsulates both 16-bit and 128-bit UUIDs. */
typedef struct {
    CORE_BLE_UUID_TYPE_E     uuid_type;                  /**< UUID Type, Refer to @CORE_BLE_UUID_TYPE_E */

    union {
        uint16_t            uuid16;                     /**< 16-bit UUID value  */
        uint32_t              uuid32;                     /**< 32-bit UUID value */
        uint8_t             uuid128[16];                /**< Little-Endian UUID bytes. 128bit uuid*/
    }uuid;
} CORE_BLE_UUID_T;

/**< GATT characteristic property bit field values */
typedef enum {
    CORE_BLE_GATT_CHAR_PROP_BROADCAST            = 0x01, /**< If set, permits broadcasts of the Characteristic Value using Server Characteristic Configuration Descriptor. */
    CORE_BLE_GATT_CHAR_PROP_READ                 = 0x02, /**< If set, permits reads of the Characteristic Value */
    CORE_BLE_GATT_CHAR_PROP_WRITE_NO_RSP         = 0x04, /**< If set, permit writes of the Characteristic Value without response */
    CORE_BLE_GATT_CHAR_PROP_WRITE                = 0x08, /**< If set, permits writes of the Characteristic Value with response */
    CORE_BLE_GATT_CHAR_PROP_NOTIFY               = 0x10, /**< If set, permits notifications of a Characteristic Value without acknowledgment */
    CORE_BLE_GATT_CHAR_PROP_INDICATE             = 0x20, /**< If set, permits indications of a Characteristic Value with acknowledgment */
    CORE_BLE_GATT_CHAR_PROP_WRITE_AUTHEN_SIGNED  = 0x40, /**< If set, permits signed writes to the Characteristic Value */
    CORE_BLE_GATT_CHAR_PROP_EXT_PROP             = 0x80, /**< If set, additional characteristic properties are defined in the Characteristic */
}CORE_BLE_CHAR_PROP_TYPE_E;

/**< GATT attribute permission bit field values */
typedef enum {
    CORE_BLE_GATT_PERM_NONE                      = 0x01, /**< No operations supported, e.g. for notify-only */
    CORE_BLE_GATT_PERM_READ                      = 0x02, /**< Attribute read permission. */
    CORE_BLE_GATT_PERM_WRITE                     = 0x04, /**< Attribute write permission. */
    CORE_BLE_GATT_PERM_READ_ENCRYPT              = 0x08, /**< Attribute read permission with encryption. */
    CORE_BLE_GATT_PERM_WRITE_ENCRYPT             = 0x10, /**< Attribute write permission with encryption. */
    CORE_BLE_GATT_PERM_READ_AUTHEN               = 0x20, /**< Attribute read permission with authentication. */
    CORE_BLE_GATT_PERM_WRITE_AUTHEN              = 0x40, /**< Attribute write permission with authentication. */
    CORE_BLE_GATT_PERM_PREPARE_WRITE             = 0x80, /**< Attribute prepare write permission. */
} CORE_BLE_ATTR_PERM_E;

typedef struct {
    uint16_t        handle;                             /**< [Output] After init the characteristic, we will get the char-handle, we need to restore it */

    CORE_BLE_UUID_T  char_uuid;                          /**< Characteristics UUID */
    uint8_t         property;                           /**< Characteristics property , Refer to CORE_BLE_CHAR_PROP_TYPE_E */
    uint8_t         permission;                         /**< Characteristics value attribute permission */
    uint8_t         value_len;                          /**< Characteristics value length */
} CORE_BLE_CHAR_PARAMS_T;

typedef enum {
    CORE_BLE_UUID_UNKNOWN                    = 0x0000,   /**< Reserved UUID. */
    CORE_BLE_UUID_SERVICE_PRIMARY            = 0x2800,   /**< Primary Service. */
    CORE_BLE_UUID_SERVICE_SECONDARY          = 0x2801,   /**< Secondary Service. */
    CORE_BLE_UUID_SERVICE_INCLUDE            = 0x2802,   /**< Include. */
    CORE_BLE_UUID_CHARACTERISTIC             = 0x2803,   /**< Characteristic. */
} CORE_BLE_SERVICE_TYPE_E;

typedef struct {
    uint16_t                    handle;                 /**< After init the service, we will get the svc-handle */

    CORE_BLE_UUID_T              svc_uuid;               /**< Service UUID */
    CORE_BLE_SERVICE_TYPE_E      type;                   /**< Service Type */
    
    uint8_t                     char_num;               /**< Number of characteristic */
    CORE_BLE_CHAR_PARAMS_T       *p_char;                /**< Pointer of characteristic */
} CORE_BLE_SERVICE_PARAMS_T;

typedef struct {
    uint8_t                     svc_num;                /**< If we only use service(0xFD50), the svc_num will be set into 1 */
    CORE_BLE_SERVICE_PARAMS_T    *p_service;
} CORE_BLE_GATTS_PARAMS_T;

typedef enum {
    CORE_BLE_EVT_STACK_INIT = 0x01,                      /**< Init Bluetooth Stack, return  refer@CORE_BLE_GAP_PARAMS_EVT_T.result */
    
    CORE_BLE_EVT_STACK_DEINIT,                           /**< Deinit Bluetooth Stack, Requirements from Tuyaos ZT*/

    CORE_BLE_EVT_STACK_RESET,                            /**< Reset Bluetooth Stack, Requirements from Bluetooth Gateway */

    CORE_BLE_GAP_EVT_CONNECT,                            /**< Connected as peripheral role */

    CORE_BLE_GAP_EVT_DISCONNECT,                         /**< Disconnected */

    CORE_BLE_GAP_EVT_ADV_STATE,                          /**< Advertising State */

    CORE_BLE_GAP_EVT_ADV_REPORT,                         /**< Scan result report */

    CORE_BLE_GAP_EVT_CONN_PARAM_REQ,                     /**< Parameter update request */

    CORE_BLE_GAP_EVT_CONN_PARAM_UPDATE,                  /**< Parameter update successfully */
    
    CORE_BLE_GAP_EVT_CONN_RSSI,                          /**< Got RSSI value of link peer device */
} CORE_BLE_GAP_EVT_TYPE_E;

typedef enum {      
    CORE_BLE_GATT_EVT_MTU_REQUEST = 0x01 ,               /**< MTU exchange request event, For Ble peripheral, we need to do reply*/

    CORE_BLE_GATT_EVT_MTU_RSP,                           /**< MTU exchange respond event, For Ble Central, the ble central has finished the MTU-Request */

    CORE_BLE_GATT_EVT_PRIM_SEV_DISCOVERY,                /**< [Ble Central] Discovery Service */

    CORE_BLE_GATT_EVT_CHAR_DISCOVERY,                    /**< [Ble Central] Discovery Characteristics*/

    CORE_BLE_GATT_EVT_CHAR_DESC_DISCOVERY,               /**< [Ble Central] Discovery descriptors */

    CORE_BLE_GATT_EVT_NOTIFY_TX,                         /**< [Ble peripheral] Transfer data Callback, only report Result */

    CORE_BLE_GATT_EVT_WRITE_REQ,                         /**< [Ble Peripheral] Get Client-Write Char Request*/

    CORE_BLE_GATT_EVT_NOTIFY_INDICATE_RX,                /**< [Ble Central] Get Notification or Indification data */

    CORE_BLE_GATT_EVT_READ_RX,                           /**< [Ble Central] Get Char-Read Data */

    CORE_BLE_GATT_EVT_SUBSCRIBE,                         /**< [Ble Peripheral] Subscribe Request*/

    CORE_BLE_GATT_EVT_READ_CHAR_VALUE,                   /**< [Ble Peripheral] read char value event*/

} CORE_BLE_GATT_EVT_TYPE_E;

typedef struct {
    uint8_t                         role;               /**< BLE role for this connection, see @ref CORE_BLE_ROLE_SERVER, or CORE_BLE_ROLE_CLIENT */
    CORE_BLE_GAP_ADDR_T              peer_addr;          /**< Reserved, [Ble Central],For some platform, we will get the peer address after connect one device */
    CORE_BLE_GAP_CONN_PARAMS_T       conn_params;        /**< Report Connection Parameters */
} CORE_BLE_GAP_CONNECT_EVT_T;

typedef struct {
    uint8_t                         role;               /**< BLE role for this disconnection */
    int                           reason;             /**< Report Disconnection Reason */
} CORE_BLE_GAP_DISCONNECT_EVT_T;

typedef struct {
    uint16_t                        char_handle;        /**< Notify Characteristic Handle */
    int                           result;             /**< Notify Result */
} CORE_BLE_NOTIFY_RESULT_EVT_T;

typedef struct {
    CORE_BLE_UUID_T              uuid;               /**< Discovery Service UUID */
    uint16_t                    start_handle;       /**< Discovery Start Handle */
    uint16_t                    end_handle;         /**< Discovery End Handle */
} CORE_BLE_GATT_SVC_HANDLE_T;

typedef struct {
    uint8_t                         svc_num;            /**< Number of Services */

    CORE_BLE_GATT_SVC_HANDLE_T       services[CORE_BLE_GATT_SERVICE_MAX_NUM];
} CORE_BLE_GATT_SVC_DISC_TYPE_T;

typedef struct {
    CORE_BLE_UUID_T              uuid;               /**< Discovery Service UUID */
    uint16_t                    handle;             /**< Discovery Char Handle */
}CORE_BLE_GATT_CHAR_HANDLE_T;

typedef struct {
    uint8_t                         char_num;           /**< Number of Characteristics */

    CORE_BLE_GATT_CHAR_HANDLE_T      characteristics[CORE_BLE_GATT_CHAR_MAX_NUM];
} CORE_BLE_GATT_CHAR_DISC_TYPE_T;

typedef struct {
    uint16_t                        cccd_handle;        /**< Discovery Descriptor Handle, Return CCCD Handle */
} CORE_BLE_GATT_DESC_DISC_TYPE_T;

typedef struct {
    uint16_t                        char_handle;        /**< Specify one characteristic handle */
    CORE_BLE_DATA_T                  report;             /**< Report Data, Refer to @ CORE_BLE_DATA_T */
} CORE_BLE_GATT_DATA_REPORT_T;

typedef struct {
    CORE_BLE_GAP_EVT_TYPE_E              type;           /**< Gap Event */
    uint16_t                            conn_handle;    /**< Connection Handle */
    int                               result;         /**< Will Refer to HOST STACK Error Code */

    union {
        CORE_BLE_GAP_CONNECT_EVT_T       connect;        /**< Receive connect callback, This value can be used with CORE_BLE_GAP_EVT_CONNECT and CORE_BLE_GAP_EVT_CONNECT*/
        CORE_BLE_GAP_DISCONNECT_EVT_T    disconnect;     /**< Receive disconnect callback*/
        CORE_BLE_GAP_ADV_REPORT_T        adv_report;     /**< Receive Adv and Respond report*/
        CORE_BLE_GAP_CONN_PARAMS_T       conn_param;     /**< We will update connect parameters.This value can be used with CORE_BLE_GAP_EVT_CONN_PARAM_REQ and CORE_BLE_GAP_EVT_CONN_PARAM_UPDATE*/
        char                          link_rssi;      /**< Peer device RSSI value */
    }gap_event;
} CORE_BLE_GAP_PARAMS_EVT_T;
typedef struct {
    uint16_t                        char_handle;                /**< Specify one characteristic handle */
    uint8_t                         reason;
    uint8_t                         prev_notify     :    1;     /**< previously subscribed */
    uint8_t                         cur_notify      :    1;     /** currently notifications. */
    uint8_t                         prev_indicate   :    1;     /** previously indications. */
    uint8_t                         cur_indicate    :    1;     /** currently subscribed to indications. */
} CORE_BLE_GATT_SUBSCRIBE_EVT_T;
typedef struct {
    uint16_t                        char_handle;                /**< Specify one characteristic handle */
    uint16_t                        offset;
} CORE_BLE_GATT_READ_CHAR_EVT_T;

typedef struct {
    CORE_BLE_GATT_EVT_TYPE_E             type;           /**< Gatt Event */
    uint16_t                            conn_handle;    /**< Connection Handle */
    int                               result;         /**< Will Refer to HOST STACK Error Code */

    union {
        uint16_t                        exchange_mtu;   /**< This value can be used with CORE_BLE_GATT_EVT_MTU_REQUEST and CORE_BLE_GATT_EVT_MTU_RSP*/
        CORE_BLE_GATT_SVC_DISC_TYPE_T    svc_disc;       /**< Discovery All Service */
        CORE_BLE_GATT_CHAR_DISC_TYPE_T   char_disc;      /**< Discovery Specific Characteristic */
        CORE_BLE_GATT_DESC_DISC_TYPE_T   desc_disc;      /**< Discovery Specific Descriptors*/
        CORE_BLE_NOTIFY_RESULT_EVT_T     notify_result;  /**< This value can be used with CORE_BLE_GATT_EVT_NOTIFY_TX*/
        CORE_BLE_GATT_DATA_REPORT_T           write_report;   /**< This value can be used with CORE_BLE_GATT_EVT_WRITE_REQ*/
        CORE_BLE_GATT_DATA_REPORT_T           data_report;    /**< This value can be used with CORE_BLE_GATT_EVT_NOTIFY_INDICATE_RX*/
        CORE_BLE_GATT_DATA_REPORT_T           data_read;      /**< After we do read attr in central mode, we will get the callback*/
        CORE_BLE_GATT_SUBSCRIBE_EVT_T          subscribe;      /**< ccc callback event, used with CORE_BLE_GATT_EVT_SUBSCRIBE*/
        CORE_BLE_GATT_READ_CHAR_EVT_T         char_read;      /**< read char event, used with CORE_BLE_GATT_EVT_READ_CHAR_VALUE*/
    }gatt_event;
} CORE_BLE_GATT_PARAMS_EVT_T;

/**< GAP Callback Register function definition */
typedef void(*CORE_BLE_GAP_EVT_FUNC_CB)(CORE_BLE_GAP_PARAMS_EVT_T *p_event);

/**< GATT Callback Register function definition */
typedef void(*CORE_BLE_GATT_EVT_FUNC_CB)(CORE_BLE_GATT_PARAMS_EVT_T *p_event);

#endif


/* ---------------------------------------------------------------------------
 * BLE platform ops card: assembled by the port (platforms without BLE leave
 * all members NULL). Member signatures use the types defined above; the card
 * has one member per interface in the port's port_bluetooth.h.
 * --------------------------------------------------------------------------- */
typedef struct {
    /* stack */
    OPERATE_RET (*stack_init)(uint8_t role);
    OPERATE_RET (*stack_deinit)(uint8_t role);
    OPERATE_RET (*stack_gatt_link)(uint16_t *p_link);
    /* Event registration (callback arguments use the event types above; must precede stack_init) */
    OPERATE_RET (*gap_callback_register)(const CORE_BLE_GAP_EVT_FUNC_CB gap_evt);
    OPERATE_RET (*gatt_callback_register)(const CORE_BLE_GATT_EVT_FUNC_CB gatt_evt);
    /* GAP */
    OPERATE_RET (*gap_addr_set)(CORE_BLE_GAP_ADDR_T const *p_peer_addr);
    OPERATE_RET (*gap_address_get)(CORE_BLE_GAP_ADDR_T *p_peer_addr);
    OPERATE_RET (*gap_adv_start)(CORE_BLE_GAP_ADV_PARAMS_T const *p_adv_params);
    OPERATE_RET (*gap_adv_stop)(void);
    OPERATE_RET (*gap_adv_rsp_data_set)(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp);
    OPERATE_RET (*gap_adv_rsp_data_update)(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp);
    OPERATE_RET (*gap_disconnect)(uint16_t conn_handle, uint8_t hci_reason);
    OPERATE_RET (*gap_conn_param_update)(uint16_t conn_handle, CORE_BLE_GAP_CONN_PARAMS_T const *p_conn_params);
    OPERATE_RET (*gap_rssi_get)(uint16_t conn_handle);
    /* GATT server */
    OPERATE_RET (*gatts_service_add)(CORE_BLE_GATTS_PARAMS_T *p_service); /* non-const: port writes back handles */
    OPERATE_RET (*gatts_value_set)(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length);
    OPERATE_RET (*gatts_value_notify)(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length);
    OPERATE_RET (*gatts_exchange_mtu_reply)(uint16_t conn_handle, uint16_t server_rx_mtu);
} CORE_BLUETOOTH_OPS_T;

/**
 * @brief Get the platform ops of core_bluetooth
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
CORE_BLUETOOTH_OPS_T *core_bluetooth_get_ops(void);

#endif /* __CORE_BLUETOOTH_OPS_H__ */
