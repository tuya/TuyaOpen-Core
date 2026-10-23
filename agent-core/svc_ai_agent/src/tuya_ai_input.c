/**
 * @file tuya_ai_input.c
 * @brief AI input sessions: per-scode ring buffers that carry uplink audio and
 * text packets into the AI business layer.
 *
 * Each scode keeps an AI_INPUT_SESSION_CTX_T with a ring buffer and a start
 * flag. Audio and text input calls append framed packets to the ring buffer
 * (tuya_ai_audio_input_s / tuya_ai_text_input_s), start/stop control the
 * session, and the deinit path releases every session context.
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
 */
#include "core_memory.h"
#include "core_thread.h"
#include "core_system.h"
#include "core_mutex.h"
#include "tuya_ringbuf.h"
#include "uni_log.h"
#include "tuya_ai_agent.h"
#include "tuya_ai_biz.h"
#include "base_event.h"
#include "tuya_ai_internal.h"
#include "tuya_ai_input.h"
#include "core_queue.h"
#include "core_sw_timer.h"

#ifndef AI_INPUT_STACK_SIZE
#define AI_INPUT_STACK_SIZE (4608)
#endif
#ifndef AI_INPUT_RINGBUF_SIZE
#define AI_INPUT_RINGBUF_SIZE (20*1024)
#endif
#ifndef AI_INPUT_BUF_SIZE
#define AI_INPUT_BUF_SIZE (6*1024)
#endif

#define AI_INPUT_TASK_DELAY   (10)

typedef struct {
    CHAR_T scode[AI_SOLUTION_CODE_LEN];
    BOOL_T queue_sync;
    QUEUE_HANDLE queue;
    UINT32_T lazy_input;
    TUYA_RINGBUFF_T ringbuf;
    AI_INPUT_STATE_E state;
} AI_INPUT_SESSION_CTX_T;

typedef struct {
    THREAD_HANDLE thread;
    MUTEX_HANDLE mutex;
    CHAR_T *input_buf;
    BOOL_T terminate;
    BOOL_T mute_audio;
    AI_INPUT_SESSION_CTX_T *sctx[AI_SESSION_MAX_NUM];
} AI_INPUT_CTX_T;
STATIC AI_INPUT_CTX_T ai_input_ctx;

STATIC AI_INPUT_SESSION_CTX_T* __ai_get_sctx(CHAR_T *scode)
{
    UINT_T idx = 0;
    core_mutex_lock(ai_input_ctx.mutex);
    for (idx = 0; idx < AI_SESSION_MAX_NUM; idx++) {
        if (ai_input_ctx.sctx[idx] != NULL) {
            if ((!scode && ai_input_ctx.sctx[idx]->scode[0] == 0) ||
                (scode && strncmp(ai_input_ctx.sctx[idx]->scode, scode, AI_SOLUTION_CODE_LEN) == 0)) {
                core_mutex_unlock(ai_input_ctx.mutex);
                return ai_input_ctx.sctx[idx];
            }
        }
    }
    core_mutex_unlock(ai_input_ctx.mutex);
    return NULL;
}

STATIC AI_INPUT_SESSION_CTX_T* __ai_crt_sctx(CHAR_T *scode)
{
    OPERATE_RET rt = OPRT_OK;
    UINT_T idx = 0;
    AI_INPUT_SESSION_CTX_T *sctx = __ai_get_sctx(scode);
    if (sctx) {
        return sctx;
    }

    core_mutex_lock(ai_input_ctx.mutex);
    for (idx = 0; idx < AI_SESSION_MAX_NUM; idx++) {
        if (ai_input_ctx.sctx[idx] == NULL) {
            ai_input_ctx.sctx[idx] = (AI_INPUT_SESSION_CTX_T *)Malloc(SIZEOF(AI_INPUT_SESSION_CTX_T));
            if (ai_input_ctx.sctx[idx] == NULL) {
                core_mutex_unlock(ai_input_ctx.mutex);
                PR_ERR("malloc session ctx failed");
                return NULL;
            }
            memset(ai_input_ctx.sctx[idx], 0, SIZEOF(AI_INPUT_SESSION_CTX_T));
            if (scode) {
                strncpy(ai_input_ctx.sctx[idx]->scode, scode, AI_SOLUTION_CODE_LEN);
            }
            rt = core_queue_create_init(&ai_input_ctx.sctx[idx]->queue, SIZEOF(AI_INPUT_STATE_E), 3);
            if (rt != OPRT_OK) {
                PR_ERR("create input queue failed");
                Free(ai_input_ctx.sctx[idx]);
                ai_input_ctx.sctx[idx] = NULL;
                core_mutex_unlock(ai_input_ctx.mutex);
                return NULL;
            }
            core_mutex_unlock(ai_input_ctx.mutex);
            return ai_input_ctx.sctx[idx];
        }
    }
    PR_ERR("crt ctx full");
    core_mutex_unlock(ai_input_ctx.mutex);
    return NULL;
}


