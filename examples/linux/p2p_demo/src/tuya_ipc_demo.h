/**
 * @file tuya_ipc_demo.h
 * @brief p2p_demo media chain: sample-file virtual camera source -> frame queue/ring buffer -> P2P frame-fetch callbacks.
 *
 * Data flow:
 *   vcam frame callback -> Annex-B validation -> 2-slot frame queue + ring buffer
 *   (plus optional local_store recording) -> P2P frame-fetch callback
 *   demo_on_get_video_frame_callback (ring buffer first, then the queue).
 *
 * tuya_ipc_demo_start() creates the frame pool and locks, reports the AV
 * capabilities, initializes the ring buffer when ENABLE_IPC_RING_BUFFER is on,
 * initializes local_store when P2P_DEMO_LOCAL_STORE is defined, and registers
 * the media event callback. The camera is opened on the App's live video start
 * and closed on stop; the sample path is resolved from P2P_DEMO_H264_FILE
 * (cwd first, then the executable directory). The audio callbacks are empty
 * implementations (the P2P SDK callback set requires non-NULL pointers); the
 * frame-fetch callback returns -1 when no frame is available.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __TUYA_IPC_DEMO_H__
#define __TUYA_IPC_DEMO_H__

/* This demo requires P2P: a missing switch produces an explicit build error
 * instead of silently compiling an empty shell. This block must precede the
 * includes below -- when P2P is disabled the whole p2p-core tree is not built
 * and even the tuya_ipc_p2p.h include path does not exist; including it first
 * would report "header not found" and hide the real cause (the build error
 * should point at the switch, not at a missing header). */
#if !defined(ENABLE_TUYA_P2P) || (ENABLE_TUYA_P2P != 1)
#error "p2p_demo requires CONFIG_ENABLE_TUYA_P2P=ON (or delete linux/.config / pass -DCONFIG_ENABLE_TUYA_P2P=ON)"
#endif

#include "tuya_cloud_types.h"
#include "tuya_ipc_p2p.h"

/* local_store playback switch: the single macro P2P_DEMO_LOCAL_STORE (injected
 * by CMake as =1 only when CONFIG_P2P_DEMO_LOCAL_STORE and
 * CONFIG_ENABLE_LOCAL_STORE are both on; if only the demo switch is on, CMake
 * warns and does not inject it). The code only needs #ifdef -- "macro present =
 * playback available" is guaranteed at build time, with no second derived macro. */

/**
 * @brief Initialize the demo media chain (ring buffer / vcam / local_store)
 */
void tuya_ipc_demo_start(void);

/**
 * @brief Release the demo media chain resources
 */
void tuya_ipc_demo_end(void);

/**
 * @brief P2P link-down callback
 * @return 0
 */
int demo_on_signal_disconnect_callback(void);

/**
 * @brief App starts watching the live stream
 * @return 0
 */
int demo_on_live_video_start_callback(void);

/**
 * @brief App stops watching the live stream
 * @return 0
 */
int demo_on_live_video_stop_callback(void);

/**
 * @brief P2P video frame fetch (AU)
 * @param[in,out] media_frame output frame
 * @return 0 a frame is available; -1 none for now
 */
int demo_on_get_video_frame_callback(MEDIA_FRAME *media_frame);

/**
 * @brief P2P audio uplink frame fetch (this demo has no audio; empty implementation returns -1)
 * @param[in,out] media_frame output frame
 * @return -1
 */
int demo_on_get_audio_frame_callback(MEDIA_FRAME *media_frame);

/**
 * @brief App starts two-way talk (empty implementation)
 * @return 0
 */
int demo_on_live_audio_start_callback(void);

/**
 * @brief App ends two-way talk (empty implementation)
 * @return 0
 */
int demo_on_live_audio_stop_callback(void);

/**
 * @brief Receive App downlink audio (this demo has no audio; empty implementation)
 * @param[in] media_frame G.711U payload
 * @return 0
 */
int demo_on_recv_audio_frame_callback(MEDIA_FRAME *media_frame);

/**
 * @brief App I-frame request -> demo_vcam_request_i_frame
 * @return 0
 */
int demo_on_request_i_frame_callback(void);

/**
 * @brief App bitrate adjustment (the sample source has no encoder, so the bitrate cannot be changed)
 * @param[in] kbps target bitrate
 * @return -1 (not supported; the internal __demo_camera_set_bitrate returns OPRT_NOT_SUPPORTED)
 */
int demo_on_set_video_bitrate_callback(uint32_t kbps);

/**
 * @brief Resolve this demo's video sample path (cwd first, then the executable directory; P2P_DEMO_H264_FILE can override)
 *
 * Live streaming (vcam) and the playback seed (PB_SEED of demo_media_event)
 * share the same source file.
 *
 * @return readable path (static buffer, do not free); NULL when neither location works
 */
const char *demo_h264_source_path(void);

#endif /* __TUYA_IPC_DEMO_H__ */
