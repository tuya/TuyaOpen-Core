/**
 * @file ota_app.h
 * @brief Application-layer OTA implementation (Linux file-writing variant): 3 callbacks plus registration
 *
 * The implementation lives in the application and is registered with the core
 * through core_ota_register_cbs (see core_ota.h). An application that does not
 * register callbacks does not support OTA (core_ota_* returns
 * OPRT_NOT_SUPPORTED).
 *
 * start_notify opens ./tuyadb/ota ("w+b") and keeps the FILE handle;
 * data_process writes each received pack and reports the trailing 100 bytes of
 * a non-final pack through remain_len (they are delivered again and written
 * with the next pack), leaving remain_len=0 for the final pack; end_notify
 * closes the handle and, for reset=TRUE, prints the success message and exits
 * the process (exit(1), the closest simulation of a reboot on Linux). The cwd
 * must contain a tuyadb/ directory.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __OTA_APP_H__
#define __OTA_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
* @brief ota start notify (registered callback: CORE_OTA_CBS_T.start_notify)
*
* @param[in] image_size:  image size
* @param[in] type:        ota type
* @param[in] path:        ota path
*
* @note This API is used for ota start notify
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET ota_start_notify(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path);

/**
* @brief ota data process (registered callback: CORE_OTA_CBS_T.data_process)
*
* @param[in] pack:       point to ota pack
* @param[out] remain_len: ota pack remain len
*
* @note This API is used for ota data process
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET ota_data_process(TUYA_OTA_DATA_T *pack, uint32_t *remain_len);

/**
* @brief ota end notify (registered callback: CORE_OTA_CBS_T.end_notify)
*
* @param[in] reset:  ota reset
*
* @note This API is used for ota end notify
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET ota_end_notify(BOOL_T reset);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_APP_H__ */
