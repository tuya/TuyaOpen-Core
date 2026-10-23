/**
 * @file mqc_app.c
 * @brief Custom MQTT protocol registry: fixed-size handler table, publish
 *        helper and the netmgr linkage descriptor.
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 * mqc_app_register_cb() stores {protocol, handler} in a static table of
 * MAX_MQC_PROTOCOL_NUM entries and registers __mqc_app_handler with the IoT
 * MQTT context, passing the table entry as user_data. __mqc_app_handler
 * forwards event->root_json to that handler. mqc_app_unregister_cb()
 * unregisters the protocol and compacts the table, and
 * mqc_send_custom_mqtt_msg() publishes a NUL-terminated payload under a
 * protocol id (the length comes from strlen).
 *
 * The same file defines the linkage used by netmgr consumers: g_linkage is a
 * LINKAGE_TYPE_WIFI descriptor whose get callback answers only LINKAGE_CFG_IP
 * (through core_netif_get_ip); other configurations return OPRT_NOT_SUPPORTED.
 * __ai_port_linkage_get() and mqc_get_connection_linkage() expose it.
 */

#include "mqc_app.h"

#include "core_log.h"

#include "tuya_iot.h"
#include "mqtt_service.h"
#include "core_netif.h"

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    uint32_t protocol;
    mqc_protocol_handler_cb handler;
} mqc_protocol_entry_t;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
#define MAX_MQC_PROTOCOL_NUM 16
static mqc_protocol_entry_t g_mqc_protocol_table[MAX_MQC_PROTOCOL_NUM] = {0};
static uint16_t g_mqc_protocol_count = 0;

/***********************************************************
***********************function define**********************
***********************************************************/
void __mqc_app_handler(tuya_protocol_event_t *event)
{
    PR_DEBUG("mqc app handler protocol:%d", event->event_id);

    if (event == NULL) {
        PR_ERR("invalid event");
        return;
    }

    mqc_protocol_entry_t *entry = (mqc_protocol_entry_t *)event->user_data;
    if (entry == NULL || entry->handler == NULL) {
        PR_ERR("invalid handler for protocol: %d", event->event_id);
        return;
    }

    entry->handler(event->root_json);

    return;
}

OPERATE_RET mqc_app_register_cb(uint32_t mq_pro, mqc_protocol_handler_cb handler)
{
    PR_DEBUG("register mq protocol:%u", mq_pro);

    if (g_mqc_protocol_count >= MAX_MQC_PROTOCOL_NUM) {
        PR_ERR("mqc protocol table full");
        return OPRT_COM_ERROR;
    }

    g_mqc_protocol_table[g_mqc_protocol_count].protocol = mq_pro;
    g_mqc_protocol_table[g_mqc_protocol_count].handler = handler;

    tuya_mqtt_protocol_register(&tuya_iot_client_get()->mqctx, (uint16_t)mq_pro, __mqc_app_handler,
                                &g_mqc_protocol_table[g_mqc_protocol_count]);

    g_mqc_protocol_count++;

    return OPRT_OK;
}

OPERATE_RET mqc_app_unregister_cb(uint32_t mq_pro, mqc_protocol_handler_cb handler)
{
    PR_DEBUG("unregister mq protocol:%u", mq_pro);

    for (uint32_t i = 0; i < g_mqc_protocol_count; i++) {
        if (g_mqc_protocol_table[i].protocol == mq_pro && g_mqc_protocol_table[i].handler == handler) {
            tuya_mqtt_protocol_unregister(&tuya_iot_client_get()->mqctx, (uint16_t)mq_pro, __mqc_app_handler);

            /* Shift remaining entries */
            for (uint32_t j = i; j < g_mqc_protocol_count - 1; j++) {
                g_mqc_protocol_table[j].protocol = g_mqc_protocol_table[j + 1].protocol;
                g_mqc_protocol_table[j].handler = g_mqc_protocol_table[j + 1].handler;
            }
            g_mqc_protocol_count--;
            return OPRT_OK;
        }
    }

    return OPRT_OK;
}

OPERATE_RET mqc_send_custom_mqtt_msg(IN CONST uint32_t protocol, IN CONST uint8_t *p_data)
{
    tuya_iot_client_t *client = tuya_iot_client_get();
    tuya_mqtt_protocol_data_publish(&client->mqctx, protocol, p_data, strlen((const char *)p_data));

    return OPRT_OK;
}

OPERATE_RET __linkage_get(IN LINKAGE_CFG_E cfg, OUT VOID *data)
{
    OPERATE_RET rt = OPRT_OK;

    if (LINKAGE_CFG_IP == cfg) {
        rt = core_netif_get_ip((NW_IP_S *)data);
    } else {
        PR_ERR("linkage get not supported for cfg:%d", cfg);
        return OPRT_NOT_SUPPORTED;
    }

    return rt;
}

static netmgr_linkage_t g_linkage = {
    .type = LINKAGE_TYPE_WIFI,
    .capability = 0,
    .get = __linkage_get,
};

netmgr_linkage_t *__ai_port_linkage_get(VOID)
{
    return &g_linkage;
}

OPERATE_RET mqc_get_connection_linkage(netmgr_linkage_t **linkage)
{
    *linkage = &g_linkage;

    return OPRT_OK;
}