/**
 * @file core_bluetooth.c
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

#include <string.h>
#include "core_bluetooth_def.h"
/* Platform ops card contract plus the core-owned mirror type set (CORE_BLE_*) */
#include "core_bluetooth_ops.h"
#include "core_memory.h"
#include "core_log.h"

/* ---------------------------------------------------------------------------
 * Platform ops card dispatch, structurally identical to core_kv.c / core_fs.c:
 * when the card is unavailable, log with PR_ERR and return
 * OPRT_NOT_SUPPORTED, handled by the caller (or by the degradation branches
 * in this file).
 * --------------------------------------------------------------------------- */
extern CORE_BLUETOOTH_OPS_T core_bluetooth_ops_platform;

CORE_BLUETOOTH_OPS_T *core_bluetooth_get_ops(void)
{
    return &core_bluetooth_ops_platform;
}

/* Tail dispatch: return the card call result directly */
#define CORE_BLUETOOTH_EXEC_OP(op_name, ...)                                                                            \
    do {                                                                                                               \
        CORE_BLUETOOTH_OPS_T *ops = core_bluetooth_get_ops();                                                            \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("CORE bluetooth operation %s not available", #op_name);                                                  \
        return OPRT_NOT_SUPPORTED;                                                                                     \
    } while (0)

/* Non-tail dispatch (the result takes part in tests/assignments): set
 * OPRT_NOT_SUPPORTED when the card is unavailable */
#define CORE_BLUETOOTH_EXEC_OP_VAL(ret, op_name, ...)                                                                   \
    do {                                                                                                               \
        CORE_BLUETOOTH_OPS_T *ops = core_bluetooth_get_ops();                                                            \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            (ret) = ops->op_name(__VA_ARGS__);                                                                         \
        } else {                                                                                                       \
            PR_ERR("CORE bluetooth operation %s not available", #op_name);                                              \
            (ret) = OPRT_NOT_SUPPORTED;                                                                                \
        }                                                                                                              \
    } while (0)

#define CORE_COMMON_SERVICE_INDEX (0)

#define CORE_COMMON_CHAR_MAX_NUM (3)

#if (defined(ENABLE_MATTER_TUYA_BLE_CHAR) && (ENABLE_MATTER_TUYA_BLE_CHAR == 1))
#define CORE_COMMON_SERVICE_MAX_NUM    (2)
#define CORE_MATTER_TUYA_SERVICE_INDEX (1)
#define CORE_MATTER_TUYA_CHAR_MAX_NUM  (3)
#else
#define CORE_COMMON_SERVICE_MAX_NUM (1)
#endif

#ifndef CORE_BLE_SERVICE_VERSION
#define CORE_BLE_SERVICE_VERSION (2) // Default Service is version 2: Follow the latest service.
#endif

/**< Cache GATTS parameters and the characteristic table using the mirror
 * types of the card contract */
static CORE_BLE_GATTS_PARAMS_T core_ble_gatt_service = {0};
static CORE_BLE_SERVICE_PARAMS_T *pcore_ble_service = NULL;
static CORE_BLE_CHAR_PARAMS_T *pcore_ble_service_char = NULL;

static uint16_t core_ble_common_connect_handle = CORE_BLE_GATT_INVALID_HANDLE;

/**< Use CORE Definitions, CORE Only Support One GATT Link at one time, follow
 * Bluetooth Spec. */
static CORE_BLE_EVT_FUNC_CB core_ble_event_callback;
static __attribute__((unused)) uint16_t core_ble_uuid16_convert(CORE_BLE_UUID_T *p_uuid)
{
    uint16_t uuid16 = 0xFFFF;

    if (p_uuid->uuid_type == CORE_BLE_UUID_TYPE_16) {
        return p_uuid->uuid.uuid16;
    } else if (p_uuid->uuid_type == CORE_BLE_UUID_TYPE_32) {
        /**< Not Support */
        return 0xFFFF;
    } else if (p_uuid->uuid_type == CORE_BLE_UUID_TYPE_128) {
        uuid16 = p_uuid->uuid.uuid128[13] << 8 | p_uuid->uuid.uuid128[12];
        return uuid16;
    }

    return 0xFFFF;
}

static void core_ble_kernel_gap_event_callback(CORE_BLE_GAP_PARAMS_EVT_T *p_event)
{
    CORE_BLE_EVT_PARAMS_T core_event;

    if (p_event == NULL) {
        return;
    }

    memset(&core_event, 0, sizeof(CORE_BLE_EVT_PARAMS_T));

    switch (p_event->type) {
    case CORE_BLE_EVT_STACK_INIT: {
        core_event.type = CORE_BLE_STACK_INIT;
        core_event.ble_event.init = p_event->result;
    } break;

    case CORE_BLE_EVT_STACK_DEINIT: {
        core_event.type = CORE_BLE_STACK_DEINIT;
        core_event.ble_event.init = p_event->result;
    } break;

    case CORE_BLE_GAP_EVT_CONNECT: {
        core_event.ble_event.connect.result = p_event->result;
        if (p_event->gap_event.connect.role == CORE_BLE_ROLE_SERVER) {
            core_event.type = CORE_BLE_EVT_PERIPHERAL_CONNECT;

            core_ble_common_connect_handle = p_event->conn_handle;
            core_event.ble_event.connect.peer.conn_handle = p_event->conn_handle;
            core_event.ble_event.connect.peer.peer_addr.type = p_event->gap_event.connect.peer_addr.type;
            memcpy(core_event.ble_event.connect.peer.peer_addr.addr, p_event->gap_event.connect.peer_addr.addr, 6);

            core_event.ble_event.connect.peer.char_handle[CORE_COMMON_WRITE_CHAR_INDEX] =
                pcore_ble_service_char[CORE_COMMON_WRITE_CHAR_INDEX].handle;
            core_event.ble_event.connect.peer.char_handle[CORE_COMMON_NOTIFY_CHAR_INDEX] =
                pcore_ble_service_char[CORE_COMMON_NOTIFY_CHAR_INDEX].handle;
            core_event.ble_event.connect.peer.char_handle[CORE_COMMON_READ_CHAR_INDEX] =
                pcore_ble_service_char[CORE_COMMON_READ_CHAR_INDEX].handle;
#if (defined(ENABLE_MATTER_TUYA_BLE_CHAR) && (ENABLE_MATTER_TUYA_BLE_CHAR == 1))
            core_event.ble_event.connect.peer.char_handle[CORE_MATTER_TUYA_T1_CHAR_INDEX] =
                pcore_ble_service_char[CORE_MATTER_TUYA_T1_CHAR_INDEX].handle;
            core_event.ble_event.connect.peer.char_handle[CORE_MATTER_TUYA_T2_CHAR_INDEX] =
                pcore_ble_service_char[CORE_MATTER_TUYA_T2_CHAR_INDEX].handle;
            core_event.ble_event.connect.peer.char_handle[CORE_MATTER_TUYA_T3_CHAR_INDEX] =
                pcore_ble_service_char[CORE_MATTER_TUYA_T3_CHAR_INDEX].handle;
#endif

        }
    } break;

    case CORE_BLE_GAP_EVT_DISCONNECT: {
        if (p_event->gap_event.disconnect.role == CORE_BLE_ROLE_SERVER &&
            p_event->conn_handle == core_ble_common_connect_handle) {
            core_ble_common_connect_handle = CORE_BLE_GATT_INVALID_HANDLE;
        }

        core_event.type = CORE_BLE_EVT_DISCONNECT;
        core_event.ble_event.disconnect.peer.conn_handle = p_event->conn_handle;
        core_event.ble_event.disconnect.reason = p_event->gap_event.disconnect.reason;
    } break;

    case CORE_BLE_GAP_EVT_CONN_PARAM_REQ: {
        core_event.type = CORE_BLE_EVT_CONN_PARAM_REQ;
        core_event.ble_event.conn_param.conn_handle = p_event->conn_handle;
    } break;

    case CORE_BLE_GAP_EVT_CONN_PARAM_UPDATE: {
        core_event.type = CORE_BLE_EVT_CONN_PARAM_UPDATE;
        core_event.ble_event.conn_param.conn_handle = p_event->conn_handle;

        core_event.ble_event.conn_param.conn.conn_sup_timeout = p_event->gap_event.conn_param.conn_sup_timeout;
        core_event.ble_event.conn_param.conn.max_conn_interval = p_event->gap_event.conn_param.conn_interval_max;
        core_event.ble_event.conn_param.conn.min_conn_interval = p_event->gap_event.conn_param.conn_interval_min;
        core_event.ble_event.conn_param.conn.latency = p_event->gap_event.conn_param.conn_latency;
    } break;

    case CORE_BLE_GAP_EVT_CONN_RSSI: {
        core_event.type = CORE_BLE_EVT_CONN_RSSI;
        core_event.ble_event.link_rssi.conn_handle = p_event->conn_handle;

        core_event.ble_event.link_rssi.rssi = p_event->gap_event.link_rssi;
    } break;

    default: {
    }
        return;
    }

    if (core_ble_event_callback) {
        core_ble_event_callback(&core_event);
    }
}

static void core_ble_kernel_gatt_event_callback(CORE_BLE_GATT_PARAMS_EVT_T *p_event)
{
    CORE_BLE_EVT_PARAMS_T core_event;

    if (p_event == NULL) {
        return;
    }

    memset(&core_event, 0, sizeof(CORE_BLE_EVT_PARAMS_T));

    switch (p_event->type) {
    case CORE_BLE_GATT_EVT_MTU_REQUEST: {
        core_event.type = CORE_BLE_EVT_MTU_REQUEST;
        core_event.ble_event.exchange_mtu.conn_handle = p_event->conn_handle;
        core_event.ble_event.exchange_mtu.mtu = p_event->gatt_event.exchange_mtu;
    } break;
    case CORE_BLE_GATT_EVT_NOTIFY_TX: {
        core_event.type = CORE_BLE_EVT_NOTIFY_TX;
        core_event.ble_event.notify_result.conn_handle = p_event->conn_handle;

        core_event.ble_event.notify_result.char_handle = p_event->gatt_event.notify_result.char_handle;
        core_event.ble_event.notify_result.result = p_event->gatt_event.notify_result.result;
    } break;

    case CORE_BLE_GATT_EVT_WRITE_REQ: {
        core_event.type = CORE_BLE_EVT_WRITE_REQ;
        core_event.ble_event.write_report.peer.conn_handle = p_event->conn_handle;
        core_event.ble_event.write_report.peer.char_handle[0] = p_event->gatt_event.write_report.char_handle;
        core_event.ble_event.write_report.report.len = p_event->gatt_event.write_report.report.len;
        core_event.ble_event.write_report.report.p_data = p_event->gatt_event.write_report.report.p_data;
    } break;

    case CORE_BLE_GATT_EVT_NOTIFY_INDICATE_RX: {
        core_event.type = CORE_BLE_EVT_NOTIFY_RX;
        core_event.ble_event.data_report.peer.conn_handle = p_event->conn_handle;
        core_event.ble_event.data_report.peer.char_handle[0] = p_event->gatt_event.data_report.char_handle;
        core_event.ble_event.data_report.report.len = p_event->gatt_event.data_report.report.len;
        core_event.ble_event.data_report.report.p_data = p_event->gatt_event.data_report.report.p_data;
    } break;
    case CORE_BLE_GATT_EVT_SUBSCRIBE: {
        if (p_event->result == OPRT_OK) {
            core_event.type = CORE_BLE_EVT_SUBSCRIBE;
            core_event.ble_event.subscribe.conn_handle = p_event->conn_handle;
            core_event.ble_event.subscribe.char_handle = p_event->gatt_event.subscribe.char_handle;
            core_event.ble_event.subscribe.reason = p_event->gatt_event.subscribe.reason;
            core_event.ble_event.subscribe.prev_notify = p_event->gatt_event.subscribe.prev_notify;
            core_event.ble_event.subscribe.cur_notify = p_event->gatt_event.subscribe.cur_notify;
            core_event.ble_event.subscribe.prev_indicate = p_event->gatt_event.subscribe.prev_indicate;
            core_event.ble_event.subscribe.cur_indicate = p_event->gatt_event.subscribe.cur_indicate;
        }
    } break;

    case CORE_BLE_GATT_EVT_READ_CHAR_VALUE: {
        core_event.type = CORE_BLE_EVT_READ_CHAR;
        core_event.ble_event.char_read.conn_handle = p_event->conn_handle;
        core_event.ble_event.char_read.char_handle = p_event->gatt_event.char_read.char_handle;
        core_event.ble_event.char_read.offset = p_event->gatt_event.char_read.offset;
    } break;

    default: {
        // Not Support
    }
        return;
    }

    if (core_ble_event_callback) {
        core_ble_event_callback(&core_event);
    }
}

/**
 * @brief   Function for initializing the bluetooth
 * @param   [in] role:      set the ble/bt role
 *          [in] p_event:   Event handler provided by the user.
 * @return  SUCCESS         Initialized successfully.
 *          ERROR
 * */
OPERATE_RET core_ble_bt_init(CORE_BLE_ROLE_E role, const CORE_BLE_EVT_FUNC_CB ble_event)
{
    uint8_t ble_stack_role = 0; // CORE_BLE_ROLE_SERVER;

    if (ble_event != NULL) {
        /**<  Get the CORE Event Callback. */
        core_ble_event_callback = ble_event;

        /**< Register GAP And GATT Callback
         * Ordering constraint: must precede stack_init and
         * gatts_service_add (the bluez layer latches the callback pointers on
         * first use; see the core_bluetooth_ops.h contract).
         *
         * These are NOT tail calls: init must continue with the service
         * registration and stack_init below, so the value-dispatch macro is
         * required here (the plain EXEC_OP macro returns from this function). */
        OPERATE_RET cb_ret = OPRT_OK;

        CORE_BLUETOOTH_EXEC_OP_VAL(cb_ret, gap_callback_register, core_ble_kernel_gap_event_callback);
        if (cb_ret != OPRT_OK) {
            return cb_ret;
        }
        CORE_BLUETOOTH_EXEC_OP_VAL(cb_ret, gatt_callback_register, core_ble_kernel_gatt_event_callback);
        if (cb_ret != OPRT_OK) {
            return cb_ret;
        }
    }

    /**< Init Bluetooth Stack Role For Ble. */
    if ((role & CORE_BLE_ROLE_PERIPERAL) == CORE_BLE_ROLE_PERIPERAL ||
        (role & CORE_BLE_ROLE_BEACON) == CORE_BLE_ROLE_BEACON) {
        ble_stack_role |= CORE_BLE_ROLE_SERVER;
    }
    if ((role & CORE_BLE_ROLE_CENTRAL) == CORE_BLE_ROLE_CENTRAL) {
        ble_stack_role |= CORE_BLE_ROLE_CLIENT;
    }

    uint8_t service_alloced = 0;
    if (!pcore_ble_service) {
        pcore_ble_service = core_malloc(sizeof(CORE_BLE_SERVICE_PARAMS_T) * CORE_BLE_GATT_SERVICE_MAX_NUM);
        if (!pcore_ble_service) {
            PR_ERR("ble service malloc failed");
            return OPRT_MALLOC_FAILED;
        }
        service_alloced = 1;
    }
    if (!pcore_ble_service_char) {
        pcore_ble_service_char = core_malloc(sizeof(CORE_BLE_CHAR_PARAMS_T) * CORE_BLE_GATT_CHAR_MAX_NUM);
        if (!pcore_ble_service_char) {
            PR_ERR("ble service char malloc failed");
            if (service_alloced) {
                core_free(pcore_ble_service);
                pcore_ble_service = NULL;
            }
            return OPRT_MALLOC_FAILED;
        }
    }

    if ((role & CORE_BLE_ROLE_PERIPERAL) == CORE_BLE_ROLE_PERIPERAL) {
        CORE_BLE_GATTS_PARAMS_T *p_ble_services = &core_ble_gatt_service;
        *p_ble_services = (CORE_BLE_GATTS_PARAMS_T){
            .svc_num = CORE_COMMON_SERVICE_MAX_NUM,
            .p_service = pcore_ble_service,
        };

#if (defined(CORE_BLE_SERVICE_VERSION) && (CORE_BLE_SERVICE_VERSION == 2))
        /**< Add Tuya Common Service */
        *(pcore_ble_service + CORE_COMMON_SERVICE_INDEX) = (CORE_BLE_SERVICE_PARAMS_T){
            .handle = CORE_BLE_GATT_INVALID_HANDLE,
            .svc_uuid =
                {
                    .uuid_type = CORE_BLE_UUID_TYPE_16,
                    .uuid.uuid16 = CORE_BLE_CMD_SERVICE_UUID_V2,
                },
            .type = CORE_BLE_UUID_SERVICE_PRIMARY,
            .char_num = CORE_COMMON_CHAR_MAX_NUM,
            .p_char = pcore_ble_service_char,
        };

        /**< Add Write Characteristic */
        *(pcore_ble_service_char + CORE_COMMON_WRITE_CHAR_INDEX) = (CORE_BLE_CHAR_PARAMS_T){
            .handle = CORE_BLE_GATT_INVALID_HANDLE,
            .char_uuid =
                {
                    .uuid_type = CORE_BLE_UUID_TYPE_128,
                    .uuid.uuid128 = CORE_BLE_CMD_WRITE_CHAR_UUID128_V2,
                },
            .property = CORE_BLE_GATT_CHAR_PROP_WRITE | CORE_BLE_GATT_CHAR_PROP_WRITE_NO_RSP,
            .permission = CORE_BLE_GATT_PERM_READ | CORE_BLE_GATT_PERM_WRITE,
            .value_len = 244,
        };

        /**< Add Notify Characteristic */
        *(pcore_ble_service_char + CORE_COMMON_NOTIFY_CHAR_INDEX) = (CORE_BLE_CHAR_PARAMS_T){
            .handle = CORE_BLE_GATT_INVALID_HANDLE,
            .char_uuid =
                {
                    .uuid_type = CORE_BLE_UUID_TYPE_128,
                    .uuid.uuid128 = CORE_BLE_CMD_NOTIFY_CHAR_UUID128_V2,
                },
            .property = CORE_BLE_GATT_CHAR_PROP_NOTIFY,
            .permission = CORE_BLE_GATT_PERM_READ | CORE_BLE_GATT_PERM_WRITE,
            .value_len = 244,
        };

        /**< Add Read Characteristic */
        *(pcore_ble_service_char + CORE_COMMON_READ_CHAR_INDEX) = (CORE_BLE_CHAR_PARAMS_T){
            .handle = CORE_BLE_GATT_INVALID_HANDLE,
            .char_uuid =
                {
                    .uuid_type = CORE_BLE_UUID_TYPE_128,
                    .uuid.uuid128 = CORE_BLE_CMD_READ_CHAR_UUID128_V2,
                },
            .property = CORE_BLE_GATT_CHAR_PROP_READ,
            .permission = CORE_BLE_GATT_PERM_READ,
            .value_len = 244,
        };
#else
        /**< Add Tuya Common Service */
        *(pcore_ble_service + CORE_COMMON_SERVICE_INDEX) = (CORE_BLE_SERVICE_PARAMS_T){
            .handle = CORE_BLE_GATT_INVALID_HANDLE,
            .svc_uuid =
                {
                    .uuid_type = CORE_BLE_UUID_TYPE_16,
                    .uuid.uuid16 = CORE_BLE_CMD_SERVICE_UUID_V1,
                },
            .type = CORE_BLE_UUID_SERVICE_PRIMARY,
            .char_num = 2,
            .p_char = pcore_ble_service_char,
        };

        /**< Add Write Characteristic */

        *(pcore_ble_service_char + CORE_COMMON_WRITE_CHAR_INDEX) = (CORE_BLE_CHAR_PARAMS_T){
            .handle = CORE_BLE_GATT_INVALID_HANDLE,
            .char_uuid =
                {
                    .uuid_type = CORE_BLE_UUID_TYPE_16,
                    .uuid.uuid16 = CORE_BLE_CMD_WRITE_CHAR_UUID_V1,
                },
            .property = CORE_BLE_GATT_CHAR_PROP_WRITE | CORE_BLE_GATT_CHAR_PROP_WRITE_NO_RSP,
            .permission = CORE_BLE_GATT_PERM_READ | CORE_BLE_GATT_PERM_WRITE,
            .value_len = 244,
        };

        /**< Add Notify Characteristic */
        *(pcore_ble_service_char + CORE_COMMON_NOTIFY_CHAR_INDEX) = (CORE_BLE_CHAR_PARAMS_T){
            .handle = CORE_BLE_GATT_INVALID_HANDLE,
            .char_uuid =
                {
                    .uuid_type = CORE_BLE_UUID_TYPE_16,
                    .uuid.uuid16 = CORE_BLE_CMD_NOTIFY_CHAR_UUID_V1,
                },
            .property = CORE_BLE_GATT_CHAR_PROP_NOTIFY,
            .permission = CORE_BLE_GATT_PERM_READ | CORE_BLE_GATT_PERM_WRITE,
            .value_len = 244,
        };

#endif
        OPERATE_RET add_ret = OPRT_OK;

        /* With an empty card add_ret=OPRT_NOT_SUPPORTED, treated as an
         * initialization failure (any non-zero result fails) */
        CORE_BLUETOOTH_EXEC_OP_VAL(add_ret, gatts_service_add, p_ble_services);
        if (add_ret != OPRT_OK) {
            return OPRT_OS_ADAPTER_BLE_INIT_FAILED;
        }
    }

    OPERATE_RET init_ret = OPRT_OK;

    CORE_BLUETOOTH_EXEC_OP_VAL(init_ret, stack_init, ble_stack_role);
    if (init_ret != OPRT_OK) {
        return OPRT_OS_ADAPTER_BLE_INIT_FAILED;
    }
    return OPRT_OK;
}

/**
 * @brief Map the public CORE role bitmask to the mirror (card) role bitmask
 *
 * The mapping is explicit, so it does not rely on the numeric values being
 * identical to the mirror role values.
 *
 * The mirror role set has only SERVER/CLIENT: CORE_BLE_ROLE_BEACON (0x04) and
 * the Mesh roles (from 0x0100) have no counterpart. Any such bit makes the
 * call unsupported (OPRT_NOT_SUPPORTED) rather than being silently dropped.
 */
static OPERATE_RET core_ble_role_to_stack_role(CORE_BLE_ROLE_E role, uint8_t *p_stack_role)
{
    if (p_stack_role == NULL) {
        return OPRT_INVALID_PARM;
    }

    if ((role & ~(CORE_BLE_ROLE_PERIPERAL | CORE_BLE_ROLE_CENTRAL)) != 0) {
        return OPRT_NOT_SUPPORTED;
    }

    *p_stack_role = 0;
    if ((role & CORE_BLE_ROLE_PERIPERAL) == CORE_BLE_ROLE_PERIPERAL) {
        *p_stack_role |= CORE_BLE_ROLE_SERVER;
    }
    if ((role & CORE_BLE_ROLE_CENTRAL) == CORE_BLE_ROLE_CENTRAL) {
        *p_stack_role |= CORE_BLE_ROLE_CLIENT;
    }

    return OPRT_OK;
}

/**
 * @brief   Function for deinitializing the bluetooth, or specify one role
 * @param   [in] role:      set the ble/bt role
 * @return  SUCCESS         Deinitialized successfully.
 *          ERROR
 * */
OPERATE_RET core_ble_bt_deinit(CORE_BLE_ROLE_E role)
{
    uint8_t ble_stack_role = 0;

    /* Explicit role mapping; BEACON/Mesh and other roles without a mirror
     * value are treated as unsupported */
    if (core_ble_role_to_stack_role(role, &ble_stack_role) != OPRT_OK) {
        return OPRT_NOT_SUPPORTED;
    }

    /* Clear the pointers after freeing: core_ble_bt_init() allocates these
     * tables only when they are NULL, so leaving them dangling would make a
     * deinit -> init cycle skip the allocation and write into freed memory. */
    if (pcore_ble_service_char) {
        core_free(pcore_ble_service_char);
        pcore_ble_service_char = NULL;
    }
    if (pcore_ble_service) {
        core_free(pcore_ble_service);
        pcore_ble_service = NULL;
    }

    /* With an empty card (NULL card or NULL member) return
     * OPRT_NOT_SUPPORTED */
    CORE_BLUETOOTH_EXEC_OP(stack_deinit, ble_stack_role);
}

/**
 * @brief   Set the local Bluetooth identity address.
 * @param   [in] p_addr:    pointer to local address
 * @return  SUCCESS         Set Address successfully.
 *          ERROR
 * */
OPERATE_RET core_ble_address_set(CORE_BLE_ADDR_T const *p_addr)
{
    CORE_BLE_GAP_ADDR_T ble_addr = {0};

    if (p_addr == NULL) {
        return OPRT_INVALID_PARM;
    }

    ble_addr.type = p_addr->type;
    memcpy(ble_addr.addr, p_addr->addr, 6);
    CORE_BLUETOOTH_EXEC_OP(gap_addr_set, &ble_addr);
}

/**
 * @brief   Get the local Bluetooth identity address.
 * @param   [out] p_addr: pointer to local address
 * @return  SUCCESS         Set Address successfully.
 *          ERROR
 * */
OPERATE_RET core_ble_address_get(CORE_BLE_ADDR_T *p_addr)
{
    CORE_BLE_GAP_ADDR_T core_addr = {0};
    OPERATE_RET rt = OPRT_OK;

    if (p_addr == NULL) {
        return OPRT_INVALID_PARM;
    }

    /* Empty-card degradation: start from the neutral value (zeroed); if the
     * address is unavailable keep it and return the error code */
    memset(p_addr, 0, sizeof(CORE_BLE_ADDR_T));

    CORE_BLUETOOTH_EXEC_OP_VAL(rt, gap_address_get, &core_addr);
    if (rt != OPRT_OK) {
        return rt;
    }

    p_addr->type = core_addr.type;
    memcpy(p_addr->addr, core_addr.addr, 6);

    return OPRT_OK;
}

/**
 * @brief   Get Max Link Size
 * @param   [out] max_mtu:  set mtu size.
 * @return  SUCCESS
 *          ERROR
 * */
OPERATE_RET core_ble_bt_link_max(uint16_t *p_maxlink)
{
    OPERATE_RET rt = OPRT_OK;

    if (p_maxlink == NULL) {
        return OPRT_INVALID_PARM;
    }

    /* Empty-card degradation: start from the neutral value 0; if the card is
     * unavailable keep it and return the error code */
    *p_maxlink = 0;

    CORE_BLUETOOTH_EXEC_OP_VAL(rt, stack_gatt_link, p_maxlink);

    return rt;
}

/**
 * @brief   Start advertising
 * @param  [in] p_adv_param : pointer to advertising parameters, see
 * CORE_BLE_ADV_PARAMS_T for details
 * @return  SUCCESS             Successfully started advertising procedure.
 *          ERR_INVALID_STATE   Not in advertising state.
 * */
OPERATE_RET core_ble_advertising_start(CORE_BLE_ADV_PARAMS_T const *p_adv_param)
{
    CORE_BLE_GAP_ADV_PARAMS_T core_adv_params = {0};

    if (p_adv_param == NULL) {
        return OPRT_INVALID_PARM;
    }

    core_adv_params.adv_type = p_adv_param->adv_type;
    core_adv_params.direct_addr.type = p_adv_param->direct_addr.type;
    core_adv_params.adv_interval_min = p_adv_param->adv_interval_min;
    core_adv_params.adv_interval_max = p_adv_param->adv_interval_max;
    core_adv_params.adv_channel_map = 0x01 | 0x02 | 0x04;

    memcpy(core_adv_params.direct_addr.addr, p_adv_param->direct_addr.addr, 6);

    CORE_BLUETOOTH_EXEC_OP(gap_adv_start, &core_adv_params);
}

/**
 * @brief   Setting advertising data
 * @param   [in] p_adv : Data to be used in advertisement packets
 *          [in] p_rsp : Data to be used in scan response packets.
 * @return  SUCCESS             Successfully set advertising data.
 *          ERR_INVALID_param   Invalid parameter(s) supplied.
 * */
OPERATE_RET core_ble_advertising_data_set(CORE_BLE_DATA_T *p_adv, CORE_BLE_DATA_T *p_scan_rsp)
{
    CORE_BLE_DATA_T adv_data = {0};
    CORE_BLE_DATA_T scan_rsp_data = {0};

    if (p_adv != NULL) {
        adv_data.len = p_adv->len;
        adv_data.p_data = p_adv->p_data;
    } else {
        adv_data.len = 0;
        adv_data.p_data = NULL;
    }

    if (p_scan_rsp != NULL) {
        scan_rsp_data.len = p_scan_rsp->len;
        scan_rsp_data.p_data = p_scan_rsp->p_data;
    } else {
        scan_rsp_data.len = 0;
        scan_rsp_data.p_data = NULL;
    }

    CORE_BLUETOOTH_EXEC_OP(gap_adv_rsp_data_set, &adv_data, &scan_rsp_data);
}

/**
 * @brief   Stop advertising
 * @param   void
 * @return  SUCCESS             Successfully stopped advertising procedure.
 *          ERR_INVALID_STATE   Not in advertising state.
 * */
OPERATE_RET core_ble_advertising_stop(void)
{
    CORE_BLUETOOTH_EXEC_OP(gap_adv_stop);
}

/**
 * @brief   Updating advertising data
 * @param   [in] p_adv :        Data to be used in advertisement packets
 *          [in] p_rsp :        Data to be used in scan response packets.
 * @return  SUCCESS             Successfully update advertising data.
 *          ERR_INVALID_STATE   Invalid parameter(s) supplied.
 * @Note:   If being in advertising, we will do update adv+rsp data.
 *          If not being in advertising, we will only do set adv+rsp data.
 * */
OPERATE_RET core_ble_advertising_data_update(CORE_BLE_DATA_T *p_adv, CORE_BLE_DATA_T *p_scan_rsp)
{
    CORE_BLE_DATA_T adv_data = {0};
    CORE_BLE_DATA_T scan_rsp_data = {0};

    if (p_adv != NULL) {
        adv_data.len = p_adv->len;
        adv_data.p_data = p_adv->p_data;
    } else {
        adv_data.len = 0;
        adv_data.p_data = NULL;
    }

    if (p_scan_rsp != NULL) {
        scan_rsp_data.len = p_scan_rsp->len;
        scan_rsp_data.p_data = p_scan_rsp->p_data;
    } else {
        scan_rsp_data.len = 0;
        scan_rsp_data.p_data = NULL;
    }

    CORE_BLUETOOTH_EXEC_OP(gap_adv_rsp_data_update, &adv_data, &scan_rsp_data);
}

/**
 * @brief   Get the received signal strength for the last connection event.
 * @param   [in]peer:       conn_handle Connection handle.
 * @return  SUCCESS         Successfully read the RSSI.
 *          ERROR           No sample is available.
 *          The rssi reading is given through CORE_BLE_EVT_CONN_RSSI event !!!!!!
 * */
OPERATE_RET core_ble_rssi_get(const CORE_BLE_PEER_INFO_T peer)
{
    uint16_t conn_handle = peer.conn_handle;

    CORE_BLUETOOTH_EXEC_OP(gap_rssi_get, conn_handle);
}

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
core_ble_conn_param_update(const CORE_BLE_PEER_INFO_T peer, CORE_BLE_CONN_PARAMS_T const *p_conn_params)
{
    uint16_t conn_handle = peer.conn_handle;
    CORE_BLE_GAP_CONN_PARAMS_T param = {0};

    if (p_conn_params == NULL) {
        return OPRT_INVALID_PARM;
    }

    param.conn_interval_min = p_conn_params->min_conn_interval;
    param.conn_interval_max = p_conn_params->max_conn_interval;
    param.conn_latency = p_conn_params->latency;
    param.conn_sup_timeout = p_conn_params->conn_sup_timeout;
    param.connection_timeout = p_conn_params->connection_timeout;

    CORE_BLUETOOTH_EXEC_OP(gap_conn_param_update, conn_handle, &param);
}

/**
 * @brief   Start connecting one peer
 * @param : [in] peer info.     include address and address type
 *          [in] conn_param:    connect parameters including interval, peer info
 * \
 * @return  SUCCESS             Successfully initiated connecting procedure.
 *
 *          The connection event is given through CORE_BLE_EVT_CENTRAL_CONNECT_DISCOVERY
 * event !!!!!!
 */
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
OPERATE_RET core_ble_disconnect(const CORE_BLE_PEER_INFO_T peer)
{
    uint16_t conn_handle = peer.conn_handle;
    CORE_BLUETOOTH_EXEC_OP(gap_disconnect, conn_handle, CORE_BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
}

/**
 * @brief   [Ble Peripheral] Notify Command characteristic value by
 * characteristic handle
 * @param   [in] p_data: pointer to data, refer to CORE_BLE_DATA_T
 * @return  SUCCESS
 *          ERROR Refer to platform error code
 * */
OPERATE_RET core_ble_server_common_notify(uint16_t index, CORE_BLE_DATA_T *p_data)
{
    if (p_data == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (NULL == pcore_ble_service_char) {
        /* init not completed (a platform without BLE capability returns early
         * because the card is unavailable, or init was never called): the
         * service characteristic table does not exist, so avoid a NULL
         * dereference */
        return OPRT_RESOURCE_NOT_READY;
    }

    if (core_ble_common_connect_handle != CORE_BLE_GATT_INVALID_HANDLE &&
        pcore_ble_service_char[index].handle != CORE_BLE_GATT_INVALID_HANDLE) {
        CORE_BLUETOOTH_EXEC_OP(gatts_value_notify, core_ble_common_connect_handle,
                              pcore_ble_service_char[index].handle, p_data->p_data, p_data->len);
    }

    return OPRT_OS_ADAPTER_BLE_NOTIFY_FAILED;
}

/**
 * @brief   [Ble Peripheral] Notify Command characteristic value
 * @param   [in] p_data: pointer to data, refer to CORE_BLE_DATA_T
 * @return  SUCCESS
 *          ERROR Refer to platform error code
 * */
OPERATE_RET core_ble_server_common_send(CORE_BLE_DATA_T *p_data)
{
    if (p_data == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (NULL == pcore_ble_service_char) {
        /* init not completed (a platform without BLE capability returns early
         * because the card is unavailable, or init was never called): the
         * service characteristic table does not exist, so avoid a NULL
         * dereference */
        return OPRT_RESOURCE_NOT_READY;
    }

    if (core_ble_common_connect_handle != CORE_BLE_GATT_INVALID_HANDLE &&
        pcore_ble_service_char[CORE_COMMON_NOTIFY_CHAR_INDEX].handle != CORE_BLE_GATT_INVALID_HANDLE) {

        CORE_BLUETOOTH_EXEC_OP(gatts_value_notify, core_ble_common_connect_handle,
                              pcore_ble_service_char[CORE_COMMON_NOTIFY_CHAR_INDEX].handle, p_data->p_data, p_data->len);
    }

    return OPRT_OS_ADAPTER_BLE_NOTIFY_FAILED;
}

/**
 * @brief   [Ble Peripheral] Set Read Command characteristic value by
 * characteristic handle
 * @param   [in] index: char index
 * @param   [in] p_data: pointer to data, refer to CORE_BLE_DATA_T
 * @return  SUCCESS
 *          ERROR Refer to platform error code
 * */
OPERATE_RET core_ble_server_common_read_update_ext(uint16_t index, CORE_BLE_DATA_T *p_data)
{
    if (p_data == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (NULL == pcore_ble_service_char) {
        /* init not completed (a platform without BLE capability returns early
         * because the card is unavailable, or init was never called): the
         * service characteristic table does not exist, so avoid a NULL
         * dereference */
        return OPRT_RESOURCE_NOT_READY;
    }

    if (core_ble_common_connect_handle != CORE_BLE_GATT_INVALID_HANDLE &&
        pcore_ble_service_char[index].handle != CORE_BLE_GATT_INVALID_HANDLE) {

        CORE_BLUETOOTH_EXEC_OP(gatts_value_set, core_ble_common_connect_handle,
                              pcore_ble_service_char[index].handle, p_data->p_data, p_data->len);
    }

    return OPRT_INVALID_PARM;
}

/**
 * @brief   [Ble Peripheral] Set Read Command characteristic value
 * @param   [in] p_data: pointer to data, refer to CORE_BLE_DATA_T
 * @return  SUCCESS
 *          ERROR Refer to platform error code
 * */
OPERATE_RET core_ble_server_common_read_update(CORE_BLE_DATA_T *p_data)
{
    if (p_data == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (NULL == pcore_ble_service_char) {
        /* init not completed (a platform without BLE capability returns early
         * because the card is unavailable, or init was never called): the
         * service characteristic table does not exist, so avoid a NULL
         * dereference */
        return OPRT_RESOURCE_NOT_READY;
    }

    if (core_ble_common_connect_handle != CORE_BLE_GATT_INVALID_HANDLE &&
        pcore_ble_service_char[CORE_COMMON_READ_CHAR_INDEX].handle != CORE_BLE_GATT_INVALID_HANDLE) {

        CORE_BLUETOOTH_EXEC_OP(gatts_value_set, core_ble_common_connect_handle,
                              pcore_ble_service_char[CORE_COMMON_READ_CHAR_INDEX].handle, p_data->p_data, p_data->len);
    }

    return OPRT_INVALID_PARM;
}
/**
 * @brief   [Ble Peripheral] Reply to an ATT_MTU exchange request by sending an
 * Exchange MTU Response to the client.
 * @param   [in] att_mtu: mtu size
 * @return  SUCCESS
 *          ERROR Refer to platform error code
 *          The MTU negotiation is given through CORE_BLE_EVT_MTU_REQUEST event
 * !!!!!!
 * */
OPERATE_RET core_ble_server_exchange_mtu_reply(const CORE_BLE_PEER_INFO_T peer, uint16_t server_mtu)
{
    uint16_t conn_handle = peer.conn_handle;

    if (server_mtu < 23 || server_mtu > 247) {
        return OPRT_INVALID_PARM;
    }

    CORE_BLUETOOTH_EXEC_OP(gatts_exchange_mtu_reply, conn_handle, server_mtu);
}
