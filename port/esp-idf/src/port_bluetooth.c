/**
 * @file port_bluetooth.c
 * @brief ESP32 BLE implementation (esp-idf NimBLE): port_ble_* GAP/GATT entry points.
 *
 * Implements the port_ble_* interface declared in port_bluetooth.h on top of the
 * esp-idf bundled NimBLE host (CONFIG_BT_NIMBLE_ENABLED). The reference for the
 * event translation (NimBLE events -> CORE_BLE_* parameter structs consumed by
 * core's core_bluetooth.c) is the upstream Tuya NimBLE adaptation; the host
 * lifecycle here is the esp-idf one:
 *   nimble_port_init()                controller + host bring-up
 *   ble_hs_cfg.reset_cb/sync_cb      stack event callbacks
 *   nimble_port_freertos_init()      NimBLE host task
 *   nimble_port_stop()/deinit()      teardown
 *
 * Coverage: stack init/deinit, GAP address/advertising/disconnect/connection-
 * parameter update/RSSI, GATT server service add / value set / notify / MTU.
 * The GATT client / central and scan entry points are not implemented: the
 * supported role is peripheral (slave) only (it advertises, it does not scan).
 *
 * Layout: this single file holds both the implementation and the CORE BLE card.
 * With ENABLE_BLUETOOTH it compiles both; otherwise it compiles only the empty
 * card (every member NULL) so core degrades every BLE call to OPRT_NOT_SUPPORTED.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "core_bluetooth_ops.h"   /* contract layer (interface): CORE BLE card */

#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1)

#include "port_bluetooth.h"       /* port: this platform BLE interface + CORE_BLE_* types */
#include "tuya_cloud_types.h"     /* TRUE/FALSE, uint8_t and friends (this layer's common types) */
#include "tuya_error_code.h"      /* OPRT_* error codes (this layer's common codes) */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_id.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "port_log.h"

/* Local capacity of the GATT service table (same limits as the upstream
 * adaptation: one row of characteristics per service). core's common Tuya
 * service needs 1 service with at most 3 characteristics. */
#define PORT_BLE_GATT_SERVICE_MAX_NUM 2
#define PORT_BLE_GATT_CHAR_MAX_NUM    4

/* Stack snapshot size for the read-characteristic value in the GATT READ
 * access callback. core's characteristic value_len is 244 and the ATT MTU
 * caps a readable value at 253, so 256 always covers it. */
#define PORT_BLE_READ_BUF_MAX 256

typedef struct {
    uint8_t role; /*< Refer to @CORE_BLE_ROLE_SERVER and @CORE_BLE_ROLE_CLIENT */
    struct {
        uint8_t *buffer;
        uint16_t size;
        uint16_t handle;
    } read_char[1];
} PORT_BLUETOOTH_SERVER_PARAMS_T;

static CORE_BLE_GAP_EVT_FUNC_CB port_bluetooth_gap_callback;
static CORE_BLE_GATT_EVT_FUNC_CB port_bluetooth_gatt_callback;

static struct ble_gatt_svc_def *port_gatt_svcs = NULL;
static struct ble_gatt_chr_def
    port_gatt_chars[PORT_BLE_GATT_SERVICE_MAX_NUM][PORT_BLE_GATT_CHAR_MAX_NUM];

static PORT_BLUETOOTH_SERVER_PARAMS_T port_ble_server;
static int gatts_service_flag = FALSE;
/* GATT services handed over before the NimBLE host existed (core registers them
 * before stack_init); port_ble_stack_init() registers them once the host
 * structures are up. */
static int gatts_pending = FALSE;
static OPERATE_RET port_ble_gatts_register_cfg(void);
static SemaphoreHandle_t port_ble_stack_mutex = NULL;
/* Guards the published read-characteristic buffer
 * (port_ble_server.read_char[0].{buffer,size,handle}): written by
 * port_ble_gatts_value_set() from the caller's task and read by the GATT READ
 * access callback on the NimBLE host task. Deliberately independent from
 * port_ble_stack_mutex: that one is held across nimble_port_stop() (which
 * blocks until the host task answers) and must never be taken from a callback
 * running on the host task. */
static SemaphoreHandle_t port_ble_read_buf_mutex = NULL;
static int stack_sync_flag = 0;
/* nimble_port_init() succeeded and the host structures are up. This is the
 * lifecycle predicate: ble_hs_is_enabled() only turns true once the host has
 * synced, so it cannot tell "never brought up" from "brought up but not yet
 * synced". Cleared by port_ble_stack_deinit() after nimble_port_deinit(). */
static int host_inited = 0;
static int host_task_running = 0;

/* ---------------------------------------------------------------------------
 * Logging
 * --------------------------------------------------------------------------- */
#define BLE_LOG_ERR(fmt, ...)  PORT_LOG_ERR(fmt, ##__VA_ARGS__)
#define BLE_LOG_WARN(fmt, ...) PORT_LOG_WARN(fmt, ##__VA_ARGS__)
#define BLE_LOG_INFO(fmt, ...) PORT_LOG_NOTICE(fmt, ##__VA_ARGS__)
#define BLE_LOG_DEBUG(fmt, ...) PORT_LOG_DEBUG(fmt, ##__VA_ARGS__)

/* ---------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------- */
static void port_ble_time_delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void port_ble_stack_mutex_init(void)
{
    if (!port_ble_stack_mutex) {
        port_ble_stack_mutex = xSemaphoreCreateMutex();
    }
}

/* Created together with the stack bring-up and kept for the process lifetime:
 * a deinit does not delete it, so a later re-init reuses the same handle. */
