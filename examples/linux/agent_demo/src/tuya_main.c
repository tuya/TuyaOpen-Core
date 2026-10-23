/**
 * @file tuya_main.c
 * @brief agent_demo main flow: CORE runtime init, IoT cloud connection, and AI link startup
 *
 * Link: IoT activation/cloud connection → (after MQTT connects) initialize the
 * AI agent → wait for the client to become ready → automatically upload one
 * preset utterance (trigger) → print downlink text (ASR/NLG).
 *
 * Sequence in user_main(): cJSON hooks and CORE runtime init (time service,
 * log, KV, software timer, work queue); downlink audio service init when
 * ENABLE_COMP_AI_AUDIO is on (ai_audio_player_init, then the ALSA consumer
 * install); reset_netconfig_start(); tuya_iot_init() with compile-time
 * credentials; link status callback; mqtt_bind token port; ai_mcp_init() when
 * ENABLE_COMP_AI_MCP is on; tuya_iot_start(); reset_netconfig_check(); then
 * tuya_iot_yield() forever.
 *
 * No UI; downlink audio is played through the application-side ALSA consumer
 * (play_local_out) when ENABLE_COMP_AI_AUDIO is enabled and the media packets
 * are dropped inside core when it is disabled; uplink audio is obtained
 * through the application-side play_local_in_* contract (Linux: PCM file).
 * The volume DP (id 3) drives the player master volume; received OBJ/RAW DPs
 * are echoed back to the cloud.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "cJSON.h"
#include "mqtt_bind.h"
#include "core_api.h"
#include "core_netif.h"
#include "core_api_ops.h"
#include "tuya_config.h"
#include "tuya_iot.h"
#include "tuya_iot_dp.h"
#include "reset_netcfg.h"

#include "agent.h"

/* The MCP header comes with the ai_mcp target (CONFIG_ENABLE_COMP_AI_MCP): when
 * the switch is off the target does not exist and its include path disappears,
 * so the include and the call are guarded by the macro together (same style as
 * ENABLE_QRCODE below) */
#if defined(ENABLE_COMP_AI_MCP) && (ENABLE_COMP_AI_MCP == 1)
#include "ai_mcp.h"
#endif

/* Downlink audio playback (src/components/ai_audio): ai_audio_player_init sets up
 * the playback service (internally tuya_ai_player_service_init) and the port's
 * ALSA consumer is then injected. Like MCP: when the switch is off neither the
 * header nor the target exists, so the include and the call are guarded by the
 * macro together */
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
#include "ai_audio_player.h"
#include "play_local_out.h"
#endif

#include <assert.h>
#include <stdio.h>

#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1)
#include "ble_mgr.h"
#include "ble_netcfg.h"
#endif

#if defined(ENABLE_QRCODE) && (ENABLE_QRCODE == 1)
#include "qrencode_print.h"
#endif

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

/* Product DP: volume (0-100, rw) */
#define AGENT_DPID_VOLUME 3

/* Tuya device handle */
tuya_iot_client_t client;

/* Tuya license information (uuid authkey) */
tuya_iot_license_t license;

/**
 * @brief user defined log output api, in this demo, it will use uart0 as log-tx
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
    cJSON *url_item     = cJSON_GetObjectItem(upgrade, "url");

    PR_INFO("OTA Channel: %d", cJSON_IsNumber(type_item) ? type_item->valueint : -1);
    PR_INFO("Version: %s", cJSON_IsString(version_item) ? version_item->valuestring : "N/A");
    PR_INFO("Size: %s", cJSON_IsString(size_item) ? size_item->valuestring : "N/A");
    PR_INFO("URL: %s", cJSON_IsString(url_item) ? url_item->valuestring : "N/A");
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

    /* An activated device connected to the cloud MQTT: start the AI link (agent session + voice trigger, see agent.c) */
    case TUYA_EVENT_MQTT_CONNECTED:
        PR_NOTICE("[AGENT] mqtt connected, start ai chain");
        agent_start();
        break;

    /* RECV upgrade request */
    case TUYA_EVENT_UPGRADE_NOTIFY:
        user_upgrade_notify_on(client, event->value.asJSON);
        break;

    case TUYA_EVENT_RESET: {
        tuya_reset_type_t reset_type = (tuya_reset_type_t)event->value.asInteger;
        PR_INFO("Device Reset:%d", reset_type);
    } break;

    case TUYA_EVENT_RESET_COMPLETE:
        PR_INFO("Device Reset Complete!");
        core_system_reset();
        break;

    /* RECV OBJ DP: print + drive the player master volume from the volume DP +
     * report everything back (without the report the App shows the device as unresponsive) */
    case TUYA_EVENT_DP_RECEIVE_OBJ: {
        dp_obj_recv_t *dpobj = event->value.dpobj;
        PR_NOTICE("[AGENT] DP recv: cnt=%u", dpobj->dpscnt);
        for (uint32_t i = 0; i < dpobj->dpscnt; i++) {
            dp_obj_t *dp = dpobj->dps + i;
            PR_NOTICE("[AGENT] DP id=%d type=%d value=%d", dp->id, dp->type, (int)dp->value.dp_value);
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
            if (dp->id == AGENT_DPID_VOLUME) {
                PR_NOTICE("[AGENT] volume DP -> set_vol(%d)", (int)dp->value.dp_value);
                ai_audio_player_set_vol((int)dp->value.dp_value);
            }
#endif
        }
        tuya_iot_dp_obj_report(client, dpobj->devid, dpobj->dps, dpobj->dpscnt, 0);
    } break;

    /* RECV RAW DP: print (with a byte dump) then report back */
    case TUYA_EVENT_DP_RECEIVE_RAW: {
        dp_raw_recv_t *dpraw = event->value.dpraw;
        PR_NOTICE("[AGENT] DP raw recv: id=%d len=%d", dpraw->dp.id, dpraw->dp.len);
        for (uint32_t i = 0; i < dpraw->dp.len; i++) {
            PR_DEBUG_RAW("%02x", dpraw->dp.data[i]);
        }
        tuya_iot_dp_raw_report(client, dpraw->devid, &dpraw->dp, 3);
    } break;

    default:
        break;
    }
}

