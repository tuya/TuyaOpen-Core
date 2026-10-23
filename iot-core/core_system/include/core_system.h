/**
 * @file core_system.h
 * @brief Provides system-level utilities and operations for Tuya IoT
 * applications.
 *
 * This header file defines the interface for system-level operations in Tuya
 * IoT applications, including system sleep and reset functionalities,
 * millisecond timing, and random number generation. These utilities are
 * essential for managing the application's execution environment and ensuring
 * time-sensitive operations are handled correctly.
 *
 * The API abstracts the underlying hardware and operating system details,
 * offering a portable and consistent interface for application development
 * across different Tuya IoT devices.
 *
 * @note This file is part of the Tuya IoT Development Platform and is intended
 * for use in Tuya-based applications. It is subject to the platform's license
 * and copyright terms.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __CORE_SYSTEM_H__
#define __CORE_SYSTEM_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ********************* constant ( macro and enum ) *********************
 **********************************************************************/
/***********************************************************************
 ********************* struct ******************************************
 **********************************************************************/

/***********************************************************************
 ********************* variable ****************************************
 **********************************************************************/

/***********************************************************************
 ********************* function ****************************************
 **********************************************************************/

/**
 * @brief This API is used for system sleep.
 *
 * @param[in] time_ms: time in MS
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
void core_system_sleep(uint32_t time_ms);

/**
 * @brief core_system_reset
 *
 * @param[in] param: none
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
void core_system_reset(void);

/**
 * @brief core_system_get_millisecond
 *
 * @param[in] param: none
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
SYS_TIME_T core_system_get_millisecond(void);

/**
 * @brief Get system random data
 *
 * @param[in] range: random from 0  to range
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
int core_system_get_random(uint32_t range);

#ifdef __cplusplus
}
#endif

#endif /* __CORE_SYSTEM_H__ */
