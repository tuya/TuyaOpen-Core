/**
 * @file demo_media_event.h
 * @brief MEDIA_STREAM event callback: local_store playback (query by month/day,
 *        start, pause, speed, seek, cross-segment continuation)
 *
 * Playback runs on a dedicated send thread that reads local_store segments,
 * paces frames by their timestamps, arms on an I frame, and sends video
 * frames through tuya_ipc_media_playback_send_video_frame() (audio frames
 * through the audio send API). P2P conventions: OPRT_RESOURCE_NOT_READY is
 * retried for I frames only, OPRT_NOT_EXIST/OPRT_SOCK_CONN_ERR are treated as
 * session end, drop_unsent is called before seek/segment switch, and
 * video_send_start is issued after the first I frame. All playback handling is
 * compiled only when P2P_DEMO_LOCAL_STORE is defined; otherwise only START and
 * STOP acknowledge the App with send_finish.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __DEMO_MEDIA_EVENT_H__
#define __DEMO_MEDIA_EVENT_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the MEDIA_STREAM event callback (playback query/control)
 * @note Called from tuya_ipc_demo_start() (at which point P2P has already done TUYA_APP_Start/p2p_init)
 */
void demo_media_event_register(void);

/**
 * @brief Stop the playback send thread (if running)
 */
void demo_media_pb_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEMO_MEDIA_EVENT_H__ */
