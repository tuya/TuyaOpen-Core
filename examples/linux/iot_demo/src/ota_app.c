/**
 * @file ota_app.c
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

#include "core_api.h"

#include <stdio.h>
#include <stdlib.h>

#include "ota_app.h"

static FILE *s_upgrade_fd = NULL;

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
OPERATE_RET ota_start_notify(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path)
{
    (void)image_size;
    (void)type;
    (void)path;

    FILE *p_upgrade_fd = (FILE *)s_upgrade_fd;

    if (p_upgrade_fd) {
        fclose(p_upgrade_fd);
    }

    char ota_path[255];

    sprintf(ota_path, "./tuyadb/ota");
    s_upgrade_fd = fopen(ota_path, "w+b");

    if (NULL == s_upgrade_fd) {
        printf("open upgrade file fail. upgrade fail %s\r\n", ota_path);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

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
OPERATE_RET ota_data_process(TUYA_OTA_DATA_T *pack, uint32_t *remain_len)
{
    printf("Rev File Data Total_len %d, Offset:%u Len:%u\r\n", pack->total_len, pack->offset, pack->len);
    FILE *p_upgrade_fd = (FILE *)s_upgrade_fd;

    /* Handle guard: if start_notify failed (fopen failure), the handle is NULL,
     * yet the core ignores the start return value (tuya_ota.c:61) and still
     * delivers DL_EVENT_ON_DATA to this interface -- without this guard
     * fwrite(NULL) would crash. Report the error and do not write *remain_len
     * (keep the value passed in by the caller). */
    if (NULL == p_upgrade_fd) {
        printf("ota data process: upgrade file not opened\r\n");
        return OPRT_COM_ERROR;
    }

    if (pack->offset + pack->len != pack->total_len) {
        fwrite(pack->data, 1, pack->len - 100, p_upgrade_fd);
        *remain_len = 100;
    } else {
        fwrite(pack->data, 1, pack->len, p_upgrade_fd);
        *remain_len = 0;
    }

    return OPRT_OK;
}

/**
* @brief ota end notify (registered callback: CORE_OTA_CBS_T.end_notify)
*
* @param[in] reset:  ota reset
*
* @note This API is used for ota end notify
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET ota_end_notify(BOOL_T reset)
{
    FILE *p_upgrade_fd = (FILE *)s_upgrade_fd;

    /* Note: the core failure paths do not call this interface -- the only call
     * site in the whole core is end_notify(TRUE) on the success path
     * (tuya_ota.c:118); the DL_EVENT_FAULT / hmac verification failure
     * branches send no notification.
     *
     * The reset parameter is TRUE on the successful end path: this
     * implementation prints a message and resets the process (exit(1), the
     * closest simulation of a reboot on Linux). With reset=FALSE it only closes
     * the handle and does not exit. */
    if (p_upgrade_fd) {
        fclose(p_upgrade_fd);
        s_upgrade_fd = NULL;
    }

    if (reset) {
        printf("SOC Upgrade File Download Success\r\n");
        // UserTODO
        exit(1);
    } else {
        printf("SOC Upgrade File Download Fail.ret = %d\r\n", reset);
    }

    return OPRT_OK;
}
