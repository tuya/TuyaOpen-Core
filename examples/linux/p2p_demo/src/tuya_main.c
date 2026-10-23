/**
 * @file tuya_main.c
 * @brief p2p_demo application entry: cloud connection plus P2P hookup.
 *
 * Startup chain:
 *   - Runtime initialization: core runtime init (time service, log, kv,
 *     sw_timer, workq), compile-time credentials (tuya_config.h or -D
 *     injection), core_netif link status callback, mqtt_bind direct binding with
 *     QR code printing, and DP echo reporting;
 *   - P2P hookup: OnIotInited() assembles the activation skill parameters ->
 *     tuya_iot_init(..., .skill_param = gw_active_get_ext_param()) ->
 *     TUYA_EVENT_RTC_REQ -> gw_p2p_mqtt_data_cb() (cloud signaling entry) ->
 *     a thread started after MQTT connects fills the ten TUYA_IPC_SDK_VAR_S
 *     callbacks -> TUYA_APP_Start() -> tuya_ipc_demo_start() (media chain:
 *     frame queue / ring buffer / vcam / local_store);
 *   - main() runs user_main() directly (the Linux main thread stack is 8MB and
 *     P2P signaling runs on tuya_iot_yield). On non-Linux targets user_main()
 *     runs from the tuya_app_main thread with an enlarged stack for pj/ICE.
 *
 * The event handler starts the P2P media thread once on MQTT connect (core has
 * already started the LAN service by then), forwards RTC signaling, and echoes
 * received OBJ/RAW DPs.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "cJSON.h"
#include "mqtt_bind.h"
#include "core_api.h"
#include "core_network.h"
#include "core_netif.h"
#include "core_api_ops.h"
#include "tuya_config.h"
#include "tuya_iot.h"
#include "tuya_iot_dp.h"
#include "tuya_lan.h"
#include "tuya_p2p_sdk.h"
#include "tuya_ipc_demo.h"
#include "reset_netcfg.h"
#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1)
#include "ble_mgr.h"
#include "ble_netcfg.h"
#endif
#include <assert.h>
#include <stdio.h>

#if defined(ENABLE_QRCODE) && (ENABLE_QRCODE == 1)
#include "qrencode_print.h"
#endif

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

/* P2P media thread handle: started once after MQTT connects; a non-NULL value prevents a second creation */
static THREAD_HANDLE hIpcDemoHandle = NULL;
static void          tuya_ipc_demo_thread(void *arg);

/* Tuya device handle */
tuya_iot_client_t client;

/* Tuya license information (uuid authkey) */
tuya_iot_license_t license;

/**
 * @brief user defined log output api, in this demo it will use uart0 as log-tx
 *
 * @param str log string
 * @return void
 */
void user_log_output_cb(const char *str)
{
    CORE_API_OPS_T *ops = core_api_get_ops();
    if (NULL != ops && ops->log_output) {
        ops->log_output(str);
    }
}

/**
 * @brief user defined upgrade notify callback, it will notify device a OTA
 * request received
 *
 * @param client device info
 * @param upgrade the upgrade request info
 * @return void
 */
void user_upgrade_notify_on(tuya_iot_client_t *client, cJSON *upgrade)
{
    PR_INFO("----- Upgrade information -----");
    if (!upgrade) {
        PR_WARN("upgrade JSON is NULL");
        return;
    }

    cJSON *type_item    = cJSON_GetObjectItem(upgrade, "type");
    cJSON *version_item = cJSON_GetObjectItem(upgrade, "version");
    cJSON *size_item    = cJSON_GetObjectItem(upgrade, "size");
    cJSON *md5_item     = cJSON_GetObjectItem(upgrade, "md5");
    cJSON *hmac_item    = cJSON_GetObjectItem(upgrade, "hmac");
    cJSON *url_item     = cJSON_GetObjectItem(upgrade, "url");
    cJSON *https_item   = cJSON_GetObjectItem(upgrade, "httpsUrl");

    PR_INFO("OTA Channel: %d", cJSON_IsNumber(type_item) ? type_item->valueint : -1);
    PR_INFO("Version: %s", cJSON_IsString(version_item) ? version_item->valuestring : "N/A");
    PR_INFO("Size: %s", cJSON_IsString(size_item) ? size_item->valuestring : "N/A");
    PR_INFO("MD5: %s", cJSON_IsString(md5_item) ? md5_item->valuestring : "N/A");
    PR_INFO("HMAC: %s", cJSON_IsString(hmac_item) ? hmac_item->valuestring : "N/A");
    PR_INFO("URL: %s", cJSON_IsString(url_item) ? url_item->valuestring : "N/A");
    PR_INFO("HTTPS URL: %s", cJSON_IsString(https_item) ? https_item->valuestring : "N/A");
}

