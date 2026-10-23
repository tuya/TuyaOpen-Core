/**
 * @file tuya_ipc_demo.c
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "tuya_ipc_demo.h"
#include "core_log.h"
#include "core_memory.h"
#include "core_mutex.h"
#include "core_system.h"
#include "demo_media_event.h"
#include "demo_vcam.h"

/* Same clock base as the P2P stack: this symbol has no public header, so it is declared extern here */
extern uint64_t tuya_p2p_misc_get_current_time_ms(void);

#if defined(ENABLE_IPC_RING_BUFFER) && (ENABLE_IPC_RING_BUFFER == 1)
#include "tuya_ring_buffer.h"
#define DEMO_HAS_RING_BUFFER 1
#else
#define DEMO_HAS_RING_BUFFER 0
#endif

#ifdef P2P_DEMO_LOCAL_STORE
#include "local_store.h"
#endif

#define DEMO_VIDEO_CODEC TY_AV_CODEC_VIDEO_H264

/* Live recording: enabled only when both the demo switch (P2P_DEMO_LOCAL_STORE)
 * and the recording switch are on */
#if defined(P2P_DEMO_LOCAL_STORE) && defined(P2P_DEMO_LIVE_RECORD) && (P2P_DEMO_LIVE_RECORD == 1)
#define DEMO_LIVE_RECORD 1
#ifndef P2P_DEMO_RECORD_MAX_SEC
#define P2P_DEMO_RECORD_MAX_SEC 120
#endif
#else
#define DEMO_LIVE_RECORD 0
#endif

#if !defined(P2P_DEMO_WIDTH) || !defined(P2P_DEMO_HEIGHT) || !defined(P2P_DEMO_FPS) || !defined(P2P_DEMO_KBPS)
#error "CMake must inject P2P_DEMO_{WIDTH,HEIGHT,FPS,KBPS} (see CMakeLists.txt / Kconfig)"
#endif

#define DEMO_CAM_WIDTH  P2P_DEMO_WIDTH
#define DEMO_CAM_HEIGHT P2P_DEMO_HEIGHT
#define DEMO_CAM_FPS    P2P_DEMO_FPS
#define DEMO_CAM_KBPS   P2P_DEMO_KBPS

#ifndef P2P_DEMO_GOP
#define P2P_DEMO_GOP 30
#endif
#define DEMO_CAM_GOP P2P_DEMO_GOP

/* fps reported to the cloud: same as the frame feed rate, so the App's timing
 * estimate matches the actual stream (P2P_DEMO_FPS) */
#define DEMO_AV_FPS DEMO_CAM_FPS

/* Sample path: falls back to "demo_video.264" when not injected (copied from
 * assets/ into the build directory by CMake at configure time); see
 * __demo_h264_path() for the actual resolution order */
#ifndef P2P_DEMO_H264_FILE
#define P2P_DEMO_H264_FILE "demo_video.264"
#endif

/* Must match the media_frame buffer of p2p_init(); do not read media_frame->size */
#define DEMO_P2P_FRAME_CAP    (300 * 1024)
#define DEMO_FRAME_BUF_SIZE   (256 * 1024)
#define DEMO_P2P_QUEUE_DEPTH  2

typedef struct {
    uint32_t len;
    BOOL_T   is_key;
    uint64_t ts_ms;
} DEMO_P2P_Q_SLOT_T;

static MUTEX_HANDLE      s_frame_mutex = NULL;
static uint8_t          *s_q_pool[DEMO_P2P_QUEUE_DEPTH] = {0};
static DEMO_P2P_Q_SLOT_T s_q_slot[DEMO_P2P_QUEUE_DEPTH];
static uint32_t          s_q_head = 0;
static uint32_t          s_q_tail = 0;
static uint32_t          s_q_count = 0;
static uint32_t          s_frame_slot_cap = 0;
static BOOL_T            s_media_ready = FALSE;
static BOOL_T            s_live_push_enable = FALSE;
static BOOL_T            s_live_want_cam = FALSE;
static MUTEX_HANDLE      s_cam_lock = NULL;
static BOOL_T            s_queue_need_iframe = FALSE;
static BOOL_T            s_cam_running = FALSE;

#if DEMO_HAS_RING_BUFFER
static RING_BUFFER_USER_HANDLE_T s_ring_w = NULL;
static RING_BUFFER_USER_HANDLE_T s_ring_r = NULL;
static BOOL_T                    s_ring_ready = FALSE;
#endif