STATIC TUYA_RINGBUFF_T __ai_crt_ringbuf(CHAR_T *scode)
{
    OPERATE_RET rt = OPRT_OK;
    AI_INPUT_SESSION_CTX_T *sctx = __ai_get_sctx(scode);
    if (sctx == NULL) {
        return NULL;
    }
    if (sctx && sctx->ringbuf) {
        return sctx->ringbuf;
    }

    core_mutex_lock(ai_input_ctx.mutex);
    rt = tuya_ring_buff_create(AI_INPUT_RINGBUF_SIZE, OVERFLOW_STOP_TYPE, &sctx->ringbuf);
    if (rt != OPRT_OK) {
        PR_ERR("create input ringbuf failed");
        core_mutex_unlock(ai_input_ctx.mutex);
        return NULL;
    }
    core_mutex_unlock(ai_input_ctx.mutex);
    return sctx->ringbuf;
}

STATIC VOID __ai_free_sctx(VOID)
{
    UINT_T idx = 0;
    for (idx = 0; idx < AI_SESSION_MAX_NUM; idx++) {
        if (ai_input_ctx.sctx[idx] != NULL) {
            if (ai_input_ctx.sctx[idx]->ringbuf) {
                tuya_ring_buff_free(ai_input_ctx.sctx[idx]->ringbuf);
                ai_input_ctx.sctx[idx]->ringbuf = NULL;
            }
            if (ai_input_ctx.sctx[idx]->queue) {
                core_queue_free(ai_input_ctx.sctx[idx]->queue);
                ai_input_ctx.sctx[idx]->queue = NULL;
            }
            Free(ai_input_ctx.sctx[idx]);
            ai_input_ctx.sctx[idx] = NULL;
        }
    }
}

STATIC OPERATE_RET __ai_ringbuf_write(AI_RINGBUF_HEAD_T *head, BYTE_T *data)
{
    UINT_T rt = 0;
    // if ((ai_input_ctx.state != AI_INPUT_PROC) && (ai_input_ctx.state != AI_INPUT_STOPPING)) {
    //     return OPRT_OK;
    // }
    if (ai_input_ctx.mute_audio) {
        return OPRT_OK;
    }

    if ((data == NULL || head->len == 0 || head->type != AI_PT_AUDIO)) {
        return OPRT_OK;
    }
    if (head->len > AI_INPUT_BUF_SIZE) {
        PR_ERR("input data len is too long %d, type:%d", head->len, head->type);
        return OPRT_INVALID_PARM;
    }

    TUYA_RINGBUFF_T ringbuf = __ai_crt_ringbuf(head->scode);
    if (ringbuf == NULL) {
        return OPRT_COM_ERROR;
    }

    core_mutex_lock(ai_input_ctx.mutex);
    UINT_T free_size = tuya_ring_buff_free_size_get(ringbuf);
    if (free_size < (SIZEOF(AI_RINGBUF_HEAD_T) + head->len)) {
        core_mutex_unlock(ai_input_ctx.mutex);
        return OPRT_RESOURCE_NOT_READY;
    }
    rt = tuya_ring_buff_write(ringbuf, (VOID_T *)head, SIZEOF(AI_RINGBUF_HEAD_T));
    if (rt != SIZEOF(AI_RINGBUF_HEAD_T)) {
        PR_ERR("input ring buf write head failed %d %d", rt, SIZEOF(AI_RINGBUF_HEAD_T));
        goto EXIT;
    }
    rt = tuya_ring_buff_write(ringbuf, data, head->len);
    if (rt != head->len) {
        PR_ERR("ring buf write pbuf failed %d %d", rt, head->len);
        goto EXIT;
    }
    core_mutex_unlock(ai_input_ctx.mutex);
    return OPRT_OK;

EXIT:
    tuya_ring_buff_reset(ringbuf);
    core_mutex_unlock(ai_input_ctx.mutex);
    return OPRT_COM_ERROR;
}

