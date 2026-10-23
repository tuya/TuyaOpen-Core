/**
 * @file agent_audio.h
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

#ifndef __AGENT_AUDIO_H__
#define __AGENT_AUDIO_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Uplink audio parameters: 16kHz / 16bit / mono, 80ms frames (matching the cloud uplink attributes) */
#define AGENT_AUDIO_SAMPLE_RATE 16000
#define AGENT_AUDIO_SAMPLE_BYTES 2 /* 16bit */
#define AGENT_AUDIO_FRAME_MS 80
#define AGENT_AUDIO_FRAME_BYTES (AGENT_AUDIO_SAMPLE_RATE * AGENT_AUDIO_SAMPLE_BYTES * AGENT_AUDIO_FRAME_MS / 1000)

/* Uplink verification timeout: wait for the cloud ASR response (15s is ample) */
#define AGENT_UPLINK_VERIFY_TIMEOUT_MS 15000

/* Upload statistics (used by logs and selftest criteria) */
typedef struct {
    uint32_t frames;           /* number of frames uploaded */
    uint32_t bytes;            /* total bytes uploaded */
    uint32_t last_frame_bytes; /* bytes in the last frame (verifies the boundary for partial frames) */
} agent_audio_stat_t;

/**
 * @brief Upload one utterance (one-shot): open the audio source → start an input round → feed frames → stop the round
 *
 * The AI client must be ready before calling (tuya_ai_agent_is_ready()).
 * After all data is fed, tuya_ai_input_stop() tells the cloud that speech has
 * ended and processing can start.
 *
 * @param[in]  src_path audio source path (Linux: 16kHz/16bit/mono PCM file)
 * @param[out] stat     upload statistics (may be NULL)
 * @return OPRT_OK if all data was uploaded; others see tuya_error_code.h
 */
OPERATE_RET agent_audio_upload_once(const char *src_path, agent_audio_stat_t *stat);

/**
 * @brief Uplink result verification: an ASR response from the cloud is the hard criterion for success
 *
 * A successful ring buffer write does not prove that the uplink works: when the
 * connection is broken, "upload done" only means that local frame feeding
 * finished. Connection-state queries can lag behind and miss the upload window,
 * so this module waits for the cloud ASR response instead.
 *
 * Usage (inside the trigger thread, one-shot): init → upload_once → wait(timeout).
 * Note: the semaphore is designed for a single trigger; extending it to multiple
 * rounds requires resetting the state before each round.
 *
 * @return init: OPRT_OK; wait: OPRT_OK = ASR received (both ASR_OK and ASR_EMPTY
 *         count as a working link), others (timeout/not initialized) = not received
 */
OPERATE_RET agent_audio_uplink_verify_init(void);
OPERATE_RET agent_audio_uplink_verify_wait(uint32_t timeout_ms);

/**
 * @brief Called when an ASR event arrives (posted by the downlink callback;
 *        both ASR_OK and ASR_EMPTY should call it — an empty recognition means
 *        the link works but carries no content, not an upload failure)
 */
void agent_audio_uplink_verify_notify(void);

/**
 * @brief Register the downlink event callback (ai_user_event_notify_register)
 *
 * Prints:
 *   [AGENT][ASR]  <speech recognition result>  (verifies the uploaded speech was recognized)
 *   [AGENT][NLG]  <agent reply text>           (final criterion that the whole link works)
 * Downlink audio packets are handled in ai_agent.c's media_data_cb (played when
 * the AI audio module is enabled, dropped otherwise).
 *
 * @return OPRT_OK on success
 */
OPERATE_RET agent_downlink_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __AGENT_AUDIO_H__ */
