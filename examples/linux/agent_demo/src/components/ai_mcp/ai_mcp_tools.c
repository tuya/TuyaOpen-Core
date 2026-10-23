/**
 * @file ai_mcp_tools.c
 * @brief MCP tool registration: device_info_get reports model, serial number, and firmware version
 *
 * ai_mcp_init() subscribes to EVENT_MQTT_CONNECTED one-shot; after MQTT
 * connects the handler starts the MCP server and registers the device info
 * tool. serialNumber is read from the IoT client device ID and degrades to
 * "unknown" when the client is not initialized. Tools whose backing components
 * are absent are not registered, so the cloud tool list matches the compiled
 * capabilities.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#include "core_api.h"

#include "tuya_ai_agent.h"
#include "tuya_iot.h"

#include "ai_mcp_server.h"

#include "ai_mcp.h"
/***********************************************************
************************macro define************************
***********************************************************/
/* Fallbacks for the project info injected by the build system (same style as
 * examples/agent_demo/src/tuya_main.c): PROJECT_NAME/PROJECT_VERSION are
 * injected by the host build (esp-idf injects PROJECT_NAME); plain CMake hosts
 * that do not inject them use the defaults below. */
#ifndef PROJECT_NAME
#define PROJECT_NAME "agent_demo"
#endif

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
***********************variable define**********************
***********************************************************/


/***********************************************************
***********************function define**********************
***********************************************************/
static OPERATE_RET __get_device_info(const MCP_PROPERTY_LIST_T *properties, MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    cJSON             *json   = NULL;
    tuya_iot_client_t *client = NULL;
    const char        *devid  = NULL;

    json = cJSON_CreateObject();
    if (!json) {
        PR_ERR("Create JSON object failed");
        return OPRT_MALLOC_FAILED;
    }

    /* serialNumber takes the IoT client's real device ID (delivered by the
     * cloud after activation and stored in KV, restored on reboot); this tool
     * can only be called after MQTT connects (MCP cb registered), so devid is
     * non-empty on the normal path. It degrades to "unknown" when not
     * activated/initialized (client is NULL). */
    client = tuya_iot_client_get();
    if (client) {
        devid = tuya_iot_devid_get(client);
    }

    // Add device information
    cJSON_AddStringToObject(json, "model", PROJECT_NAME);
    cJSON_AddStringToObject(json, "serialNumber", (devid && devid[0] != '\0') ? devid : "unknown");
    cJSON_AddStringToObject(json, "firmwareVersion", PROJECT_VERSION);

    // Set return value
    ai_mcp_return_value_set_json(ret_val, json);

    return OPRT_OK;
}

static OPERATE_RET __ai_mcp_tools_register(void)
{
    OPERATE_RET rt = OPRT_OK;

    // device info get tool
    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "device_info_get",
        "Get device information such as model, serial number, and firmware version.",
        __get_device_info,
        NULL
    ), err);

/* Tools that are not registered, so the cloud tool list matches the actual
 * capabilities and never exposes placeholders whose call would always fail:
 * - device_camera_take_photo: depends on ai_video (not present in this tree)
 * - device_audio_volume_set: depends on ai_audio (not present in this tree)
 * - device_audio_mode_set  : depends on ai_mode (not present in this tree) */

    return OPRT_OK;

err:
    // destroy MCP server on failure
    ai_mcp_server_destroy();

    return rt;
}

static OPERATE_RET __ai_mcp_init(void *data)
{
    OPERATE_RET rt = OPRT_OK;

    // FIXME: Set actual MCP server name and mcp version
    TUYA_CALL_ERR_RETURN(ai_mcp_server_init("Tuya MCP Server", "1.0"));

    TUYA_CALL_ERR_RETURN(__ai_mcp_tools_register());

    PR_DEBUG("MCP Server initialized successfully");

    return rt;
}

OPERATE_RET ai_mcp_init(void)
{
    return core_event_subscribe(EVENT_MQTT_CONNECTED, "ai_mcp_init", __ai_mcp_init, SUBSCRIBE_TYPE_ONETIME);
}

OPERATE_RET ai_mcp_deinit(void)
{
    ai_mcp_server_destroy();

    PR_DEBUG("MCP Server deinitialized successfully");

    return OPRT_OK;
}