static void port_ble_read_buf_mutex_init(void)
{
    if (!port_ble_read_buf_mutex) {
        port_ble_read_buf_mutex = xSemaphoreCreateMutex();
    }
}

static void port_ble_stack_event_callback(CORE_BLE_GAP_EVT_TYPE_E type, int result)
{
    CORE_BLE_GAP_PARAMS_EVT_T init_event;

    memset(&init_event, 0, sizeof(init_event));
    init_event.type = type;
    init_event.result = result;
    init_event.conn_handle = CORE_BLE_GATT_INVALID_HANDLE;
    BLE_LOG_INFO("BLE stack event %d", type);
    if (port_bluetooth_gap_callback) {
        port_bluetooth_gap_callback(&init_event);
    }
}

/* ---------------------------------------------------------------------------
 * GAP event translation
 * --------------------------------------------------------------------------- */
static int port_ble_host_gap_event(struct ble_gap_event *event, void *arg)
{
    CORE_BLE_GAP_PARAMS_EVT_T gap_event;
    CORE_BLE_GATT_PARAMS_EVT_T gatt_event;

    PORT_BLUETOOTH_SERVER_PARAMS_T *p_role = arg;

    memset(&gap_event, 0, sizeof(CORE_BLE_GAP_PARAMS_EVT_T));
    memset(&gatt_event, 0, sizeof(CORE_BLE_GATT_PARAMS_EVT_T));
    gap_event.conn_handle = CORE_BLE_GATT_INVALID_HANDLE;
    gatt_event.conn_handle = CORE_BLE_GATT_INVALID_HANDLE;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        gap_event.type = CORE_BLE_GAP_EVT_CONNECT;
        gap_event.result = event->connect.status;
        gap_event.conn_handle = event->connect.conn_handle;
        gap_event.gap_event.connect.role = p_role->role;
        BLE_LOG_INFO("BLE_GAP_EVENT_CONNECT(0x%02x), handle = 0x%02x, Role(%d)", event->connect.status,
                     event->connect.conn_handle, p_role->role);
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        gap_event.type = CORE_BLE_GAP_EVT_DISCONNECT;
        gap_event.result = 0;
        gap_event.conn_handle = event->disconnect.conn.conn_handle;
        gap_event.gap_event.disconnect.reason = event->disconnect.reason;
        gap_event.gap_event.disconnect.role = p_role->role;
        BLE_LOG_WARN("BLE_GAP_EVENT_DISCONNECT(0x%02x)", event->disconnect.reason);
        break;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        BLE_LOG_INFO("BLE_GAP_EVENT_DISC_COMPLETE");
        break;

    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc desc;

        gap_event.type = CORE_BLE_GAP_EVT_CONN_PARAM_UPDATE;
        gap_event.result = 0;
        gap_event.conn_handle = event->conn_update.conn_handle;
        if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) != 0) {
            /* The connection is already gone; there is no valid parameter set
             * to report (desc would be uninitialised). */
            BLE_LOG_WARN("BLE_GAP_EVENT_CONN_UPDATE: connection not found");
            return 0;
        }

        gap_event.gap_event.conn_param.conn_interval_min = desc.conn_itvl;
        gap_event.gap_event.conn_param.conn_interval_max = desc.conn_itvl;
        gap_event.gap_event.conn_param.conn_latency = desc.conn_latency;
        gap_event.gap_event.conn_param.conn_sup_timeout = desc.supervision_timeout;
        BLE_LOG_INFO("BLE_GAP_EVENT_CONN_UPDATE,0x%x,0x%x,0x%x", desc.conn_itvl, desc.conn_latency,
                     desc.supervision_timeout);
    } break;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        BLE_LOG_INFO("BLE_GAP_EVENT_CONN_UPDATE_REQ");
        break;

    case BLE_GAP_EVENT_MTU:
        gatt_event.type = CORE_BLE_GATT_EVT_MTU_REQUEST;
        gatt_event.result = 0;
        gatt_event.conn_handle = event->mtu.conn_handle;
        gatt_event.gatt_event.exchange_mtu = event->mtu.value;
        BLE_LOG_INFO("mtu update event; conn_handle=0x%04x mtu=%d", event->mtu.conn_handle, event->mtu.value);
        break;

    case BLE_GAP_EVENT_NOTIFY_TX:
        gatt_event.type = CORE_BLE_GATT_EVT_NOTIFY_TX;
        gatt_event.result = event->notify_tx.status;
        gatt_event.conn_handle = event->notify_tx.conn_handle;
        gatt_event.gatt_event.notify_result.char_handle = event->notify_tx.attr_handle;
        /* core reads the transmission status from this field (core_bluetooth.c
         * reads notify_result.result, not the event-level result) */
        gatt_event.gatt_event.notify_result.result = event->notify_tx.status;
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        BLE_LOG_INFO("receive notify ok");
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        gatt_event.type = CORE_BLE_GATT_EVT_SUBSCRIBE;
        gatt_event.result = 0;
        gatt_event.conn_handle = event->subscribe.conn_handle;
        gatt_event.gatt_event.subscribe.char_handle = event->subscribe.attr_handle;
        gatt_event.gatt_event.subscribe.reason = event->subscribe.reason;
        gatt_event.gatt_event.subscribe.prev_notify = event->subscribe.prev_notify;
        gatt_event.gatt_event.subscribe.cur_notify = event->subscribe.cur_notify;
        gatt_event.gatt_event.subscribe.prev_indicate = event->subscribe.prev_indicate;
        gatt_event.gatt_event.subscribe.cur_indicate = event->subscribe.cur_indicate;
        BLE_LOG_INFO("BLE_GAP_EVENT_SUBSCRIBE");
        break;

    default:
        BLE_LOG_DEBUG("Unknown GAP event type = %d", event->type);
        return 0;
    }

    if (port_bluetooth_gap_callback && gap_event.conn_handle != CORE_BLE_GATT_INVALID_HANDLE) {
        port_bluetooth_gap_callback(&gap_event);
    } else if (port_bluetooth_gatt_callback && gatt_event.conn_handle != CORE_BLE_GATT_INVALID_HANDLE) {
        port_bluetooth_gatt_callback(&gatt_event);
    }
    return 0;
}