/**
 * @brief Clear the frame queue and force the next enqueue to wait for an I frame
 *        (the decoder needs a key frame after dropped frames)
 */
static void __demo_p2p_queue_clear(void)
{
    s_q_head = 0;
    s_q_tail = 0;
    s_q_count = 0;
    s_queue_need_iframe = TRUE;
}

/**
 * @brief Enqueue a frame (2-slot ring queue; when full, drop the oldest and wait for an I frame again)
 */
static OPERATE_RET __demo_p2p_queue_push(const uint8_t *data, uint32_t len, BOOL_T is_key, uint64_t ts_ms)
{
    uint8_t *dst;

    if (data == NULL || len == 0 || len > s_frame_slot_cap) {
        return OPRT_INVALID_PARM;
    }
    if (s_q_count >= DEMO_P2P_QUEUE_DEPTH) {
        s_q_head = (s_q_head + 1U) % DEMO_P2P_QUEUE_DEPTH;
        s_q_count--;
        s_queue_need_iframe = TRUE;
    }
    if (s_queue_need_iframe && !is_key) {
        return OPRT_OK;
    }
    dst = s_q_pool[s_q_tail];
    if (dst == NULL) {
        return OPRT_COM_ERROR;
    }
    memcpy(dst, data, len);
    s_q_slot[s_q_tail].len = len;
    s_q_slot[s_q_tail].is_key = is_key;
    s_q_slot[s_q_tail].ts_ms = ts_ms;
    s_q_tail = (s_q_tail + 1U) % DEMO_P2P_QUEUE_DEPTH;
    s_q_count++;
    if (is_key) {
        s_queue_need_iframe = FALSE;
    }
    return OPRT_OK;
}

/**
 * @brief Dequeue a frame and copy it into the P2P MEDIA_FRAME (caller holds s_frame_mutex)
 */
