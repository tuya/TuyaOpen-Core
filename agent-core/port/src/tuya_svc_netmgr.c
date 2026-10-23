/**
 * @file tuya_svc_netmgr.c
 * @brief Network status query for the AI stack.
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 * tuya_svc_netmgr_get_status() reports NETWORK_STATUS_MQTT while the IoT
 * client exists and reports an MQTT connection, and NETWORK_STATUS_OFFLINE
 * otherwise. The AI client uses it to gate connection setup and to decide
 * whether a failed packet read should be retried.
 */

#include "tuya_svc_netmgr.h"

#include "tuya_iot.h"

/***********************************************************
************************macro define************************
***********************************************************/


/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
********************function declaration********************
***********************************************************/


/***********************************************************
***********************variable define**********************
***********************************************************/


/***********************************************************
***********************function define**********************
***********************************************************/
NETWORK_STATUS_E tuya_svc_netmgr_get_status(VOID)
{
    if (tuya_iot_client_get() == NULL) {
        return NETWORK_STATUS_OFFLINE;
    }

    return (tuya_iot_is_connected() ? NETWORK_STATUS_MQTT : NETWORK_STATUS_OFFLINE);
}