STATIC OPERATE_RET __ai_ringbuf_read(AI_INPUT_SESSION_CTX_T *sctx, AI_RINGBUF_HEAD_T *head, CHAR_T *buf)
{
    OPERATE_RET rt = OPRT_OK;
    UINT_T read_len = 0, total_len = 0;
    if (sctx == NULL || sctx->ringbuf == NULL || head == NULL || buf == NULL) {
        return OPRT_INVALID_PARM;
    }

    while (total_len < AI_INPUT_BUF_SIZE) {
        if (sctx->state == AI_INPUT_STOP) {
            break;
        }
        read_len = tuya_ring_buff_peek(sctx->ringbuf, (VOID_T *)head, SIZEOF(AI_RINGBUF_HEAD_T));
        if (read_len != SIZEOF(AI_RINGBUF_HEAD_T)) {
            break;
        }
        if (head->len + total_len > AI_INPUT_BUF_SIZE) {
            break;
        }
        read_len = tuya_ring_buff_read(sctx->ringbuf, head, SIZEOF(AI_RINGBUF_HEAD_T));
        if (read_len != SIZEOF(AI_RINGBUF_HEAD_T)) {
            break;
        }
        read_len = tuya_ring_buff_read(sctx->ringbuf, buf + total_len, head->len);
        if (read_len != head->len) {
            PR_ERR("ring buf read pbuf failed %d %d", read_len, head->len);
            rt = OPRT_COM_ERROR;
            break;
        }
        total_len += read_len;
    }
    head->len = total_len;
    head->total_len = total_len;
    return rt;
}

STATIC BOOL_T tuya_ai_input_is_started_s(CHAR_T *scode)
{
    UINT_T cnt = 0;
    AI_INPUT_SESSION_CTX_T *sctx = __ai_get_sctx(scode);
    if (sctx == NULL) {
        PR_ERR("scode not exist");
        return FALSE;
    }

    while (!sctx->queue_sync) {
        core_system_sleep(50);
        if (cnt++ > 100) {
            PR_ERR("input failed, ai input not started");
            return FALSE;
        }
    }
    return TRUE;
}

OPERATE_RET tuya_ai_audio_input_s(CHAR_T *scode, UINT64_T timestamp, UINT64_T pts, BYTE_T *data, UINT_T len, UINT_T total_len)
{
    OPERATE_RET rt = OPRT_OK;
    UINT_T cnt = 0;
    AI_RINGBUF_HEAD_T head = {0};
    head.type = AI_PT_AUDIO;
    head.len = len;
    head.total_len = total_len;
    head.biz.audio.timestamp = timestamp;
    head.biz.audio.pts = pts;
    if (scode) {
        strncpy(head.scode, scode, AI_SOLUTION_CODE_LEN);
    }
    rt = __ai_ringbuf_write(&head, data);
    while (rt == OPRT_RESOURCE_NOT_READY) {
        core_system_sleep(10);
        rt = __ai_ringbuf_write(&head, data);
        if (cnt++ > 1000) {
            PR_ERR("audio input failed %d", rt);
            break;
        }
    }
    return rt;
}

OPERATE_RET tuya_ai_text_input_s(CHAR_T *scode, BYTE_T *data, UINT_T len, UINT_T total_len)
{
    if (!tuya_ai_input_is_started_s(scode)) {
        return OPRT_RESOURCE_NOT_READY;
    }
    return tuya_ai_agent_upload_stream(scode, AI_PT_TEXT, NULL, (CHAR_T *)data, len, total_len);
}

VOID tuya_ai_input_start_s(CHAR_T *scode, BOOL_T force)
{
    OPERATE_RET rt = OPRT_OK;
    if (!tuya_ai_agent_is_ready()) {
        return;
    }

    AI_INPUT_SESSION_CTX_T *sctx = __ai_crt_sctx(scode);
    if (sctx == NULL) {
        return;
    }

    AI_INPUT_STATE_E state = AI_INPUT_IDLE;
    if (force) {
        state = AI_INPUT_START;
    } else {
        state = AI_INPUT_START_LAZY;
    }

    sctx->queue_sync = FALSE;
    rt = core_queue_post(sctx->queue, &state, 0);
    if (OPRT_OK != rt) {
        PR_ERR("queue post err, rt:%d", rt);
    } else {
        PR_DEBUG("ai input start, state:%d", state);
    }
    return;
}

