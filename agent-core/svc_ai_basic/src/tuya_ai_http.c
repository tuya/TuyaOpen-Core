/**
 * @file tuya_ai_http.c
 * @author tuya
 * @brief AI HTTP media download entry points and secret-key retrieval.
 * @version 0.1
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * tuya_ai_http_dld_audio/image/file build an AI_HTTP_DLD_T descriptor and
 * schedule __ai_http_dld_work on the system work queue. The worker fills the
 * media attributes (codec, format, dimensions, fps; URL payload types are
 * converted to raw under AI_VERSION 2), opens an HTTP session through
 * http_manager, sends a GET request and checks for a 200/201 response with a
 * body. Media body download is not implemented on this side: the stored
 * callback is not invoked, the request is rejected with an error log, and the
 * descriptor, session and URL helper are released.
 *
 * tuya_ai_http_get_secret_key() posts to "thing.km.secret.get.device" and
 * Base64-decodes "secretValue" into the caller's key buffer; it only performs
 * this request under AI_SUB_VERSION 2 and otherwise returns OPRT_OK without
 * touching the buffer.
 */
#include <stdio.h>
#include <string.h>
#include "uni_log.h"
#include "http_inf.h"
#include "http_manager.h"
#include "mix_method.h"
#include "core_workq_service.h"
#include "tuya_ai_private.h"
#include "tuya_ai_biz.h"
#include "tuya_ai_http.h"
#include "tuya_ai_agent.h"
#include "tuya_ai_input.h"
#include "ty_cJSON.h"
#include "tuya_devos_utils.h"
#include "uni_base64.h"

typedef struct {
    CHAR_T        *url;
    AI_PACKET_PT   type;
    AI_BIZ_RECV_CB cb;
} AI_HTTP_DLD_T;

STATIC VOID __ai_http_dld_work(VOID *data)
{
    OPERATE_RET        rt           = OPRT_OK;
    SESSION_ID         http_sesion  = NULL;
    AI_HTTP_DLD_T     *dld          = (AI_HTTP_DLD_T *)data;
    AI_PACKET_PT       type         = dld->type;
    CHAR_T            *url          = dld->url;
    S_HTTP_MANAGER    *http_manager = NULL;
    AI_BIZ_HEAD_INFO_T head         = {0};
    AI_BIZ_ATTR_INFO_T attr         = {0};
    attr.type                       = type;
    attr.flag                       = AI_HAS_ATTR;
#if defined(AI_VERSION) && (0x02 == AI_VERSION)
    AI_ATTR_BASE_T *down_attr = tuya_ai_agent_get_down_attr();
#endif
    if (type == AI_PT_IMAGE) {
#if defined(AI_VERSION) && (0x02 == AI_VERSION)
        if (down_attr && (down_attr->image.type == IMAGE_PAYLOAD_TYPE_URL)) {
            attr.value.image.base.type   = IMAGE_PAYLOAD_TYPE_RAW;
            attr.value.image.base.format = down_attr->image.format;
            attr.value.image.base.width  = down_attr->image.width;
            attr.value.image.base.height = down_attr->image.height;
        } else {
            attr.value.image.base.format = IMAGE_FORMAT_JPEG;
            attr.value.image.base.width  = 480;
            attr.value.image.base.height = 480;
        }
#else
        attr.value.image.base.format = IMAGE_FORMAT_JPEG;
        attr.value.image.base.width  = 480;
        attr.value.image.base.height = 480;
#endif
    } else if (type == AI_PT_AUDIO) {
        attr.value.audio.base.codec_type = AUDIO_CODEC_MP3;
    } else if (type == AI_PT_VIDEO) {
        attr.value.video.base.codec_type  = VIDEO_CODEC_H264;
        attr.value.video.base.fps         = 30;
        attr.value.video.base.width       = 480;
        attr.value.video.base.height      = 480;
        attr.value.video.base.sample_rate = 90000;
    } else if (type == AI_PT_FILE) {
#if defined(AI_VERSION) && (0x02 == AI_VERSION)
        if (down_attr && (down_attr->file.type == FILE_PAYLOAD_TYPE_URL)) {
            attr.value.file.base.type   = FILE_PAYLOAD_TYPE_RAW;
            attr.value.file.base.format = down_attr->file.format;
            memcpy(attr.value.file.base.file_name, down_attr->file.file_name, SIZEOF(down_attr->file.file_name));
        } else {
            attr.value.file.base.format = FILE_FORMAT_MP4;
        }
#else
        attr.value.file.base.format = FILE_FORMAT_MP4;
#endif
    }

    HTTP_URL_H_S *hu_h = create_http_url_h(0, 10);
    TUYA_CHECK_NULL_GOTO(hu_h, EXIT);
    fill_url_head(hu_h, url);

    http_manager = get_http_manager_instance();
    TUYA_CHECK_NULL_GOTO(http_manager, EXIT);

    http_sesion = http_manager->create_http_session(url, TRUE);
    TUYA_CHECK_NULL_GOTO(http_sesion, EXIT);

    http_req_t req = {
        .type = HTTP_GET,
        .resource = hu_h->buf,
        .version = HTTP_VER_1_1,
    };

    rt = http_manager->send_http_request(http_sesion, &req, 0);
    if (OPRT_OK != rt) {
        PR_ERR("http send request failed, rt:%d", rt);
        goto EXIT;
    }

    http_resp_t *resp = NULL;
    rt = http_manager->receive_http_response(http_sesion, &resp);
    if ((OPRT_OK != rt) || (!resp) || (resp->status_code != 200 && resp->status_code != 201)) {
        PR_ERR("put fail %d,code %d", rt, resp ? resp->status_code : 0xff);
        // PR_ERR("http max header size:%d,security level:%d", HTTP_MAX_REQ_RESP_HDR_SIZE, TUYA_SECURITY_LEVEL);
        goto EXIT;
    }

    if (0 == resp->content_length && !(resp->chunked)) {
        PR_ERR("http head err length %d, chunked %d", resp->content_length, resp->chunked);
        goto EXIT;
    }

    /* Media URL download is not implemented on the agent-core side: the
     * download request is rejected and an error is logged. */
    PR_ERR("http media download not implemented (agent-core), abort");
    rt = OPRT_COM_ERROR;

EXIT:
    if (dld) {
        if (dld->url) {
            OS_FREE(dld->url);
        }
        OS_FREE(dld);
    }
    if (http_sesion) {
        http_manager->destory_http_session(http_sesion);
    }
    if (hu_h) {
        del_http_url_h(hu_h);
    }
    return;
}