/**
 * @brief user defined event handler
 *
 * @param client device info
 * @param event the event info
 * @return void
 */
void user_event_handler_on(tuya_iot_client_t *client, tuya_event_msg_t *event)
{
    PR_DEBUG("Tuya Event ID:%d(%s)", event->id, EVENT_ID2STR(event->id));
    switch (event->id) {
    case TUYA_EVENT_BIND_START:
        PR_INFO("Device Bind Start!");
        break;

    /* Print the QRCode for Tuya APP bind */
    case TUYA_EVENT_DIRECT_MQTT_CONNECTED: {
#if defined(ENABLE_QRCODE) && (ENABLE_QRCODE == 1)
        char buffer[255];
        sprintf(buffer, "https://smartapp.tuya.com/s/p?p=%s&uuid=%s&v=2.0", TUYA_PRODUCT_ID, license.uuid);
        qrcode_string_output(buffer, user_log_output_cb, 0);
#endif
    } break;

    /* Activated device connected to the cloud MQTT: start the P2P media thread
     * (core has already enabled the LAN service at this point) */
    case TUYA_EVENT_MQTT_CONNECTED: {
        PR_INFO("Device MQTT Connected!");
        if (NULL != hIpcDemoHandle) {
            break;
        }
        /* A 4KB stack overflows in pj/ICE, hence 5*4096 */
        THREAD_CFG_T thrd_param = {4096 * 5, 4, "tuya_ipc_demo_thread"};
        core_thread_create_and_start(&hIpcDemoHandle, NULL, NULL, tuya_ipc_demo_thread, NULL, &thrd_param);
    } break;

    /* Cloud MQTT downlink RTC signaling -> P2P (P2P signaling entry) */
    case TUYA_EVENT_RTC_REQ: {
        cJSON *root_json = event->value.asJSON;
        gw_p2p_mqtt_data_cb(root_json);
    } break;

    /* RECV upgrade request */
    case TUYA_EVENT_UPGRADE_NOTIFY:
        user_upgrade_notify_on(client, event->value.asJSON);
        break;

    /* Sync time with tuya Cloud */
    case TUYA_EVENT_TIMESTAMP_SYNC:
        PR_INFO("Sync timestamp:%d", event->value.asInteger);
        core_time_set_posix(event->value.asInteger, 1);
        break;

    case TUYA_EVENT_RESET: {
        tuya_reset_type_t reset_type = (tuya_reset_type_t)event->value.asInteger;
        PR_INFO("Device Reset:%d", reset_type);

        // TUYA_RESET_TYPE_FACTORY, TUYA_RESET_TYPE_REMOTE_FACTORY, TUYA_RESET_TYPE_DATA_FACTORY
        // Need remove the device application data from the kv store
    } break;

    case TUYA_EVENT_RESET_COMPLETE: {
        PR_INFO("Device Reset Complete!");

        // Restart the device
        core_system_reset();
    } break;

    /* RECV OBJ DP */
    case TUYA_EVENT_DP_RECEIVE_OBJ: {
        dp_obj_recv_t *dpobj = event->value.dpobj;
        PR_DEBUG("SOC Rev DP Cmd t1:%d t2:%d CNT:%u", dpobj->cmd_tp, dpobj->dtt_tp, dpobj->dpscnt);
        if (dpobj->devid != NULL) {
            PR_DEBUG("devid.%s", dpobj->devid);
        }

        uint32_t index = 0;
        for (index = 0; index < dpobj->dpscnt; index++) {
            dp_obj_t *dp = dpobj->dps + index;
            PR_DEBUG("idx:%d dpid:%d type:%d ts:%u", index, dp->id, dp->type, dp->time_stamp);
            switch (dp->type) {
            case PROP_BOOL: {
                PR_DEBUG("bool value:%d", dp->value.dp_bool);
                break;
            }
            case PROP_VALUE: {
                PR_DEBUG("int value:%d", dp->value.dp_value);
                break;
            }
            case PROP_STR: {
                PR_DEBUG("str value:%s", dp->value.dp_str);
                break;
            }
            case PROP_ENUM: {
                PR_DEBUG("enum value:%u", dp->value.dp_enum);
                break;
            }
            case PROP_BITMAP: {
                PR_DEBUG("bits value:0x%X", dp->value.dp_bitmap);
                break;
            }
            default: {
                PR_ERR("idx:%d dpid:%d type:%d ts:%u is invalid", index, dp->id, dp->type, dp->time_stamp);
                break;
            }
            } // end of switch
        }

        tuya_iot_dp_obj_report(client, dpobj->devid, dpobj->dps, dpobj->dpscnt, 0);

    } break;

    /* RECV RAW DP */
    case TUYA_EVENT_DP_RECEIVE_RAW: {
        dp_raw_recv_t *dpraw = event->value.dpraw;
        PR_DEBUG("SOC Rev DP Cmd t1:%d t2:%d", dpraw->cmd_tp, dpraw->dtt_tp);
        if (dpraw->devid != NULL) {
            PR_DEBUG("devid.%s", dpraw->devid);
        }

        uint32_t  index = 0;
        dp_raw_t *dp    = &dpraw->dp;
        PR_DEBUG("dpid:%d type:RAW len:%d data:", dp->id, dp->len);
        for (index = 0; index < dp->len; index++) {
            PR_DEBUG_RAW("%02x", dp->data[index]);
        }

        tuya_iot_dp_raw_report(client, dpraw->devid, &dpraw->dp, 3);

    } break;

        /* TBD.. add other event if necessary */

    default:
        break;
    }
}

