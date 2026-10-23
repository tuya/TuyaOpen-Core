/**
 * @file reset_netcfg.h
 * @brief Consecutive-reset network configuration clearing: reset counter in KV, startup check, and reset event handling
 *
 * reset_netconfig_start() reads the "rst_cnt" key from KV, increments it and
 * starts a 5-second one-shot timer that clears the counter, so a boot with no
 * further reset within 5 seconds clears the count.
 * reset_netconfig_check() runs at startup: when the counter has reached 3, it
 * subscribes the EVENT_RESET handler (which also clears the counter) and calls
 * tuya_iot_reset() to clear the network configuration and enter
 * reconfiguration.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __RESET_NETCFG_H__
#define __RESET_NETCFG_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/
/**
 * @brief Start the network configuration reset flow: increment the reset counter and start a 5-second one-shot timer to clear it.
 *
 * On a normal boot (no further reset within 5 seconds) the counter is cleared;
 * after 3 consecutive resets the next reset_netconfig_check() clears the
 * network configuration data.
 *
 * @return int 0 on success, a negative value on failure.
 */
int reset_netconfig_start(void);

/**
 * @brief Check whether the reset counter reached the threshold and clear the network configuration if so.
 *
 * When the counter is >= 3, subscribe to EVENT_RESET (which clears the counter
 * after the reset event) and call tuya_iot_reset() to clear the network
 * configuration data.
 *
 * @return int 0 on success, a negative value on failure.
 */
int reset_netconfig_check(void);

#ifdef __cplusplus
}
#endif

#endif /* __RESET_NETCFG_H__ */
