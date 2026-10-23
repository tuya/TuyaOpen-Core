/**
 * @file agent_audio.c
 * @brief agent_demo audio link glue: uplink feeding (read the sample and feed the cloud frame by frame) + downlink printing (ASR/NLG)
 *
 * Uplink: fetch data through the play_local_in_* contract → feed frames with
 * tuya_ai_audio_input (agent_audio_upload_once).
 * Downlink: register ai_user_event callbacks that print ASR results and NLG
 * reply text; ASR events also serve as the uplink verification signal (notify)
 * that wakes the trigger thread.
 *
 * This file provides both interfaces: the uplink feeding entry
 * (agent_audio_upload_once) and the downlink printing registration
 * (agent_downlink_init). The downlink ASR callback posts the uplink
 * verification semaphore that the trigger thread waits on.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "core_api.h"

#include "tuya_ai_input.h"
#include "play_local_in.h"
#include "ai_user_event.h"

#include "agent_audio.h"

#include <stdio.h>

/* ============================ Uplink: feeding and verification ============================ */

/* Uplink verification semaphore: posted by ASR events (downlink callback),
 * waited on by the trigger thread. Designed for a single trigger (one-shot
 * demo); extending it to multiple rounds requires resetting the state per round. */
static SEM_HANDLE s_uplink_verify_sem = NULL;

OPERATE_RET agent_audio_uplink_verify_init(void)
{
    if (s_uplink_verify_sem) {
        return OPRT_OK; /* already created (idempotent) */
    }

    return core_semaphore_create_init(&s_uplink_verify_sem, 0, 1);
}

OPERATE_RET agent_audio_uplink_verify_wait(uint32_t timeout_ms)
{
    if (!s_uplink_verify_sem) {
        return OPRT_INVALID_PARM;
    }

    return core_semaphore_wait(s_uplink_verify_sem, timeout_ms);
}

void agent_audio_uplink_verify_notify(void)
{
    if (s_uplink_verify_sem) {
        core_semaphore_post(s_uplink_verify_sem);
    }
}

OPERATE_RET agent_audio_upload_once(const char *src_path, agent_audio_stat_t *stat)
{
    OPERATE_RET rt = OPRT_OK;
    uint8_t     frame[AGENT_AUDIO_FRAME_BYTES];
    agent_audio_stat_t local_stat = {0};

    if (!src_path) {
        return OPRT_INVALID_PARM;
    }

    rt = play_local_in_open(src_path);
    if (rt != OPRT_OK) {
        PR_ERR("[AGENT] audio source open failed: %s, rt:%d", src_path, rt);
        return rt;
    }

    /* Start an input round (force=true: abort any leftover session first) */
    tuya_ai_input_start(true);

    for (;;) {
        INT_T n = play_local_in_read(frame, sizeof(frame));
        if (n < 0) {
            PR_ERR("[AGENT] audio source read error");
            rt = OPRT_COM_ERROR;
            break;
        }
        if (n == 0) {
            /* EOF: the utterance is fully read */
            break;
        }

        /* timestamp/pts: both take the current millisecond */
        uint64_t ts = core_system_get_millisecond();
        rt = tuya_ai_audio_input(ts, ts, frame, (UINT_T)n, (UINT_T)n);
        if (rt != OPRT_OK) {
            PR_ERR("[AGENT] audio upload failed at frame %u, rt:%d", local_stat.frames, rt);
            break;
        }

        local_stat.frames++;
        local_stat.bytes += (uint32_t)n;
        local_stat.last_frame_bytes = (uint32_t)n;
    }

    /* End the input round: the cloud starts ASR and agent processing based on this */
    tuya_ai_input_stop();
    play_local_in_close();

    /* Defensive: a 0-byte upload is meaningless (empty file / abnormal source), treat it as a failure to avoid a false "upload done" */
    if ((rt == OPRT_OK) && (0 == local_stat.bytes)) {
        PR_ERR("[AGENT] audio source empty (0 bytes), treat as failure");
        rt = OPRT_COM_ERROR;
    }

    if (stat) {
        *stat = local_stat;
    }

    return rt;
}

/* ============================ Downlink: event callbacks ============================ */

static void __agent_downlink_event(AI_NOTIFY_EVENT_T *event)
{
    /* Payload lifetime: ai_user_event_notify (utility/src/ai_user_event.c) calls
     * the callback synchronously; the payload points to an AI_NOTIFY_TEXT_T on
     * the producer's stack (ai_skill.c __ai_nlg_process) whose .data points to
     * that frame's cJSON string (root is deleted only after tuya_ai_output_text
     * returns). Reading it inside this callback is therefore always valid; if
     * consumption ever becomes asynchronous/deferred, it must be copied first —
     * dereferencing after the callback returns yields datalen=65535/data=0x44
     * (the stack is gone) and the content is overwritten after cJSON_Delete. */
    if (!event) {
        return;
    }

    switch (event->type) {
    case AI_USER_EVT_ASR_OK: {
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        if (text && text->data) {
            PR_NOTICE("[AGENT][ASR] %.*s", text->datalen, text->data);
        }
        /* Uplink verification: the cloud ASR came back (hard evidence the link works), wake the trigger thread */
        agent_audio_uplink_verify_notify();
    } break;

    case AI_USER_EVT_ASR_EMPTY:
        PR_WARN("[AGENT][ASR] <empty>");
        /* Empty recognition = the link works but has no content, not an upload failure — it also counts as verified */
        agent_audio_uplink_verify_notify();
        break;

    case AI_USER_EVT_TEXT_STREAM_START: {
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        PR_NOTICE("[AGENT][NLG] >> %.*s", text ? text->datalen : 0, (text && text->data) ? text->data : "");
    } break;

    case AI_USER_EVT_TEXT_STREAM_DATA: {
        /* Stream chunk: print as-is (no prefix, no newline) */
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        if (text && text->data && text->datalen > 0) {
            printf("%.*s", text->datalen, text->data);
            fflush(stdout);
        }
    } break;

    case AI_USER_EVT_TEXT_STREAM_STOP: {
        /* The final segment (eof=1) of a multi-segment stream is carried by STOP
         * as that packet's content (ai_skill.c:173-175, comment "append final
         * content (may be empty)") — when non-empty it must be printed,
         * otherwise the terminal misses the last piece of text (in the
         * single-frame case STOP is an explicit empty packet and is unaffected).
         * Print the content first, then the stream end (the content continues
         * at the end of the previous line). */
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        if (text && text->data && text->datalen > 0) {
            printf("%.*s", text->datalen, text->data);
            fflush(stdout);
        }
        PR_NOTICE("[AGENT][NLG] << (stream end)");
    } break;

    case AI_USER_EVT_CHAT_BREAK:
        PR_NOTICE("[AGENT] chat break (cloud interrupted)");
        break;

    case AI_USER_EVT_EXIT:
        PR_NOTICE("[AGENT] chat exit");
        break;

    default:
        /* Other events (TTS playback control, emotion, skills, etc.) are not handled by this demo */
        break;
    }
}

OPERATE_RET agent_downlink_init(void)
{
    ai_user_event_notify_register(__agent_downlink_event);
    PR_NOTICE("[AGENT] downlink handler registered (text events; TTS/music audio via ai_audio_player)");

    return OPRT_OK;
}