/* Link status: maintained by the port layer (port_netif) status callback and
 * pulled by the core through user_network_check */
static volatile bool s_link_up = false;

/**
 * @brief port link status callback: port_netif reports the current status
 * immediately after registration, then polls periodically and calls back again
 * whenever the status changes
 *
 * @param stat current link status
 * @return void
 */
static void __user_link_status_cb(CORE_NETIF_LINK_STAT_E stat)
{
    s_link_up = (CORE_NETIF_LINK_UP == stat);

    /* Sync the active IP: link up does not mean the IP is ready (port_netif only
     * checks IFF_UP, which does not imply an address was obtained). Both the source
     * address binding of outbound sockets and the local IP in LAN broadcast packets
     * come from this value; 0 means no binding */
    TUYA_IP_ADDR_T addr = 0;
    if (s_link_up) {
        NW_IP_S ip = {0};
        if (OPRT_OK == core_netif_get_ip(&ip) && ip.ip[0] != '\0') {
            addr = core_net_str2addr(ip.ip);
        }
    }
    core_network_set_active_ip(addr);

    PR_NOTICE("link status changed to %d, link %s", stat, s_link_up ? "UP" : "DOWN");
}

/**
 * @brief user defined network check callback, it will check the network every
 * 1sec by reading the link status maintained by the port layer
 *
 * @return true
 * @return false
 */
bool user_network_check(void)
{
    return s_link_up;
}

#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1)
/* ---------------------------------------------------------------------------
 * Optional BLE provisioning (CONFIG_ENABLE_BLUETOOTH, off by default)
 *
 * With the switch on, an unbound device receives its WiFi credentials and the
 * binding token from the Tuya APP over BLE instead of the direct-MQTT QR flow.
 * This Linux demo runs on a wired link, so the finish callback only prints the
 * received data: it does not join a WiFi network and does not publish the
 * binding token, so the client stays in the token-pending state. Completing
 * the flow later means publishing EVENT_LINK_ACTIVATE with the token
 * (region | token | regist_key), as the ESP32 demo does.
 * --------------------------------------------------------------------------- */

/**
 * @brief Bring up the Tuya BLE stack (idempotent)
 */
static void __ble_stack_start(void)
{
    tuya_ble_cfg_t cfg = {0};

    cfg.client = &client;
    snprintf(cfg.device_name, sizeof(cfg.device_name), "TYBLE");

    if (OPRT_OK != tuya_ble_init(&cfg)) {
        PR_ERR("BLE stack init failed; BLE provisioning unavailable");
    }
}

/**
 * @brief BLE netcfg finish callback: print only (see the block comment above)
 */
static int __ble_netcfg_finish(ble_netcfg_info_t *info)
{
    if (NULL == info) {
        PR_ERR("BLE netcfg: empty payload");
        return OPRT_INVALID_PARM;
    }

    PR_NOTICE("BLE netcfg: ssid=\"%s\", passwd_len=%u, token=\"%.*s\"",
              (const char *)info->ssid, (unsigned)info->p_len, (int)info->t_len,
              (const char *)info->token);
    return OPRT_OK;
}

/**
 * @brief Token-get port: start BLE provisioning on an unbound device
 */
