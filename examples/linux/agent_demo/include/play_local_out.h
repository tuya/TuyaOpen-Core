/**
 * @file play_local_out.h
 * @brief AI player platform consumer implementation (Linux ALSA): writes decoded PCM to the sound card
 *
 * Contract: the AI_PLAYER_CONSUMER_T (six callbacks) of
 * agent_demo/src/components/audio_player, injected through
 * tuya_ai_player_set_consumer() (the port-side entry is play_local_out_install).
 *
 * Design:
 *   - this implementation owns the device lifecycle — open opens, close closes —
 *     fully decoupled from the input path;
 *   - the output is fixed at 16k/16bit/mono (the service_init configuration;
 *     the consumer's start takes no format arguments, so the device is
 *     configured with this format in open).
 *
 * Callback semantics (the call sites are in svc_ai_player.c):
 *   open   — called when set_consumer replaces the default consumer loaded by
 *            service_init (this implementation opens the ALSA device and does
 *            hw_params/prepare); if the device cannot be opened it degrades to
 *            "discard output" without blocking the playback link (write returns
 *            an error and svc ignores the return value)
 *   start  — called when the first player enters PLAYING (__switch_player_mode);
 *            prepares the device (after the previous stop's drain the device is
 *            back in SETUP, so it must be prepared again)
 *   write  — the playback thread writes PCM; blocking write + loop until full
 *            (svc ignores the return value, so short writes/failures must be
 *            absorbed here); xrun (-EPIPE) / suspend (-ESTRPIPE) are recovered
 *            through snd_pcm_recover and retried
 *   stop   — called when all players stop; snd_pcm_drain plays out the device
 *            buffer (TTS tail not cut off), failures are ignored (start's
 *            prepare recovers)
 *   close  — called on service exit / set_consumer replacement; drop + close
 *            (does not wait for the buffer)
 *   set_volume — global volume (0-100). No hardware mixer: software gain (16bit
 *            samples multiplied by volume/100 in fixed point), volume=0 writes
 *            zero samples (keeping device timing instead of skipping data).

 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __PLAY_LOCAL_OUT_H__
#define __PLAY_LOCAL_OUT_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inject this port's ALSA consumer into the AI player (tuya_ai_player_set_consumer)
 *
 * Must be called after tuya_ai_player_service_init() (see the file header).
 * It first closes the default empty consumer and then opens the ALSA device; a
 * device open failure is not returned as an error (playback can continue
 * without sound) and the error log gives the reason.
 *
 * @return OPRT_OK once injection is done (including graceful degradation on device open failure); others see tuya_error_code.h
 */
OPERATE_RET play_local_out_install(void);

#ifdef __cplusplus
}
#endif

#endif /* __PLAY_LOCAL_OUT_H__ */
