/**
 * @file core_ota.h
 * @brief CORE OTA interface: callback injection for the start, data and end
 *        steps of a firmware update.
 *
 * OTA is an optional capability. A platform or application that supports it
 * registers a CORE_OTA_CBS_T with core_ota_register_cbs(); core_ota_start_notify,
 * core_ota_data_process and core_ota_end_notify forward to the registered
 * callbacks and return OPRT_NOT_SUPPORTED when no callback set is registered.
 * Registration stores the pointer without copying it, so the callback set must
 * have static lifetime covering the whole OTA flow.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __CORE_OTA_H__
#define __CORE_OTA_H__

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

/**
 * @brief OTA platform callback set (callback injection; OTA is optional)
 *
 * Platforms/applications that support OTA register this structure with
 * core_ota_register_cbs at initialization; without registration core_ota_*
 * returns OPRT_NOT_SUPPORTED.
 *
 * @note Registration stores the pointer only, it does not copy: the structure
 *       must be a static/global object whose lifetime covers the whole OTA
 *       flow.
 */
typedef struct {
    /* OTA start notification; see core_ota_start_notify */
    OPERATE_RET (*start_notify)(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path);
    /* OTA data write; see core_ota_data_process */
    OPERATE_RET (*data_process)(TUYA_OTA_DATA_T *pack, uint32_t *remain_len);
    /* OTA end notification; see core_ota_end_notify */
    OPERATE_RET (*end_notify)(BOOL_T reset);
} CORE_OTA_CBS_T;

/***********************************************************************
 ********************* variable ****************************************
 **********************************************************************/

/***********************************************************************
 ********************* function ****************************************
 **********************************************************************/

/**
 * @brief Register the platform callbacks of OTA (optional capability)
 *
 * Register the OTA platform callbacks (callback injection): platforms or
 * applications that support OTA call this interface to inject them; when not
 * registered, core_ota_* dispatch returns OPRT_NOT_SUPPORTED. Typical call
 * site: application initialization (before the cloud connection is
 * established); repeated calls keep the last one, and passing NULL unregisters.
 *
 * @param[in] cbs: pointer to the callback set (static lifetime), may be NULL
 *
 * @note Not registering means OTA is unsupported: core_ota_* returns
 *       OPRT_NOT_SUPPORTED. Note that core's cloud OTA flow (tuya_ota.c) does
 *       not check these return values -- the cloud still completes
 *       download/verification and reports "upgrade finished" while the device
 *       does not actually apply the package; platforms that do not intend to
 *       support OTA should make sure the cloud does not send upgrade tasks.
 *
 * @return none
 */
void core_ota_register_cbs(const CORE_OTA_CBS_T *cbs);

/**
 * @brief This API is used for ota start notify
 *
 * @param[in] image_size: image size
 * @param[in] type: ota type
 * @param[in] path: path
 *
 * @return OPRT_OK on success; OPRT_NOT_SUPPORTED: no OTA callback registered.
 * Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET core_ota_start_notify(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path);

/**
 * @brief This API is used for ota data process
 *
 * @param[in] *pack: point to ota pack
 * @param[in] remain_len: ota pack remain len
 *
 * @return OPRT_OK on success; OPRT_NOT_SUPPORTED: no OTA callback registered
 * (in that case *remain_len is not written and keeps the caller's value).
 * Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET core_ota_data_process(TUYA_OTA_DATA_T *pack, uint32_t *remain_len);

/**
 * @brief This API is used for ota end notify
 *
 * @param[in] reset: ota reset
 *
 * @return OPRT_OK on success; OPRT_NOT_SUPPORTED: no OTA callback registered.
 * Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET core_ota_end_notify(BOOL_T reset);

#ifdef __cplusplus
}
#endif

#endif /* __CORE_OTA_H__ */