static int __ble_token_get(tuya_iot_config_t *config)
{
    (void)config;

    __ble_stack_start();
    if (OPRT_OK != ble_netcfg_start(__ble_netcfg_finish)) {
        PR_ERR("BLE netcfg start failed");
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}
#endif /* ENABLE_BLUETOOTH */

void user_main(void)
{
    int rt = OPRT_OK;

    //! open iot development kit runtim init
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = core_malloc, .free_fn = core_free});
    /* Before the log: every log line timestamps itself through the time
     * service, whose mutex this creates. */
    core_time_service_init();
    core_log_init(CORE_LOG_LEVEL_DEBUG, 1024, user_log_output_cb);

    PR_NOTICE("Application information:");
    PR_NOTICE("App version:         %s", PROJECT_VERSION);
    PR_NOTICE("Compile time:        %s", __DATE__);

    core_kv_init(&(core_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key  = "dflfuap134ddlduq",
    });
    core_sw_timer_init();
    core_workq_init();

    /* Clear the network configuration when the reset counter reaches the threshold: increment the counter at startup and start the 5s clear timer */
    reset_netconfig_start();

    /* Use the compile-time credentials directly (tuya_config.h or -D injection) */
    license.uuid    = TUYA_OPENSDK_UUID;
    license.authkey = TUYA_OPENSDK_AUTHKEY;

    /* P2P activation parameters (skill list) must be assembled before tuya_iot_init */
    OnIotInited();

    /* Initialize Tuya device configuration */
    rt = tuya_iot_init(&client, &(const tuya_iot_config_t){
                                    .software_ver  = PROJECT_VERSION,
                                    .productkey    = TUYA_PRODUCT_ID,
                                    .uuid          = license.uuid,
                                    .authkey       = license.authkey,
                                    .event_handler = user_event_handler_on,
                                    .network_check = user_network_check,
                                    .skill_param   = gw_active_get_ext_param(),
                                });
    assert(rt == OPRT_OK);

#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1)
    /* BLE stack for the optional provisioning path below */
    __ble_stack_start();
#endif

    /* Link status hookup: port (port_netif) reports the current status immediately
     * and polls for changes */
    core_netif_set_link_cb(__user_link_status_cb);

    /* Token fetch port: mqtt_bind handles direct binding and dispatches
     * TUYA_EVENT_DIRECT_MQTT_CONNECTED after the MQTT connection is up to print
     * the QR code. With BLE enabled, the APP delivers the token over BLE instead. */
#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1)
    tuya_iot_token_get_port_register(&client, __ble_token_get);
#else
    tuya_iot_token_get_port_register(&client, mqtt_bind_token_get);
#endif

    PR_DEBUG("tuya_iot_init success");
    /* Start tuya iot task */
    tuya_iot_start(&client);

    /* Clear the network configuration when the reset counter reaches the threshold
     * (must be called after iot start; it depends on the global client) */
    reset_netconfig_check();

    for (;;) {
        /* Loop to receive packets, and handles client keepalive */
        tuya_iot_yield(&client);
    }
}

/**
 * @brief main
 *
 * @param argc
 * @param argv
 * @return void
 */
#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    user_main();
}
#else

/* Tuya thread handle */
static THREAD_HANDLE ty_app_thread = NULL;

/**
 * @brief  task thread
 *
 * @param[in] arg:Parameters when creating a task
 * @return none
 */
static void tuya_app_thread(void *arg)
{
    user_main();

    core_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    /* P2P offer is handled on this thread via MQTT yield; 4KB overflows in pj/ICE */
    thrd_param.stackDepth   = 1024 * 48;
    thrd_param.priority     = THREAD_PRIO_1;
    thrd_param.thrdname     = "tuya_app_main";
    core_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif

/**
 * @brief P2P media thread: fill the ten TUYA_IPC_SDK_VAR_S callbacks -> TUYA_APP_Start -> start the media chain
 *
 * @param arg unused
 * @return void
 */
static void tuya_ipc_demo_thread(void *arg)
{
    TUYA_IPC_SDK_VAR_S sdkVar      = {0};
    sdkVar.OnGetVideoFrameCallback = demo_on_get_video_frame_callback;
    sdkVar.OnGetAudioFrameCallback = demo_on_get_audio_frame_callback;
    sdkVar.OnSignalDisconnectCallback = demo_on_signal_disconnect_callback;
    sdkVar.OnLiveVideoStartCallback = demo_on_live_video_start_callback;
    sdkVar.OnLiveVideoStopCallback = demo_on_live_video_stop_callback;
    sdkVar.OnLiveAudioStartCallback = demo_on_live_audio_start_callback;
    sdkVar.OnLiveAudioStopCallback = demo_on_live_audio_stop_callback;
    sdkVar.OnRecvAudioFrameCallback = demo_on_recv_audio_frame_callback;
    sdkVar.OnRequestIFrameCallback = demo_on_request_i_frame_callback;
    sdkVar.OnSetVideoBitrateCallback = demo_on_set_video_bitrate_callback;
    TUYA_APP_Start(&sdkVar);
    tuya_ipc_demo_start();
    return;
}