VOID tuya_ai_input_stop_s(CHAR_T *scode)
{
    OPERATE_RET rt = OPRT_OK;
    UINT_T cnt = 0;
    if (!tuya_ai_agent_is_ready()) {
        return;
    }

    AI_INPUT_SESSION_CTX_T *sctx = __ai_get_sctx(scode);
    if (sctx == NULL) {
        return;
    }

    AI_INPUT_STATE_E state = AI_INPUT_STOPPING;
    sctx->lazy_input = 0;
    sctx->queue_sync = FALSE;

    rt = core_queue_post(sctx->queue, &state, 0);
    if (OPRT_OK != rt) {
        PR_ERR("queue post err, rt:%d", rt);
    } else {
        PR_DEBUG("ai input stop, state:%d", state);
        while (!sctx->queue_sync) {
            core_system_sleep(100);
            if (cnt++ >= 5) {
                PR_ERR("ai input stop timeout, cnt:%d", cnt);
                break;
            }
        }
    }
    return;
}

VOID tuya_ai_input_deinit(VOID)
{
    ai_input_ctx.terminate = TRUE;
}

STATIC VOID __ai_input_free(VOID)
{
    if (ai_input_ctx.thread) {
        core_thread_delete(ai_input_ctx.thread);
        ai_input_ctx.thread = NULL;
    }
    if (ai_input_ctx.input_buf) {
        Free(ai_input_ctx.input_buf);
        ai_input_ctx.input_buf = NULL;
    }
    if (ai_input_ctx.mutex) {
        core_mutex_release(ai_input_ctx.mutex);
        ai_input_ctx.mutex = NULL;
    }
    __ai_free_sctx();
    memset(&ai_input_ctx, 0, SIZEOF(ai_input_ctx));
    PR_DEBUG("ai input deinit");
}

