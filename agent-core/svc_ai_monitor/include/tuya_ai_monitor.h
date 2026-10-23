/**
 * @file tuya_ai_monitor.h
 * @author tuya
 * @brief TUYA AI monitor service
 * @version 0.1
 * @date 2025-06-09
 *
 * @copyright Copyright 2014-2025 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TUYA_AI_MONITOR_H__
#define __TUYA_AI_MONITOR_H__

#include "tuya_ai_types.h"

#include "tuya_cloud_types.h"
#include "tuya_ai_biz.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AI_PT_CUSTOM_LOG  60        // Custom pt type for log messages

#define AI_EVENT_MONITOR_FILTER     0xF000  // Filter for AI event monitor type filtering
#define AI_EVENT_MONITOR_ALG_CTRL   0xF001  // Filter for AI event monitor algorithm control

/**
 * @brief AI monitor server configuration
 */
typedef struct {
    uint32_t port;                    // TCP server port
    uint32_t max_clients;             // maximum client connections
    uint32_t recv_buf_size;           // receive buffer size
    uint32_t send_buf_size;           // send buffer size
    uint32_t heartbeat_interval;      // heartbeat interval in seconds
    uint32_t heartbeat_timeout;       // heartbeat timeout in seconds
    BOOL_T enable_broadcast;        // enable broadcast to all clients
} ai_monitor_config_t;

#define AI_MONITOR_PORT_DEFAULT          5055
#define AI_MONITOR_MAX_CLIENTS_DEFAULT   3
#define AI_MONITOR_CFG_DEFAULT {\
    .port = AI_MONITOR_PORT_DEFAULT, \
    .max_clients = AI_MONITOR_MAX_CLIENTS_DEFAULT, \
    .recv_buf_size = 1024, \
    .send_buf_size = 1024, \
    .heartbeat_interval = 30, \
    .heartbeat_timeout = 60, \
    .enable_broadcast = TRUE \
}

/**
 * @brief initialize AI monitor TCP server
 *
 * @param[in] config server configuration
 * @param[in] callbacks callback functions
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_monitor_init(CONST ai_monitor_config_t *config);

#ifdef __cplusplus
}
#endif
#endif //__TUYA_AI_MONITOR_H__