STATIC OPERATE_RET __ai_http_dld_media(CHAR_T *url, AI_PACKET_PT type, AI_BIZ_RECV_CB cb)
{
    OPERATE_RET rt = OPRT_OK;
    TUYA_CHECK_NULL_RETURN(url, OPRT_INVALID_PARM);
    AI_HTTP_DLD_T *dld = (AI_HTTP_DLD_T *)OS_MALLOC(SIZEOF(AI_HTTP_DLD_T));
    TUYA_CHECK_NULL_RETURN(dld, OPRT_MALLOC_FAILED);
    memset(dld, 0, SIZEOF(AI_HTTP_DLD_T));
    dld->type = type;
    dld->cb   = cb;
    dld->url  = mm_strdup(url);
    if (dld->url == NULL) {
        PR_ERR("malloc url failed");
        Free(dld);
        return OPRT_MALLOC_FAILED;
    }
    PR_DEBUG("do http dld work");
    rt             = core_workq_schedule(WORKQ_SYSTEM, __ai_http_dld_work, dld);
    if (OPRT_OK != rt) {
        PR_ERR("schedule workq err,rt:%d", rt);
        Free(dld->url);
        Free(dld);
    }
    return rt;
}

OPERATE_RET tuya_ai_http_dld_audio(CHAR_T *url, AI_BIZ_RECV_CB cb)
{
    return __ai_http_dld_media(url, AI_PT_AUDIO, cb);
}

OPERATE_RET tuya_ai_http_dld_image(CHAR_T *url, AI_BIZ_RECV_CB cb)
{
    return __ai_http_dld_media(url, AI_PT_IMAGE, cb);
}

OPERATE_RET tuya_ai_http_dld_file(CHAR_T *url, AI_BIZ_RECV_CB cb)
{
    return __ai_http_dld_media(url, AI_PT_FILE, cb);
}

OPERATE_RET tuya_ai_http_get_secret_key(UCHAR_T *key)
{
#if defined(AI_SUB_VERSION) && (0x02 == AI_SUB_VERSION)
    TUYA_CHECK_NULL_RETURN(key, OPRT_INVALID_PARM);
    OPERATE_RET rt      = OPRT_OK;
    INT_T       dec_len = 0;
    ty_cJSON   *result  = NULL;

    memset(key, 0, TUYA_AI_SECRET_KEY_LEN);
    rt = iot_httpc_common_post("thing.km.secret.get.device", "1.0", NULL, get_gw_dev_id(),
                NULL, NULL, &result);
    if ((rt != OPRT_OK) || (result == NULL)) {
        PR_ERR("http get secret failed, rt:%d", rt);
        return rt;
    }
    ty_cJSON *secret = ty_cJSON_GetObjectItem(result, "secretValue");
    if (secret && secret->valuestring) {
        dec_len = tuya_base64_decode(secret->valuestring, key);
        if (dec_len != (INT_T)TUYA_AI_SECRET_KEY_LEN) {
            PR_ERR("secret key length err:%d expect:%u", dec_len, (UINT_T)TUYA_AI_SECRET_KEY_LEN);
            memset(key, 0, TUYA_AI_SECRET_KEY_LEN);
            rt = OPRT_COM_ERROR;
        }
    } else {
        PR_ERR("get secret key failed");
        rt = OPRT_CJSON_GET_ERR;
    }
    ty_cJSON_Delete(result);
    return rt;
#endif
    return OPRT_OK;
}