STATIC VOID __ai_input_thread(VOID* arg)
{
    OPERATE_RET rt = OPRT_OK;
    UINT_T idx = 0;
    AI_INPUT_STATE_E queue_state = AI_INPUT_IDLE, new_state = AI_INPUT_IDLE;
    AI_RINGBUF_HEAD_T head = {0};

    while (!ai_input_ctx.terminate && core_thread_get_state(ai_input_ctx.thread) == THREAD_STATE_RUNNING) {
        for (idx = 0; idx < AI_SESSION_MAX_NUM; idx++) {
            BOOL_T need_unlock = TRUE;
            core_mutex_lock(ai_input_ctx.mutex);
            if (ai_input_ctx.sctx[idx] != NULL) {
                AI_INPUT_SESSION_CTX_T sctx = {0};
                memcpy(&sctx, ai_input_ctx.sctx[idx], SIZEOF(AI_INPUT_SESSION_CTX_T));
                core_mutex_unlock(ai_input_ctx.mutex);
                need_unlock = FALSE;
                queue_state = new_state = sctx.state;
                if (core_queue_fetch(sctx.queue, &queue_state, 0) == 0) {
                    PR_DEBUG("recv queue state %d, scode %s", queue_state, sctx.scode);
                }
                switch (queue_state) {
                case AI_INPUT_START: {
                    tuya_ai_agent_event_s(sctx.scode, AI_EVENT_CHAT_BREAK, 0);
                    tuya_ai_output_stop(TRUE);
                    if (tuya_ai_agent_is_ready()) {
                        new_state = AI_INPUT_PROC;
                        rt = tuya_ai_agent_start(sctx.scode);
                        if (OPRT_OK != rt) {
                            new_state = AI_INPUT_STOP;
                        }
                    } else {
                        tuya_ai_output_alert(AT_NETWORK_FAIL);
                        new_state = AI_INPUT_IDLE;
                    }
                    PR_DEBUG("start queue sync");
                    core_mutex_lock(ai_input_ctx.mutex);
                    if (ai_input_ctx.sctx[idx] != NULL) {
                        ai_input_ctx.sctx[idx]->state = new_state;
                        ai_input_ctx.sctx[idx]->queue_sync = TRUE;
                    }
                    core_mutex_unlock(ai_input_ctx.mutex);
                    need_unlock = FALSE;
                }
                break;
                case AI_INPUT_START_LAZY: {
                    if (tuya_ai_agent_is_ready()) {
                        new_state = AI_INPUT_PROC;
                        rt = tuya_ai_agent_start(sctx.scode);
                        if (OPRT_OK != rt) {
                            new_state = AI_INPUT_STOP;
                        }
                    } else {
                        tuya_ai_output_alert(AT_NETWORK_FAIL);
                        new_state = AI_INPUT_IDLE;
                    }
                    PR_DEBUG("start lazy queue sync");
                    core_mutex_lock(ai_input_ctx.mutex);
                    if (ai_input_ctx.sctx[idx] != NULL) {
                        ai_input_ctx.sctx[idx]->state = new_state;
                        ai_input_ctx.sctx[idx]->queue_sync = TRUE;
                    }
                    core_mutex_unlock(ai_input_ctx.mutex);
                    need_unlock = FALSE;
                }
                break;
                case AI_INPUT_PROC: {
                    core_mutex_lock(ai_input_ctx.mutex);
                    rt = __ai_ringbuf_read(&sctx, &head, ai_input_ctx.input_buf);
                    core_mutex_unlock(ai_input_ctx.mutex);
                    need_unlock = FALSE;
                    if ((rt == OPRT_OK) && (head.len > 0)) {
                        tuya_ai_agent_upload_stream(sctx.scode, head.type, &head.biz, ai_input_ctx.input_buf, head.len, head.total_len);
                    }
                }
                break;
                case AI_INPUT_STOPPING: {
                    core_mutex_lock(ai_input_ctx.mutex);
                    if (sctx.lazy_input < 30) {
                        rt = __ai_ringbuf_read(&sctx, &head, ai_input_ctx.input_buf);
                        if ((rt == OPRT_OK) && (head.len > 0)) {
                            new_state = AI_INPUT_STOPPING;
                        } else {
                            new_state = AI_INPUT_STOP;
                        }
                    } else {
                        new_state = AI_INPUT_STOP;
                    }
                    if (ai_input_ctx.sctx[idx] != NULL) {
                        ai_input_ctx.sctx[idx]->state = new_state;
                        ai_input_ctx.sctx[idx]->lazy_input++;
                    }
                    core_mutex_unlock(ai_input_ctx.mutex);
                    need_unlock = FALSE;
                    if ((rt == OPRT_OK) && (head.len > 0)) {
                        tuya_ai_agent_upload_stream(sctx.scode, head.type, &head.biz, ai_input_ctx.input_buf, head.len, head.total_len);
                    }
                }
                break;
                case AI_INPUT_STOP: {
                    tuya_ai_agent_end(sctx.scode);
                    core_mutex_lock(ai_input_ctx.mutex);
                    if (ai_input_ctx.sctx[idx] != NULL) {
                        tuya_ring_buff_reset(ai_input_ctx.sctx[idx]->ringbuf);
                        ai_input_ctx.sctx[idx]->state = AI_INPUT_IDLE;
                        ai_input_ctx.sctx[idx]->queue_sync = TRUE;
                    }
                    core_mutex_unlock(ai_input_ctx.mutex);
                    need_unlock = FALSE;
                }
                break;
                case AI_INPUT_IDLE:
                    break;
                default:
                    break;
                }
            }

            if (need_unlock) {
                core_mutex_unlock(ai_input_ctx.mutex);
            }
        }
        core_system_sleep(AI_INPUT_TASK_DELAY);
    }
    __ai_input_free();
}

OPERATE_RET tuya_ai_input_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    if (ai_input_ctx.input_buf) {
        return OPRT_OK;
    }
    memset(&ai_input_ctx, 0, SIZEOF(ai_input_ctx));
    TUYA_CALL_ERR_RETURN(core_mutex_create_init(&ai_input_ctx.mutex));
    ai_input_ctx.input_buf = Malloc(AI_INPUT_BUF_SIZE);
    TUYA_CHECK_NULL_GOTO(ai_input_ctx.input_buf, EXIT);

    THREAD_CFG_T thrd_param = {0};
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = "ai_agent_input";
    thrd_param.stackDepth = AI_INPUT_STACK_SIZE;
    rt = core_thread_create_and_start(&ai_input_ctx.thread, NULL, NULL, __ai_input_thread, NULL, &thrd_param);
    if (OPRT_OK != rt) {
        PR_ERR("ai inpput thread create err, rt:%d", rt);
        goto EXIT;
    }
    PR_DEBUG("ai input init success");
    return rt;

EXIT:
    __ai_input_free();
    return rt;
}

VOID tuya_ai_input_mute_audio(BOOL_T mute)
{
    ai_input_ctx.mute_audio = mute;
}