static int port_ble_host_write_callback(uint16_t conn_handle, uint16_t attr_handle,
                                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    struct os_mbuf *om = ctxt->om;
    CORE_BLE_GATT_PARAMS_EVT_T gatt_event;

    memset(&gatt_event, 0, sizeof(gatt_event));
    gatt_event.conn_handle = conn_handle;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        /* Snapshot the published value under the read-buffer lock and append it
         * only after the lock is released: port_ble_gatts_value_set() runs in
         * the caller's task and republishes this buffer, while this callback
         * runs on the NimBLE host task, and the mbuf work below must not happen
         * with the lock held. */
        uint8_t value[PORT_BLE_READ_BUF_MAX];
        uint16_t value_len = 0;

        BLE_LOG_INFO("Conn Handle(0x%02x), Read Char Handle(0x%02x)", conn_handle, attr_handle);
        gatt_event.type = CORE_BLE_GATT_EVT_READ_CHAR_VALUE;
        gatt_event.gatt_event.char_read.char_handle = attr_handle;
        gatt_event.gatt_event.char_read.offset = 0;

        if (port_bluetooth_gatt_callback) {
            port_bluetooth_gatt_callback(&gatt_event);
        }

        port_ble_read_buf_mutex_init();
        if (port_ble_read_buf_mutex) {
            xSemaphoreTake(port_ble_read_buf_mutex, portMAX_DELAY);
        }
        if (port_ble_server.read_char[0].handle == attr_handle && port_ble_server.read_char[0].buffer != NULL) {
            if (port_ble_server.read_char[0].size <= sizeof(value)) {
                value_len = port_ble_server.read_char[0].size;
                memcpy(value, port_ble_server.read_char[0].buffer, value_len);
            } else {
                BLE_LOG_WARN("read value (%u bytes) exceeds the snapshot buffer",
                             port_ble_server.read_char[0].size);
            }
        }
        if (port_ble_read_buf_mutex) {
            xSemaphoreGive(port_ble_read_buf_mutex);
        }

        if (value_len > 0) {
            os_mbuf_append(om, value, value_len);
        }
        /* No value for this handle (or nothing published yet) is answered with
         * an empty value and success: the TUYA read characteristic is only
         * meaningful after core has called gatts_value_set, and failing the ATT
         * read would be a protocol error for the peer. */
        return 0;
    }

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        while (om) {
            gatt_event.type = CORE_BLE_GATT_EVT_WRITE_REQ;
            gatt_event.conn_handle = conn_handle;
            gatt_event.gatt_event.write_report.char_handle = attr_handle;
            gatt_event.gatt_event.write_report.report.len = om->om_len;
            gatt_event.gatt_event.write_report.report.p_data = (uint8_t *)om->om_data;

            if (port_bluetooth_gatt_callback) {
                port_bluetooth_gatt_callback(&gatt_event);
            }
            om = SLIST_NEXT(om, om_next);
        }
        return 0;

    default:
        BLE_LOG_INFO("Unknown op = %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Host lifecycle
 * --------------------------------------------------------------------------- */
static void port_ble_host_stack_reset_callback(int reason)
{
    BLE_LOG_WARN("BLE host reset, reason = %d", reason);
}

static void port_ble_host_stack_sync_callback(void)
{
    BLE_LOG_INFO("BLE host sync");
    stack_sync_flag = 1;
}

static void port_ble_host_task(void *param)
{
    nimble_port_run();
    host_task_running = 0;
    nimble_port_freertos_deinit();
}

/* ---------------------------------------------------------------------------
 * Stack
 * --------------------------------------------------------------------------- */
OPERATE_RET port_ble_stack_init(uint8_t role)
{
    /* Already brought up and synced (the latch is only set from the sync
     * callback, which runs after the GATT services have been registered). */
    if (host_inited && stack_sync_flag) {
        BLE_LOG_INFO("ble stack already inited");
        port_ble_stack_event_callback(CORE_BLE_EVT_STACK_INIT, 0);
        return OPRT_OK;
    }

    port_ble_stack_mutex_init();
    port_ble_read_buf_mutex_init();
    xSemaphoreTake(port_ble_stack_mutex, portMAX_DELAY);

    if ((role & CORE_BLE_ROLE_SERVER) == CORE_BLE_ROLE_SERVER) {
        memset(&port_ble_server, 0, sizeof(PORT_BLUETOOTH_SERVER_PARAMS_T));
        port_ble_server.role = CORE_BLE_ROLE_SERVER;
        port_ble_server.read_char[0].buffer = NULL;
    }

    if (!host_inited) {
        esp_err_t err = nimble_port_init();
        if (err != ESP_OK) {
            BLE_LOG_ERR("nimble_port_init failed (%d)", err);
            xSemaphoreGive(port_ble_stack_mutex);
            return OPRT_OS_ADAPTER_BLE_INIT_FAILED;
        }

        /* Register the stack callbacks before the host task starts (the sync
         * event is reported from the host task). */
        ble_hs_cfg.reset_cb = port_ble_host_stack_reset_callback;
        ble_hs_cfg.sync_cb = port_ble_host_stack_sync_callback;
        ble_hs_cfg.store_status_cb = NULL;
        ble_svc_gap_init();
        ble_svc_gatt_init();

        host_inited = 1;

        /* Register the services the core handed over before stack_init. The
         * host structures exist now, and this must happen before the host task
         * starts: ble_gatts_start() (inside the host start) snapshots them. */
        if (gatts_pending) {
            OPERATE_RET gatt_ret = port_ble_gatts_register_cfg();
            if (gatt_ret != OPRT_OK) {
                BLE_LOG_ERR("gatt service register failed (%d)", gatt_ret);
                xSemaphoreGive(port_ble_stack_mutex);
                return OPRT_OS_ADAPTER_BLE_INIT_FAILED;
            }
        }
    }

    if (!host_task_running) {
        host_task_running = 1;
        nimble_port_freertos_init(port_ble_host_task);
    }

    /* Wait for the host sync. The latch is deliberately not cleared first: if a
     * previous attempt timed out, the host may have synced afterwards and the
     * latch already reports it. */
    int num = 0;
    while ((!stack_sync_flag) && (num++ < 50)) {
        port_ble_time_delay(10);
    }
    if (!stack_sync_flag) {
        /* Report the failure instead of a fake success: core would otherwise
         * start advertising against a stack that is not up yet (and would not
         * retry). No CORE_BLE_EVT_STACK_INIT is sent on this path. The host is
         * left brought up so that a later init can complete the sync. */
        BLE_LOG_ERR("BLE host sync timeout, stack init failed");
        xSemaphoreGive(port_ble_stack_mutex);
        return OPRT_OS_ADAPTER_BLE_INIT_FAILED;
    }

    port_ble_stack_event_callback(CORE_BLE_EVT_STACK_INIT, 0);
    xSemaphoreGive(port_ble_stack_mutex);
    return OPRT_OK;
}

static void port_ble_gatts_uuid_free(void)
{
    int i, j;

    if (port_gatt_svcs) {
        for (i = 0; i < CORE_BLE_GATT_SERVICE_MAX_NUM; i++) {
            if (port_gatt_svcs[i].uuid) {
                free((void *)port_gatt_svcs[i].uuid);
                port_gatt_svcs[i].uuid = NULL;
            }
        }
    }

    for (i = 0; i < PORT_BLE_GATT_SERVICE_MAX_NUM; i++) {
        for (j = 0; j < PORT_BLE_GATT_CHAR_MAX_NUM; j++) {
            if (port_gatt_chars[i][j].uuid) {
                free((void *)port_gatt_chars[i][j].uuid);
                port_gatt_chars[i][j].uuid = NULL;
            }
        }
    }
}

OPERATE_RET port_ble_stack_deinit(uint8_t role)
{
    if (!host_inited) {
        BLE_LOG_INFO("ble stack already deinited");
        port_ble_stack_event_callback(CORE_BLE_EVT_STACK_DEINIT, 0);
        return OPRT_OK;
    }

    port_ble_stack_mutex_init();
    xSemaphoreTake(port_ble_stack_mutex, portMAX_DELAY);

    if (host_task_running) {
        int rc = nimble_port_stop();
        if (rc != 0) {
            /* The graceful stop is not possible (e.g. the host never synced, so
             * ble_hs_stop() reports "already stopped" and nimble_port_stop()
             * never posts the event that ends nimble_port_run()). Leaving the
             * stack up is not an option either: core frees its characteristic
             * table before calling this function, so a host that syncs later
             * would write the attribute handles through the freed pointers.
             * Delete the host task instead (it is parked in the default event
             * queue that nimble_port_deinit() is about to tear down) and
             * continue with the full teardown. */
            BLE_LOG_ERR("nimble_port_stop failed (%d), forcing host task shutdown", rc);
            nimble_port_freertos_deinit();
            host_task_running = 0;
            port_ble_time_delay(20);
        } else {
            /* The host task leaves nimble_port_run() and deletes itself in
             * nimble_port_freertos_deinit(); give it a moment before tearing
             * the host structures down. */
            int wait = 0;
            while (host_task_running && wait++ < 50) {
                port_ble_time_delay(2);
            }
            port_ble_time_delay(20);
        }
    }

    /* host_task_running is only cleared by the stop above, so reaching this
     * point means the host task is gone (or was never created). */
    nimble_port_deinit();
    host_task_running = 0;
    host_inited = 0;
    stack_sync_flag = 0;
    gatts_service_flag = FALSE;
    gatts_pending = FALSE;

    port_ble_stack_event_callback(CORE_BLE_EVT_STACK_DEINIT, 0);

    port_ble_gatts_uuid_free();
    if (port_gatt_svcs) {
        free((void *)port_gatt_svcs);
        port_gatt_svcs = NULL;
    }

    /* Unpublish the read buffer under the same lock the READ callback uses, so
     * a callback that is already copying it can never see it freed. */
    port_ble_read_buf_mutex_init();
    if (port_ble_read_buf_mutex) {
        xSemaphoreTake(port_ble_read_buf_mutex, portMAX_DELAY);
    }
    if (port_ble_server.read_char[0].buffer) {
        free(port_ble_server.read_char[0].buffer);
        port_ble_server.read_char[0].buffer = NULL;
    }
    port_ble_server.read_char[0].size = 0;
    port_ble_server.read_char[0].handle = 0;
    if (port_ble_read_buf_mutex) {
        xSemaphoreGive(port_ble_read_buf_mutex);
    }

    gatts_service_flag = FALSE;

    xSemaphoreGive(port_ble_stack_mutex);
    return OPRT_OK;
}

OPERATE_RET port_ble_stack_gatt_link(uint16_t *p_link)
{
    *p_link = 4;
    return OPRT_OK;
}

OPERATE_RET port_ble_gap_callback_register(const CORE_BLE_GAP_EVT_FUNC_CB gap_evt)
{
    port_bluetooth_gap_callback = gap_evt;
    return OPRT_OK;
}

OPERATE_RET port_ble_gatt_callback_register(const CORE_BLE_GATT_EVT_FUNC_CB gatt_evt)
{
    port_bluetooth_gatt_callback = gatt_evt;
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * GAP
 * --------------------------------------------------------------------------- */
/* Address convention for every CORE_BLE_GAP_ADDR_T in this file: big-endian,
 * i.e. addr[0] is the most significant byte and the bytes read like the
 * human-readable MAC. NimBLE keeps ble_addr_t.val[] little-endian (see
 * ble_hs_id.h: "specified in host byte order (little-endian!)"), so every
 * crossing point converts explicitly. */
OPERATE_RET port_ble_gap_addr_set(CORE_BLE_GAP_ADDR_T const *p_peer_addr)
{
    uint8_t rnd[6];
    uint8_t i;

    if (!p_peer_addr) {
        return OPRT_INVALID_PARM;
    }
    if (p_peer_addr->type == CORE_BLE_GAP_ADDR_TYPE_RANDOM) {
        for (i = 0; i < 6; i++) {
            rnd[i] = p_peer_addr->addr[5 - i];
        }
        return (ble_hs_id_set_rnd(rnd) == 0) ? OPRT_OK : OPRT_COM_ERROR;
    }
    /* Public address is fixed by the controller (factory MAC). */
    return OPRT_OK;
}

OPERATE_RET port_ble_gap_address_get(CORE_BLE_GAP_ADDR_T *p_peer_addr)
{
    uint8_t addr[6];
    int rc;

    if (!p_peer_addr) {
        return OPRT_INVALID_PARM;
    }

    rc = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr, NULL);
    if (rc != 0) {
        return OPRT_COM_ERROR;
    }

    for (uint8_t i = 0; i < 6; i++) {
        p_peer_addr->addr[i] = addr[5 - i];
    }
    p_peer_addr->type = CORE_BLE_GAP_ADDR_TYPE_PUBLIC;
    return OPRT_OK;
}

OPERATE_RET port_ble_gap_adv_start(CORE_BLE_GAP_ADV_PARAMS_T const *p_adv_params)
{
    struct ble_gap_adv_params adv_params;
    int rc;

    if (!ble_hs_is_enabled()) {
        BLE_LOG_INFO("bt stack close, bt operation invalid");
        return OPRT_OK;
    }
    if (!p_adv_params) {
        return OPRT_INVALID_PARM;
    }

    memset(&adv_params, 0, sizeof(adv_params));

    if (p_adv_params->adv_type == CORE_BLE_GAP_ADV_TYPE_CONN_SCANNABLE_UNDIRECTED) {
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    } else if (p_adv_params->adv_type == CORE_BLE_GAP_ADV_TYPE_NONCONN_SCANNABLE_UNDIRECTED) {
        adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    } else {
        return OPRT_INVALID_PARM;
    }

    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(p_adv_params->adv_interval_min);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(p_adv_params->adv_interval_max);
    rc = ble_gap_adv_start(BLE_HCI_ADV_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params,
                           port_ble_host_gap_event, &port_ble_server);
    if (rc != 0) {
        BLE_LOG_ERR("error enabling advertisement; rc=%d", rc);
        return OPRT_OS_ADAPTER_BLE_ADV_START_FAILED;
    }

    return OPRT_OK;
}

OPERATE_RET port_ble_gap_adv_stop(void)
{
    if (!ble_hs_is_enabled()) {
        BLE_LOG_INFO("bt stack close, bt operation invalid");
        return OPRT_OK;
    }

    if (ble_gap_adv_stop() != 0) {
        return OPRT_OS_ADAPTER_BLE_ADV_STOP_FAILED;
    }
    return OPRT_OK;
}

OPERATE_RET port_ble_gap_adv_rsp_data_set(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp)
{
    int rc;

    if (!ble_hs_is_enabled()) {
        BLE_LOG_INFO("bt stack close, bt operation invalid");
        return OPRT_OK;
    }

    if (p_adv && p_adv->p_data != NULL) {
        rc = ble_gap_adv_set_data(p_adv->p_data, p_adv->len);
        if (rc != 0) {
            return OPRT_OS_ADAPTER_BLE_ADV_START_FAILED;
        }
    }

    if (p_scan_rsp && p_scan_rsp->p_data != NULL) {
        rc = ble_gap_adv_rsp_set_data(p_scan_rsp->p_data, p_scan_rsp->len);
        if (rc != 0) {
            return OPRT_OS_ADAPTER_BLE_ADV_START_FAILED;
        }
    }
    return OPRT_OK;
}

OPERATE_RET port_ble_gap_adv_rsp_data_update(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp)
{
    return port_ble_gap_adv_rsp_data_set(p_adv, p_scan_rsp);
}

OPERATE_RET port_ble_gap_disconnect(uint16_t conn_handle, uint8_t hci_reason)
{
    int rc;

    if (!ble_hs_is_enabled()) {
        BLE_LOG_INFO("bt stack close, bt operation invalid");
        return OPRT_OK;
    }

    rc = ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        BLE_LOG_ERR("failed to disconnect; rc=%d", rc);
        return OPRT_OS_ADAPTER_BLE_GATT_DISCONN_FAILED;
    }
    return OPRT_OK;
}

