/**
 * @file ble_netcfg.c
 * @brief Implementation of BLE network configuration.
 * This file contains the logic for handling BLE-based network configuration
 * processes, including parsing network configuration commands, managing network
 * configuration sessions, and invoking callbacks upon the completion of network
 * configuration.
 *
 * It utilizes the Tuya BLE SDK to facilitate the network configuration of
 * devices over BLE, ensuring a seamless setup process for connecting Tuya smart
 * devices to the network.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "core_api.h"
#include "core_event_info.h"
#include "ble_mgr.h"
#include "tuya_register_center.h"
#include "ble_channel.h"
#include "ble_netcfg.h"

typedef struct {
    ble_netcfg_info_t info;
    ble_netcfg_finish_cb_t finish_cb;
} ble_netcfg_t;

static ble_netcfg_t g_bt_netcfg_handle;

extern int tuya_ble_adv_update(void);

static void __handle_net_cfg(void *data, void *user_data)
{
    uint8_t result = 0;
    // resp[0] is never assigned below
    uint8_t resp[5] = {0};

    cJSON *json = cJSON_Parse(data);
    if (NULL == json) {
        PR_ERR(" json parse error.");
        result = (uint8_t)OPRT_CJSON_PARSE_ERR;
        goto __exit;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
    cJSON *token_item = cJSON_GetObjectItem(json, "token");
    if (!cJSON_IsString(ssid_item) || !cJSON_IsString(token_item)) {
        PR_ERR(" json get error.");
        result = (uint8_t)OPRT_CJSON_GET_ERR;
        goto __exit;
    }

    char *ssid = ssid_item->valuestring;
    char *token = token_item->valuestring;
    char *passwd = NULL;
    cJSON *passwd_item = cJSON_GetObjectItem(json, "pwd");
    if (passwd_item != NULL && cJSON_IsString(passwd_item)) {
        passwd = passwd_item->valuestring;
    }

    if (NULL == passwd) {
        PR_ERR(" json get passwd error.");
        result = (uint8_t)OPRT_CJSON_GET_ERR;
        goto __exit;
    }

    // PR_NOTICE("cfg ssid:%s, passwd:%s, token:%s", ssid, passwd, token);
    if (g_bt_netcfg_handle.finish_cb == NULL) {
        PR_ERR(" netcfg not started.");
        result = (uint8_t)OPRT_COM_ERROR;
        goto __exit;
    }
    memset(&g_bt_netcfg_handle.info, 0, sizeof(g_bt_netcfg_handle.info));

    // Copy SSID
    size_t s_len = strnlen(ssid, WIFI_SSID_LEN + 1);
    if (s_len == 0 || s_len > WIFI_SSID_LEN) {
        PR_ERR("ssid len invalid:%zu", s_len);
        result = (uint8_t)OPRT_CJSON_GET_ERR;
        goto __exit;
    }
    memcpy(g_bt_netcfg_handle.info.ssid, ssid, s_len);
    g_bt_netcfg_handle.info.ssid[s_len] = '\0';
    g_bt_netcfg_handle.info.s_len = (uint8_t)s_len;

    // Copy password
    size_t p_len = strnlen(passwd, WIFI_PASSWD_LEN + 1);
    if (p_len == 0 || p_len > WIFI_PASSWD_LEN) {
        PR_ERR("passwd len invalid:%zu", p_len);
        result = (uint8_t)OPRT_CJSON_GET_ERR;
        goto __exit;
    }
    memcpy(g_bt_netcfg_handle.info.passwd, passwd, p_len);
    g_bt_netcfg_handle.info.passwd[p_len] = '\0';
    g_bt_netcfg_handle.info.p_len = (uint8_t)p_len;

    // Copy token
    size_t t_len = strnlen(token, BLE_NETCFG_TOKEN_LEN + 1);
    if (t_len == 0 || t_len > BLE_NETCFG_TOKEN_LEN) {
        PR_ERR("token len invalid:%zu", t_len);
        result = (uint8_t)OPRT_CJSON_GET_ERR;
        goto __exit;
    }
    memcpy(g_bt_netcfg_handle.info.token, token, t_len);
    g_bt_netcfg_handle.info.token[t_len] = '\0';
    g_bt_netcfg_handle.info.t_len = (uint8_t)t_len;
    g_bt_netcfg_handle.finish_cb(&g_bt_netcfg_handle.info);

    cJSON *reg = cJSON_GetObjectItem(json, "reg");
    if (reg) {
        tuya_register_center_save(RCS_APP, reg);
    }

__exit:
    if (json) {
        cJSON_Delete(json);
    }
    resp[1] = 0x00; // for blt timer task, set as not subpacket, not response
    resp[2] = 0x00;
    resp[3] = FRM_DATA_TRANS_SUBCMD_BT_NETCFG;
    resp[4] = result; // result code: 0x00 - success, other - fail
    tuya_ble_send(FRM_UPLINK_TRANSPARENT_REQ, 0, resp, 5);
}

/**
 * @brief Starts the BLE network configuration.
 *
 * This function starts the BLE network configuration process: registers the
 * netcfg channel and refreshes the advertising data.
 *
 * @param cb The callback function to be called when the network configuration
 * is finished.
 * @return The result of the operation.
 */
int ble_netcfg_start(ble_netcfg_finish_cb_t cb)
{
    if (cb == NULL) {
        return OPRT_INVALID_PARM;
    }

    PR_DEBUG("bt netcfg start");
    memset(&g_bt_netcfg_handle, 0, sizeof(g_bt_netcfg_handle));
    g_bt_netcfg_handle.finish_cb = cb;
    ble_channel_add(BLE_CHANNLE_NETCFG, __handle_net_cfg, NULL);

    tuya_ble_adv_update();

    return OPRT_OK;
}

/**
 * @brief Stops the BLE network configuration.
 *
 * This function stops the BLE network configuration by deleting the BLE channel
 * used for network configuration. Safe to call when not started.
 *
 * @return The result of the operation. Returns OPRT_OK if successful, otherwise
 * an error code.
 */
int ble_netcfg_stop(void)
{
    PR_DEBUG("bt netcfg stop");
    ble_channel_del(BLE_CHANNLE_NETCFG);
    g_bt_netcfg_handle.finish_cb = NULL;

    return OPRT_OK;
}
