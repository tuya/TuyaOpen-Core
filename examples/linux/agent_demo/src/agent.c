/**
 * @file agent.c
 * @brief AI link startup orchestration
 *
 * After MQTT connects: initialize the AI agent → register downlink printing
 * (agent_downlink) → start the trigger thread (wait for the client to become
 * ready → upload one preset utterance → use the ASR response as the final
 * criterion). 
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "core_api.h"
#include "ai_agent.h"
#include "tuya_ai_agent.h"
#include "agent_audio.h"
#include "agent.h"

/* Trigger timing: upper bound for waiting on client readiness / wait for session setup after readiness (crt_session is one token round trip) */
#define AGENT_TRIGGER_WAIT_READY_MS (30 * 1000)
#define AGENT_TRIGGER_SESSION_SETTLE_MS (2 * 1000)

static THREAD_HANDLE s_agent_trigger_thread = NULL;
static bool          s_agent_started        = false;

/**
 * @brief Trigger thread: wait for the AI client to become ready → upload one preset utterance (one-shot)
 *
 * The thread must delete itself on exit: the CORE contract (core_thread.c
 * __WrapRunFunc) keeps polling thrdRunSta every 10ms after the user callback
 * returns and only exits and frees once core_thread_delete sets STOP; without
 * self-deletion the thread and its handle would stay resident. The convention
 * follows iot_demo's tuya_app_thread / p2p_demo's playback thread. 
 */
static void __agent_trigger_task(void *arg)
{
    (void)arg;
    uint32_t           waited_ms = 0;
    OPERATE_RET        rt        = OPRT_OK;
    agent_audio_stat_t stat      = {0};

    while (!tuya_ai_agent_is_ready()) {
        if (waited_ms >= AGENT_TRIGGER_WAIT_READY_MS) {
            /* Give up on timeout (one-shot trigger): the 30s bound is far above
             * the typical readiness time (~1s); a timeout indicates a broken
             * link, retrying is pointless, and the log is left for diagnosis. */
            PR_ERR("[AGENT] ai client not ready in %d ms, trigger aborted", AGENT_TRIGGER_WAIT_READY_MS);
            goto __exit;
        }
        core_system_sleep(500);
        waited_ms += 500;
    }
    PR_NOTICE("[AGENT] ai client ready (waited %u ms)", waited_ms);

    /* Client ready != session established: give crt_session (MQTT token round trip) some time */
    core_system_sleep(AGENT_TRIGGER_SESSION_SETTLE_MS);

    PR_NOTICE("[AGENT] trigger: upload one utterance from %s", AGENT_AUDIO_SRC_PATH);

    /* Uplink verification: a ring buffer write still succeeds when the link is
     * down and "upload done" would be a false positive (it only means local frame
     * feeding finished); the connection-state query lags by >=18s and cannot
     * capture the upload window. The cloud ASR response is therefore the final
     * criterion — wait after uploading and report FAILED honestly on timeout. */
    agent_audio_uplink_verify_init();
    rt = agent_audio_upload_once(AGENT_AUDIO_SRC_PATH, &stat);
    PR_NOTICE("[AGENT] upload %s: frames=%u, bytes=%u, last_frame_bytes=%u", (rt == OPRT_OK) ? "done" : "FAILED",
              stat.frames, stat.bytes, stat.last_frame_bytes);

    if (rt == OPRT_OK) {
        if (agent_audio_uplink_verify_wait(AGENT_UPLINK_VERIFY_TIMEOUT_MS) == OPRT_OK) {
            PR_NOTICE("[AGENT] uplink verified (ASR received)");
        } else {
            PR_ERR("[AGENT] upload FAILED: no ASR response in %d ms (uplink broken or cloud unreachable)",
                   AGENT_UPLINK_VERIFY_TIMEOUT_MS);
        }
    }

__exit:
    if (s_agent_trigger_thread) {
        THREAD_HANDLE h        = s_agent_trigger_thread;
        s_agent_trigger_thread = NULL;
        core_thread_delete(h);
    }
}

/**
 * @brief Start the AI link (idempotent): initialize the agent + register downlink printing + start the trigger thread
 *
 * The guard (s_agent_started) is only set after everything succeeded: it is not
 * set when ai_agent_init fails, so the next MQTT_CONNECTED retries naturally
 * (tuya_ai_agent_init cleans up on its failure path, making retries safe; the
 * monitor is non-blocking and cannot create a "half-success" state). If thread
 * creation fails the AI is already initialized and re-initialization is
 * unsafe, so the guard is set and no retry happens (an ERR is logged for
 * diagnosis).
 */
void agent_start(void)
{
    if (s_agent_started) {
        return;
    }

    OPERATE_RET rt = ai_agent_init();
    if (rt != OPRT_OK) {
        PR_ERR("[AGENT] ai_agent_init failed: %d (will retry on next mqtt connect)", rt);
        return;
    }
    PR_NOTICE("[AGENT] ai agent initialized");

    agent_downlink_init();

    THREAD_CFG_T thrd = {0};
    /* Note: stackDepth is ignored on the Linux port (port_thread does not use
     * pthread_attr_setstacksize, so the thread gets the system default stack of
     * 8MB); this value only takes effect on real devices — do not assume the
     * stack has been tuned to 4.6KB. */
    thrd.stackDepth   = 4608;
    thrd.priority     = THREAD_PRIO_2;
    thrd.thrdname     = "agent_trigger";
    if (core_thread_create_and_start(&s_agent_trigger_thread, NULL, NULL, __agent_trigger_task, NULL, &thrd) != OPRT_OK) {
        PR_ERR("[AGENT] trigger thread create failed (ai agent stays initialized, no retry)");
    }

    s_agent_started = true;
}