OPERATE_RET port_ble_gap_conn_param_update(uint16_t conn_handle, CORE_BLE_GAP_CONN_PARAMS_T const *p_conn_params)
{
    struct ble_gap_upd_params conn_params;
    int rc;

    if (!ble_hs_is_enabled()) {
        BLE_LOG_INFO("bt stack close, bt operation invalid");
        return OPRT_OK;
    }
    if (!p_conn_params) {
        return OPRT_INVALID_PARM;
    }

    memset(&conn_params, 0, sizeof(conn_params));
    conn_params.itvl_min = p_conn_params->conn_interval_min;
    conn_params.itvl_max = p_conn_params->conn_interval_max;
    conn_params.latency = p_conn_params->conn_latency;
    conn_params.supervision_timeout = p_conn_params->conn_sup_timeout;

    rc = ble_gap_update_params(conn_handle, &conn_params);
    if (rc != 0) {
        BLE_LOG_ERR("failed to update connection parameters; rc=%d", rc);
        return OPRT_OS_ADAPTER_BLE_CONN_PARAM_UPDATE_FAILED;
    }
    return OPRT_OK;
}

OPERATE_RET port_ble_gap_rssi_get(uint16_t conn_handle)
{
    CORE_BLE_GAP_PARAMS_EVT_T gap_event;
    int8_t rssi;
    int rc;

    if (!ble_hs_is_enabled()) {
        BLE_LOG_INFO("bt stack close, bt operation invalid");
        return OPRT_OK;
    }

    rc = ble_gap_conn_rssi(conn_handle, &rssi);
    if (rc != 0) {
        BLE_LOG_ERR("failed to read connection rssi; rc=%d", rc);
        return OPRT_OS_ADAPTER_BLE_CONN_RSSI_GET_FAILED;
    }

    memset(&gap_event, 0, sizeof(gap_event));
    gap_event.type = CORE_BLE_GAP_EVT_CONN_RSSI;
    gap_event.result = 0;
    gap_event.conn_handle = conn_handle;
    gap_event.gap_event.link_rssi = rssi;

    if (port_bluetooth_gap_callback && gap_event.conn_handle != CORE_BLE_GATT_INVALID_HANDLE) {
        port_bluetooth_gap_callback(&gap_event);
    }
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * GATT server
 * --------------------------------------------------------------------------- */
static OPERATE_RET port_ble_gatts_register_cfg(void)
{
    int rc;

    rc = ble_gatts_count_cfg(port_gatt_svcs);
    if (rc != 0) {
        BLE_LOG_ERR("ble_gatts_count_cfg failed; rc=%d", rc);
        return OPRT_INVALID_PARM;
    }

    rc = ble_gatts_add_svcs(port_gatt_svcs);
    if (rc != 0) {
        BLE_LOG_ERR("ble_gatts_add_svcs failed; rc=%d", rc);
        return OPRT_INVALID_PARM;
    }

    gatts_service_flag = TRUE;
    gatts_pending = FALSE;
    return OPRT_OK;
}

OPERATE_RET port_ble_gatts_service_add(CORE_BLE_GATTS_PARAMS_T *p_service)
{
    unsigned char i = 0, j = 0;
    unsigned char type = 0;
    OPERATE_RET ret = OPRT_OK;

    if (gatts_service_flag == TRUE) {
        return OPRT_OK;
    }
    if (!p_service) {
        return OPRT_INVALID_PARM;
    }

    if (p_service->svc_num > CORE_BLE_GATT_SERVICE_MAX_NUM || p_service->svc_num > PORT_BLE_GATT_SERVICE_MAX_NUM) {
        return OPRT_INVALID_PARM;
    }

    if (!port_gatt_svcs) {
        /* Zero-initialized: port_ble_gatts_uuid_free() below walks the uuid
         * pointers, so a plain malloc() would free uninitialized garbage on
         * the first call (heap corruption). */
        port_gatt_svcs = calloc(CORE_BLE_GATT_SERVICE_MAX_NUM, sizeof(struct ble_gatt_svc_def));
        if (!port_gatt_svcs) {
            return OPRT_MALLOC_FAILED;
        }
    }
    port_ble_gatts_uuid_free();
    memset(port_gatt_svcs, 0, CORE_BLE_GATT_SERVICE_MAX_NUM * sizeof(struct ble_gatt_svc_def));
    memset(port_gatt_chars, 0, sizeof(port_gatt_chars));

    for (i = 0; i < p_service->svc_num; i++) {
        if (p_service->p_service[i].type == CORE_BLE_UUID_SERVICE_PRIMARY) {
            port_gatt_svcs[i].type = BLE_GATT_SVC_TYPE_PRIMARY;
        } else if (p_service->p_service[i].type == CORE_BLE_UUID_SERVICE_SECONDARY) {
            port_gatt_svcs[i].type = BLE_GATT_SVC_TYPE_SECONDARY;
        } else {
            ret = OPRT_INVALID_PARM;
            goto exit_cleanup;
        }

        if (p_service->p_service[i].svc_uuid.uuid_type == CORE_BLE_UUID_TYPE_16) {
            port_gatt_svcs[i].uuid = (ble_uuid_t *)malloc(sizeof(ble_uuid16_t));
            if (!port_gatt_svcs[i].uuid) {
                ret = OPRT_MALLOC_FAILED;
                goto exit_cleanup;
            }
            memcpy((uint8_t *)port_gatt_svcs[i].uuid,
                   (uint8_t *)BLE_UUID16_DECLARE(p_service->p_service[i].svc_uuid.uuid.uuid16),
                   sizeof(ble_uuid16_t));
        } else if (p_service->p_service[i].svc_uuid.uuid_type == CORE_BLE_UUID_TYPE_32) {
            port_gatt_svcs[i].uuid = (ble_uuid_t *)malloc(sizeof(ble_uuid32_t));
            if (!port_gatt_svcs[i].uuid) {
                ret = OPRT_MALLOC_FAILED;
                goto exit_cleanup;
            }
            memcpy((uint8_t *)port_gatt_svcs[i].uuid,
                   (uint8_t *)BLE_UUID32_DECLARE(p_service->p_service[i].svc_uuid.uuid.uuid32),
                   sizeof(ble_uuid32_t));
        } else if (p_service->p_service[i].svc_uuid.uuid_type == CORE_BLE_UUID_TYPE_128) {
            port_gatt_svcs[i].uuid = (ble_uuid_t *)malloc(sizeof(ble_uuid128_t));
            if (!port_gatt_svcs[i].uuid) {
                ret = OPRT_MALLOC_FAILED;
                goto exit_cleanup;
            }
            type = BLE_UUID_TYPE_128;
            memcpy((uint8_t *)port_gatt_svcs[i].uuid, &type, 1);
            memcpy((uint8_t *)port_gatt_svcs[i].uuid + 1, p_service->p_service[i].svc_uuid.uuid.uuid128,
                   sizeof(ble_uuid128_t) - 1);
        } else {
            ret = OPRT_INVALID_PARM;
            goto exit_cleanup;
        }

        if (p_service->p_service[i].char_num > PORT_BLE_GATT_CHAR_MAX_NUM) {
            ret = OPRT_INVALID_PARM;
            goto exit_cleanup;
        }

        CORE_BLE_CHAR_PARAMS_T *p_char = p_service->p_service[i].p_char;
        if (p_service->p_service[i].char_num > 0 && p_char == NULL) {
            ret = OPRT_INVALID_PARM;
            goto exit_cleanup;
        }

        /* ble_gatt_svc_def.characteristics is const-qualified in esp-idf's
         * NimBLE: fill the mutable row first, then publish it by pointer. */
        struct ble_gatt_chr_def *chars = port_gatt_chars[i];
        port_gatt_svcs[i].characteristics = chars;

        for (j = 0; j < p_service->p_service[i].char_num; j++) {
            if (p_char[j].char_uuid.uuid_type == CORE_BLE_UUID_TYPE_16) {
                chars[j].uuid = (ble_uuid_t *)malloc(sizeof(ble_uuid16_t));
                if (!chars[j].uuid) {
                    ret = OPRT_MALLOC_FAILED;
                    goto exit_cleanup;
                }
                memcpy((uint8_t *)chars[j].uuid,
                       (uint8_t *)BLE_UUID16_DECLARE(p_char[j].char_uuid.uuid.uuid16), sizeof(ble_uuid16_t));
            } else if (p_char[j].char_uuid.uuid_type == CORE_BLE_UUID_TYPE_32) {
                chars[j].uuid = (ble_uuid_t *)malloc(sizeof(ble_uuid32_t));
                if (!chars[j].uuid) {
                    ret = OPRT_MALLOC_FAILED;
                    goto exit_cleanup;
                }
                memcpy((uint8_t *)chars[j].uuid,
                       (uint8_t *)BLE_UUID32_DECLARE(p_char[j].char_uuid.uuid.uuid32), sizeof(ble_uuid32_t));
            } else if (p_char[j].char_uuid.uuid_type == CORE_BLE_UUID_TYPE_128) {
                chars[j].uuid = (ble_uuid_t *)malloc(sizeof(ble_uuid128_t));
                if (!chars[j].uuid) {
                    ret = OPRT_MALLOC_FAILED;
                    goto exit_cleanup;
                }
                type = BLE_UUID_TYPE_128;
                memcpy((uint8_t *)chars[j].uuid, &type, 1);
                memcpy((uint8_t *)chars[j].uuid + 1, p_char[j].char_uuid.uuid.uuid128,
                       sizeof(ble_uuid128_t) - 1);
            } else {
                ret = OPRT_INVALID_PARM;
                goto exit_cleanup;
            }

            chars[j].access_cb = port_ble_host_write_callback;
            chars[j].val_handle = &p_char[j].handle;

            if ((p_char[j].property & CORE_BLE_GATT_CHAR_PROP_WRITE_NO_RSP) == CORE_BLE_GATT_CHAR_PROP_WRITE_NO_RSP) {
                chars[j].flags |= BLE_GATT_CHR_F_WRITE_NO_RSP;
            }
            if ((p_char[j].property & CORE_BLE_GATT_CHAR_PROP_WRITE) == CORE_BLE_GATT_CHAR_PROP_WRITE) {
                chars[j].flags |= BLE_GATT_CHR_F_WRITE;
            }
            if ((p_char[j].property & CORE_BLE_GATT_CHAR_PROP_NOTIFY) == CORE_BLE_GATT_CHAR_PROP_NOTIFY) {
                chars[j].flags |= BLE_GATT_CHR_F_NOTIFY;
            }
            if ((p_char[j].property & CORE_BLE_GATT_CHAR_PROP_INDICATE) == CORE_BLE_GATT_CHAR_PROP_INDICATE) {
                chars[j].flags |= BLE_GATT_CHR_F_INDICATE;
            }
            if ((p_char[j].property & CORE_BLE_GATT_CHAR_PROP_READ) == CORE_BLE_GATT_CHAR_PROP_READ) {
                chars[j].flags |= BLE_GATT_CHR_F_READ;
            }
        }
    }

    if (host_inited) {
        ret = port_ble_gatts_register_cfg();
        if (ret != OPRT_OK) {
            goto exit_cleanup;
        }
    } else {
        /* The NimBLE host does not exist yet (core registers the services
         * before stack_init); port_ble_stack_init() registers them. */
        gatts_pending = TRUE;
    }
    return OPRT_OK;

exit_cleanup:
    port_ble_gatts_uuid_free();
    memset(port_gatt_svcs, 0, CORE_BLE_GATT_SERVICE_MAX_NUM * sizeof(struct ble_gatt_svc_def));
    memset(port_gatt_chars, 0, sizeof(port_gatt_chars));
    return ret;
}

OPERATE_RET port_ble_gatts_value_set(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    uint8_t *new_buffer;
    uint8_t *old_buffer;

    if (!ble_hs_is_enabled()) {
        BLE_LOG_INFO("bt stack close, bt operation invalid");
        return OPRT_OK;
    }
    if (!p_data || length == 0) {
        return OPRT_INVALID_PARM;
    }

    /* Build the new value before touching the published one: the READ callback
     * runs on the host task and must never observe a freed buffer or a length
     * that does not belong to the buffer it reads. */
    new_buffer = malloc(length);
    if (!new_buffer) {
        return OPRT_MALLOC_FAILED;
    }
    memcpy(new_buffer, p_data, length);

    port_ble_read_buf_mutex_init();
    if (port_ble_read_buf_mutex) {
        xSemaphoreTake(port_ble_read_buf_mutex, portMAX_DELAY);
    }
    old_buffer = port_ble_server.read_char[0].buffer;
    port_ble_server.read_char[0].buffer = new_buffer;
    port_ble_server.read_char[0].size = length;
    port_ble_server.read_char[0].handle = char_handle;
    if (port_ble_read_buf_mutex) {
        xSemaphoreGive(port_ble_read_buf_mutex);
    }

    /* Unpublished under the lock above, so no reader can still be using it. */
    free(old_buffer);

    return OPRT_OK;
}

static int port_ble_hs_notify(uint16_t conn_handle, uint16_t attr_handle, uint8_t *p_data, uint16_t length)
{
    struct os_mbuf *om;

    om = ble_hs_mbuf_from_flat(p_data, length);
    if (om == NULL) {
        /* The mbuf pool can be momentarily exhausted under a fast notify
         * stream; retry once after a short delay (same policy as the upstream
         * adaptation). */
        port_ble_time_delay(50);
        om = ble_hs_mbuf_from_flat(p_data, length);
        if (om == NULL) {
            return OPRT_OS_ADAPTER_BLE_NOTIFY_FAILED;
        }
    }

    if (ble_gattc_notify_custom(conn_handle, attr_handle, om) != 0) {
        return OPRT_OS_ADAPTER_BLE_NOTIFY_FAILED;
    }
    return OPRT_OK;
}

OPERATE_RET port_ble_gatts_value_notify(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    if (!ble_hs_is_enabled()) {
        BLE_LOG_INFO("bt stack close, bt operation invalid");
        return OPRT_OK;
    }
    return port_ble_hs_notify(conn_handle, char_handle, p_data, length);
}

OPERATE_RET port_ble_gatts_exchange_mtu_reply(uint16_t conn_handle, uint16_t server_rx_mtu)
{
    if (!ble_hs_is_enabled()) {
        BLE_LOG_INFO("bt stack close, bt operation invalid");
        return OPRT_OK;
    }

    /* The NimBLE host replies to the ATT MTU exchange automatically; only the
     * preferred local MTU needs to be recorded here. */
    ble_att_set_preferred_mtu(server_rx_mtu);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Platform ops card: core's core_bluetooth.c references it via extern and
 * dispatches core_ble_*. Peripheral-only: every remaining member is bound.
 * --------------------------------------------------------------------------- */
CORE_BLUETOOTH_OPS_T core_bluetooth_ops_platform = {
    /* stack */
    .stack_init              = port_ble_stack_init,
    .stack_deinit            = port_ble_stack_deinit,
    .stack_gatt_link         = port_ble_stack_gatt_link,
    /* Event registration */
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

#else /* !defined(ENABLE_BLUETOOTH) || (ENABLE_BLUETOOTH == 0) */


/* Fallback card for builds without BLE: every member is NULL, so core's
 * dispatch degrades every BLE call to OPRT_NOT_SUPPORTED. */
CORE_BLUETOOTH_OPS_T core_bluetooth_ops_platform = {
    /* stack */
    .stack_init               = NULL,
    .stack_deinit             = NULL,
    .stack_gatt_link          = NULL,
    /* event registration */
    .gap_callback_register    = NULL,
    .gatt_callback_register   = NULL,
    /* GAP */
    .gap_addr_set             = NULL,
    .gap_address_get          = NULL,
    .gap_adv_start            = NULL,
    .gap_adv_stop             = NULL,
    .gap_adv_rsp_data_set     = NULL,
    .gap_adv_rsp_data_update  = NULL,
    .gap_disconnect           = NULL,
    .gap_conn_param_update    = NULL,
    .gap_rssi_get             = NULL,
    /* GATT server */
    .gatts_service_add        = NULL,
    .gatts_value_set          = NULL,
    .gatts_value_notify       = NULL,
    .gatts_exchange_mtu_reply = NULL,
};

#endif /* defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1) */