/* Link state: maintained by the port layer (port_netif) status callback and pulled by core via user_network_check */
static volatile bool s_link_up = false;

/**
 * @brief Port link status callback: port_netif reports the current state right after registration,
 * then polls periodically and calls back again on state changes
 *
 * @param stat current link state
 * @return void
 */
static void __user_link_status_cb(CORE_NETIF_LINK_STAT_E stat)
{
    s_link_up = (CORE_NETIF_LINK_UP == stat);

    /* Sync the active IP: link up != IP ready (port_netif only checks IFF_UP,
     * which does not mean an address was obtained). Outbound socket source
     * address binding and the local IP in LAN broadcast packets both take this
     * value; 0 means no binding. */
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
    PR_NOTICE("Audio source:        %s", AGENT_AUDIO_SRC_PATH);

    core_kv_init(&(core_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key  = "dflfuap134ddlduq",
    });
    core_sw_timer_init();
    core_workq_init();

#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
    /* Downlink audio playback initialization (why it sits in the main init
     * section, before MQTT):
     * - ai_audio_player_init internally runs tuya_ai_player_service_init
     *   (creates threads/queues, purely local, no network dependency), so only
     *   the CORE runtime must be ready first (hence after core_workq_init);
     * - downlink TTS can only arrive after MQTT_CONNECTED, so the playback
     *   service must be ready first; otherwise the first audio packet lands on
     *   the default empty consumer and is dropped;
     * - install must run after init: service_init loads the default empty
     *   consumer and set_consumer is the final replacement point (timing in
     *   play_local_out.h);
     * - failures do not block the main flow: print the error and continue; the
     *   downlink degrades to silence (the default empty consumer behavior) */
    rt = ai_audio_player_init();
    if (rt == OPRT_OK) {
        PR_NOTICE("[AGENT] ai_audio_player initialized, ALSA consumer install rt=%d",
                  play_local_out_install());
    } else {
        PR_ERR("[AGENT] ai_audio_player_init failed: %d (downlink audio disabled)", rt);
    }
#endif

    /* Clear the network configuration after 3 consecutive resets (same as iot_demo/p2p_demo reset_netcfg) */
    reset_netconfig_start();

    /* Use compile-time credentials directly (tuya_config.h or -D injection) */
    license.uuid    = TUYA_OPENSDK_UUID;
    license.authkey = TUYA_OPENSDK_AUTHKEY;
    /* Initialize Tuya device configuration */
    rt = tuya_iot_init(&client, &(const tuya_iot_config_t){
                                    .software_ver  = PROJECT_VERSION,
                                    .productkey    = TUYA_PRODUCT_ID,
                                    .uuid          = license.uuid,
                                    .authkey       = license.authkey,
                                    .event_handler = user_event_handler_on,
                                    .network_check = user_network_check,
                                });
    assert(rt == OPRT_OK);

#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1)
    /* BLE stack for the optional provisioning path below */
    __ble_stack_start();
#endif

    /* Link state hookup: port (port_netif) reports the current state immediately and polls for changes */
    core_netif_set_link_cb(__user_link_status_cb);

    /* Token fetch port: mqtt_bind direct binding (prints the QR code when not
     * activated). With BLE enabled, the APP delivers the token over BLE instead. */
#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1)
    tuya_iot_token_get_port_register(&client, __ble_token_get);
#else
    tuya_iot_token_get_port_register(&client, mqtt_bind_token_get);
#endif

#if defined(ENABLE_COMP_AI_MCP) && (ENABLE_COMP_AI_MCP == 1)
    /* MCP tool service: ai_mcp_init only subscribes to EVENT_MQTT_CONNECTED
     * (one-shot); the real initialization (register tools +
     * tuya_ai_agent_mcp_set_cb) runs in the callback after MQTT connects.
     * Placed here (before tuya_iot_start): the event is published by the iot
     * task when MQTT connects and the subscription must precede the first
     * publication; placing it after tuya_iot_init keeps the existing order of
     * "runtime services (KV/timer/work queue) ready first, cloud connection
     * after" — core_event_subscribe lazily initializes the event manager, so
     * there is no hard dependency here; it is only for readability. */
    rt = ai_mcp_init();
    if (rt != OPRT_OK) {
        PR_ERR("[MCP] ai_mcp_init failed: %d", rt);
    }
#endif

    PR_DEBUG("tuya_iot_init success");
    /* Start tuya iot task */
    tuya_iot_start(&client);

    /* Clear the network configuration when the consecutive-reset count reaches the threshold (see reset_netcfg.h) */
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
#endif