static OPERATE_RET __demo_p2p_queue_pop(MEDIA_FRAME *media_frame)
{
    const DEMO_P2P_Q_SLOT_T *slot;
    const uint8_t           *src;

    if (media_frame == NULL || media_frame->data == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (s_q_count == 0) {
        return OPRT_NOT_FOUND;
    }
    slot = &s_q_slot[s_q_head];
    src = s_q_pool[s_q_head];
    if (src == NULL || slot->len == 0 || slot->len > DEMO_P2P_FRAME_CAP) {
        __demo_p2p_queue_clear();
        return OPRT_COM_ERROR;
    }
    memcpy(media_frame->data, src, slot->len);
    media_frame->size = slot->len;
    media_frame->type = slot->is_key ? eVideoIFrame : eVideoPBFrame;
    media_frame->pts = slot->ts_ms;
    media_frame->timestamp = (uint32_t)slot->ts_ms;
    s_q_head = (s_q_head + 1U) % DEMO_P2P_QUEUE_DEPTH;
    s_q_count--;
    return OPRT_OK;
}

#if DEMO_LIVE_RECORD
/**
 * @brief Start live recording (local_store, segment duration P2P_DEMO_RECORD_MAX_SEC)
 */
static void __demo_rec_start(void)
{
    OPERATE_RET rt = local_store_rec_start("live", P2P_DEMO_RECORD_MAX_SEC);

    if (rt != OPRT_OK) {
        PR_ERR("live rec start failed: %d", rt);
    }
}

/**
 * @brief Stop live recording
 */
static void __demo_rec_stop(void)
{
    (void)local_store_rec_stop();
}
#endif /* DEMO_LIVE_RECORD */

/**
 * @brief Check whether an AU contains an Annex-B start code (00 00 01 / 00 00 00 01)
 */
static BOOL_T __demo_au_has_annexb(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (data == NULL || len < 4) {
        return FALSE;
    }
    for (i = 0; i + 3 < len; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            return TRUE;
        }
        if (i + 4 < len && data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * @brief vcam frame callback (runs on the vcam thread): validation -> frame queue + ring buffer + recording
 *
 * The timestamp uses tuya_p2p_misc_get_current_time_ms() at frame arrival, the
 * same clock base as the P2P stack.
 */
static void __demo_encoded_frame_cb(const DEMO_VCAM_FRAME_T *frame)
{
    BOOL_T   is_key;
    uint64_t ts_ms;

    if (frame == NULL || frame->data == NULL || frame->len == 0) {
        return;
    }
    if (s_frame_mutex == NULL || s_frame_slot_cap == 0) {
        return;
    }
    if (!s_live_push_enable) {
        return;
    }
    if (frame->len > s_frame_slot_cap) {
        PR_WARN("encoded frame too large: %u > %u", (uint32_t)frame->len, (uint32_t)s_frame_slot_cap);
        return;
    }
    if (!__demo_au_has_annexb(frame->data, frame->len)) {
        static uint32_t s_bad_au_cnt = 0;
        if ((s_bad_au_cnt++ % 30) == 0) {
            PR_NOTICE("drop AU without Annex-B start code len=%u cnt=%u", (uint32_t)frame->len, s_bad_au_cnt);
        }
        return;
    }

    is_key = frame->is_i_frame ? TRUE : FALSE;
    ts_ms = tuya_p2p_misc_get_current_time_ms();

    if (s_live_push_enable) {
        core_mutex_lock(s_frame_mutex);
        (void)__demo_p2p_queue_push(frame->data, frame->len, is_key, ts_ms);
        core_mutex_unlock(s_frame_mutex);
    }

#if DEMO_HAS_RING_BUFFER
    if (s_live_push_enable && s_ring_ready && s_ring_w != NULL) {
        (void)tuya_ipc_ring_buffer_append_data_with_timestamp(s_ring_w, (uint8_t *)frame->data, frame->len,
                                                             is_key ? E_VIDEO_I_FRAME : E_VIDEO_PB_FRAME,
                                                             ts_ms * 1000ULL, ts_ms);
    }
#endif

#if DEMO_LIVE_RECORD
    (void)local_store_rec_write((const uint8_t *)frame->data, frame->len, ts_ms, is_key);
#endif
}

/**
 * @brief Resolve the sample path: first relative to cwd, then relative to the executable directory
 *
 * P2P_DEMO_H264_FILE is injected by CMake (Kconfig can override it); it falls
 * back to "demo_video.264" when not injected. If the sample is not found under
 * cwd, the executable directory is tried as well.
 *
 * @return readable sample path (static buffer, no need to free); NULL when neither location works
 */
static const char *__demo_h264_path(void)
{
    static char resolved[1024];
    const char *path  = P2P_DEMO_H264_FILE;
    char        link[512];
    ssize_t     n     = 0;
    char       *slash = NULL;

    if (path[0] == '\0') {
        return NULL;
    }
    if (access(path, R_OK) == 0) {
        return path; /* relative path under cwd or absolute path, directly readable */
    }
    if (path[0] == '/') {
        return NULL; /* absolute path not readable: do not append the exe directory */
    }
    n = readlink("/proc/self/exe", link, sizeof(link) - 1);
    if (n <= 0) {
        return NULL;
    }
    link[n] = '\0';
    slash   = strrchr(link, '/');
    if (slash == NULL) {
        return NULL;
    }
    slash[1] = '\0';
    snprintf(resolved, sizeof(resolved), "%s%s", link, path);
    if (access(resolved, R_OK) != 0) {
        return NULL;
    }
    return resolved;
}

/**
 * @brief Exposed sample path resolution (shared by the PB_SEED of demo_media_event and live streaming)
 *
 * @return readable sample path (static buffer); NULL when not found
 */
const char *demo_h264_source_path(void)
{
    return __demo_h264_path();
}

/**
 * @brief Whether the video source is ready: a readable sample file is treated as present
 */
static BOOL_T __demo_camera_present(void)
{
    const char *path = __demo_h264_path();
    FILE       *fp   = NULL;

    if (path == NULL) {
        PR_ERR("sample not found: run from build/ or set P2P_DEMO_H264_FILE");
        return FALSE;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        PR_ERR("sample '%s' not readable", path);
        return FALSE;
    }
    fclose(fp);
    return TRUE;
}

/**
 * @brief Open the virtual camera (demo_vcam_open); log an error and return on failure
 */
static OPERATE_RET __demo_camera_open(void)
{
    const char *path = NULL;
    OPERATE_RET rt   = OPRT_OK;

    if (s_cam_running) {
        return OPRT_OK;
    }

    path = __demo_h264_path();
    if (path == NULL) {
        PR_ERR("sample not found: run from build/ or set P2P_DEMO_H264_FILE");
        return OPRT_NOT_EXIST;
    }

    rt = demo_vcam_open(path, DEMO_CAM_FPS, __demo_encoded_frame_cb);
    if (rt != OPRT_OK) {
        PR_ERR("demo_vcam_open('%s') failed: %d", path, rt);
        return rt;
    }

    s_cam_running = TRUE;
    PR_NOTICE("live vcam started: %ux%u@%u %s (source=%s)", (uint32_t)DEMO_CAM_WIDTH, (uint32_t)DEMO_CAM_HEIGHT,
              (uint32_t)DEMO_CAM_FPS, DEMO_VIDEO_CODEC == TY_AV_CODEC_VIDEO_H265 ? "H265" : "H264", path);
    return OPRT_OK;
}

/**
 * @brief Close the virtual camera (constraint: must not be called from a vcam frame callback, see demo_vcam.h)
 */
static void __demo_camera_close(void)
{
    OPERATE_RET rt;

    if (!s_cam_running) {
        return;
    }
    rt = demo_vcam_close();
    if (rt != OPRT_OK) {
        /* close timed out: vcam keeps the buffer and thread (leak rather than use-after-free); only log here */
        PR_ERR("vcam close failed: %d", rt);
    }
    s_cam_running = FALSE;
    PR_NOTICE("live vcam stopped");
}

/**
 * @brief App I-frame request: forwarded to vcam (the next tick jumps to the next IDR AU)
 */
static OPERATE_RET __demo_camera_request_i_frame(void)
{
    return demo_vcam_request_i_frame();
}

/**
 * @brief Adjust the bitrate: the sample source has no encoder, so the bitrate cannot be changed
 */
static OPERATE_RET __demo_camera_set_bitrate(uint32_t kbps)
{
    (void)kbps;
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief Open/close vcam according to s_live_want_cam (holds s_cam_lock)
 */
static void __demo_camera_sync(void)
{
    BOOL_T wanted;

    if (s_cam_lock == NULL) {
        return; /* before tuya_ipc_demo_start(): nobody holds the lock yet */
    }

    core_mutex_lock(s_cam_lock);
    wanted = s_live_want_cam;

    if (wanted && !s_cam_running) {
        if (__demo_camera_open() != OPRT_OK) {
            PR_ERR("vcam open failed, nothing will be streamed");
        }
    } else if (!wanted && s_cam_running) {
        __demo_camera_close();
    }
    core_mutex_unlock(s_cam_lock);
}

/**
 * @brief Build and report the AV capabilities (video only)
 */
static void __demo_init_p2p_av_info(BOOL_T from_camera)
{
    TRANS_IPC_AV_INFO_T av_info;
    OPERATE_RET         rt;
    uint32_t            w, h, fps, gop, kbps;
    int                 i;

    if (from_camera) {
        w = DEMO_CAM_WIDTH;
        h = DEMO_CAM_HEIGHT;
        fps = DEMO_AV_FPS;
        gop = DEMO_CAM_GOP;
        kbps = DEMO_CAM_KBPS;
    } else {
        return;
    }

    memset(&av_info, 0, sizeof(av_info));
    for (i = 0; i < 2; i++) {
        int s = (i == 0) ? eIpcStreamVideoMain : eIpcStreamVideoSub;

        av_info.video_codec[s] = DEMO_VIDEO_CODEC;
        av_info.fps[s] = fps;
        av_info.gop[s] = gop;
        av_info.bitrate[s] = kbps;
        av_info.width[s] = w;
        av_info.height[s] = h;
    }

    rt = tuya_ipc_init_trans_av_info(&av_info);
    if (rt != OPRT_OK) {
        /* Reached when there is no p2p session (e.g. the media chain selftest); this is expected and does not block media start */
        PR_ERR("tuya_ipc_init_trans_av_info failed: %d", rt);
    }
}

/**
 * @brief Initialize the demo media chain: frame pool / AV info / ring buffer / local_store
 */
void tuya_ipc_demo_start(void)
{
    OPERATE_RET rt;
    BOOL_T      have_camera;
    uint32_t    i;
    const char *src;

    if (s_media_ready) {
        return;
    }

    rt = core_mutex_create_init(&s_frame_mutex);
    if (rt != OPRT_OK) {
        PR_ERR("frame mutex create failed: %d", rt);
        return;
    }
    rt = core_mutex_create_init(&s_cam_lock);
    if (rt != OPRT_OK) {
        PR_ERR("camera mutex create failed: %d", rt);
        return;
    }

    s_live_push_enable = FALSE;
    s_live_want_cam = FALSE;
    __demo_p2p_queue_clear();

    for (i = 0; i < DEMO_P2P_QUEUE_DEPTH; i++) {
        s_q_pool[i] = (uint8_t *)core_malloc(DEMO_FRAME_BUF_SIZE);
        if (s_q_pool[i] == NULL) {
            PR_ERR("alloc p2p queue slot %u failed", i);
            return;
        }
    }
    s_frame_slot_cap = DEMO_FRAME_BUF_SIZE;

    have_camera = __demo_camera_present();
    if (!have_camera) {
        /* The specific reason (not found / unreadable) was already logged by __demo_camera_present */
        PR_ERR("no video source, live start will fail");
    }

    __demo_init_p2p_av_info(have_camera);

#if DEMO_HAS_RING_BUFFER
    {
        RING_BUFFER_INIT_PARAM_T rp = {0};

        rp.bitrate = DEMO_CAM_KBPS;
        rp.fps = DEMO_AV_FPS;
        rp.max_buffer_seconds = 2;
        if (tuya_ipc_ring_buffer_init(0, 0, E_IPC_STREAM_VIDEO_MAIN, &rp) == OPRT_OK) {
            s_ring_w = tuya_ipc_ring_buffer_open(0, 0, E_IPC_STREAM_VIDEO_MAIN, E_RBUF_WRITE);
            s_ring_r = tuya_ipc_ring_buffer_open(0, 0, E_IPC_STREAM_VIDEO_MAIN, E_RBUF_READ);
            s_ring_ready = (s_ring_w != NULL && s_ring_r != NULL) ? TRUE : FALSE;
            PR_NOTICE("ring_buffer live video %s", s_ring_ready ? "ready" : "open fail");
        }
    }
#endif

#ifdef P2P_DEMO_LOCAL_STORE
    /* Linux has no mount semantics: local_store writes directly to ./media under the process cwd, no port_fs_mount */
    if (local_store_init() != OPRT_OK) {
        PR_ERR("local_store init failed");
    }
#endif

    s_media_ready = TRUE;
    demo_media_event_register();

    src = __demo_h264_path();
    PR_NOTICE("tuya_ipc_demo: ready (source=%s, sensor %ux%u@%u, av fps %u, live_rec=%d)",
              (src != NULL) ? src : "none", (uint32_t)DEMO_CAM_WIDTH, (uint32_t)DEMO_CAM_HEIGHT,
              (uint32_t)DEMO_CAM_FPS, (uint32_t)DEMO_AV_FPS, (int)DEMO_LIVE_RECORD);
}

/**
 * @brief Release the demo media chain resources
 */
void tuya_ipc_demo_end(void)
{
    uint32_t i;

    s_live_push_enable = FALSE;
    s_live_want_cam = FALSE;
    if (s_cam_lock != NULL) {
        core_mutex_lock(s_cam_lock);
        __demo_camera_close();
        core_mutex_unlock(s_cam_lock);
    } else {
        __demo_camera_close();
    }
    s_media_ready = FALSE;

    if (s_frame_mutex != NULL) {
        core_mutex_lock(s_frame_mutex);
        __demo_p2p_queue_clear();
        core_mutex_unlock(s_frame_mutex);
    }
    for (i = 0; i < DEMO_P2P_QUEUE_DEPTH; i++) {
        if (s_q_pool[i] != NULL) {
            core_free(s_q_pool[i]);
            s_q_pool[i] = NULL;
        }
    }
    s_frame_slot_cap = 0;
    if (s_frame_mutex != NULL) {
        core_mutex_release(s_frame_mutex);
        s_frame_mutex = NULL;
    }
    if (s_cam_lock != NULL) {
        core_mutex_release(s_cam_lock);
        s_cam_lock = NULL;
    }
}

/**
 * @brief App starts watching the live stream: open vcam, start recording, clear the queue, start pushing
 */
int demo_on_live_video_start_callback(void)
{
    if (!s_media_ready) {
        return -1;
    }

    s_live_want_cam = TRUE;
    __demo_camera_sync();
    if (!s_cam_running) {
        PR_ERR("LIVE start: vcam open failed");
        s_live_want_cam = FALSE;
        return -1;
    }

#if DEMO_LIVE_RECORD
    if (!local_store_rec_is_open()) {
        __demo_rec_start();
    }
#endif

    if (s_frame_mutex != NULL) {
        core_mutex_lock(s_frame_mutex);
        __demo_p2p_queue_clear();
        core_mutex_unlock(s_frame_mutex);
    }

    s_live_push_enable = TRUE;
    PR_NOTICE("LIVE video start: push enabled");
    return 0;
}

/**
 * @brief App stops watching the live stream: stop pushing, clear the queue, close vcam, stop recording
 */
int demo_on_live_video_stop_callback(void)
{
    s_live_want_cam = FALSE;
    s_live_push_enable = FALSE;
    if (s_frame_mutex != NULL) {
        core_mutex_lock(s_frame_mutex);
        __demo_p2p_queue_clear();
        core_mutex_unlock(s_frame_mutex);
    }
    __demo_camera_sync();
#if DEMO_LIVE_RECORD
    __demo_rec_stop();
#endif
    PR_NOTICE("LIVE video stop: vcam %s", s_cam_running ? "kept for cloud recording" : "closed");
    return 0;
}

/**
 * @brief P2P link down: stop playback + run live stop
 */
int demo_on_signal_disconnect_callback(void)
{
    demo_media_pb_stop();
    (void)demo_on_live_video_stop_callback();
    return 0;
}

/**
 * @brief P2P video frame fetch: ring buffer first, then the 2-slot frame queue; return -1 when no frame is available
 *
 * Note: must return -1 when there is no frame -- the P2P stack treats a return
 * of 0 as "a frame is available" and keeps sending, so an unfilled MEDIA_FRAME
 * would take the error path.
 */
int demo_on_get_video_frame_callback(MEDIA_FRAME *media_frame)
{
    OPERATE_RET rt;

    if (media_frame == NULL || media_frame->data == NULL || !s_media_ready) {
        core_system_sleep(10);
        return -1;
    }

#if DEMO_HAS_RING_BUFFER
    if (s_ring_ready && s_ring_r != NULL) {
        RING_BUFFER_NODE_T *node = tuya_ipc_ring_buffer_get_frame(s_ring_r, FALSE);

        if (node != NULL && node->raw_data != NULL && node->size > 0 && node->size <= DEMO_P2P_FRAME_CAP) {
            memcpy(media_frame->data, node->raw_data, node->size);
            media_frame->size = node->size;
            media_frame->type = (node->type == E_VIDEO_I_FRAME) ? eVideoIFrame : eVideoPBFrame;
            media_frame->pts = node->timestamp;
            media_frame->timestamp = (uint32_t)node->timestamp;
            return 0;
        }
        core_system_sleep(10);
        return -1;
    }
#endif

    core_mutex_lock(s_frame_mutex);
    rt = __demo_p2p_queue_pop(media_frame);
    core_mutex_unlock(s_frame_mutex);
    if (rt != OPRT_OK) {
        core_system_sleep(10);
        return -1;
    }
    return 0;
}

/**
 * @brief App I-frame request: forwards to demo_vcam_request_i_frame (returns -1 if not accepted)
 */
int demo_on_request_i_frame_callback(void)
{
    return (__demo_camera_request_i_frame() == OPRT_OK) ? 0 : -1;
}

/**
 * @brief App bitrate adjustment: the sample source cannot change the bitrate, so return -1
 */
int demo_on_set_video_bitrate_callback(uint32_t kbps)
{
    return (__demo_camera_set_bitrate(kbps) == OPRT_OK) ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Audio: this demo does not capture audio (empty implementations).
 * The audio frame fetch must return -1 (returning 0 would make the P2P stack
 * send an unfilled MEDIA_FRAME as if a frame were available).
 * --------------------------------------------------------------------------- */

int demo_on_get_audio_frame_callback(MEDIA_FRAME *media_frame)
{
    (void)media_frame;
    return -1;
}

int demo_on_live_audio_start_callback(void)
{
    return 0;
}

int demo_on_live_audio_stop_callback(void)
{
    return 0;
}

int demo_on_recv_audio_frame_callback(MEDIA_FRAME *media_frame)
{
    (void)media_frame;
    return 0;
}
