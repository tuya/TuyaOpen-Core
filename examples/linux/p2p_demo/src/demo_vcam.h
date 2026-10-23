/**
 * @file demo_vcam.h
 * @brief Virtual camera implementation: Annex-B sample -> AU index -> cyclic
 *        frame feed at the configured frame rate.
 *
 * The video source is a file: the whole file is read into memory (the bundled
 * video is about 4.3MB), NAL units are located by Annex-B start codes, and an
 * AU index is built; a vcam thread delivers AUs on a 1000/fps ms tick, using
 * absolute timestamps for drift correction (no accumulated sleep error), and
 * loops back to frame 0 at end of file. demo_vcam_request_i_frame() makes the
 * next tick jump to the next IDR AU. close() waits for the thread to exit
 * before freeing the buffer; a timed-out close marks the instance poisoned and
 * refuses a reopen (leak instead of use-after-free).
 *
 * AU splitting rules (works with and without AUD; the bundled demo_video.264
 * has no AUD):
 *   - an AUD (NAL type 9) starts a new AU (the AUD belongs to the new AU)
 *   - a VCL NAL (type 1..5) starts a new AU when the current AU already has a VCL
 *   - other non-VCL NALs (SPS/PPS/SEI) belong to the current AU
 *   - a type 5 NAL (IDR) inside an AU sets is_i_frame = TRUE
 *   - one VCL per AU is assumed (multi-slice streams are out of scope)
 *
 * The frame callback runs on the vcam thread and must not block for long
 * (the demo side copies the frame into its queue); frame->data points into the
 * vcam internal buffer and is only valid during the callback.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __DEMO_VCAM_H__
#define __DEMO_VCAM_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Description of one frame (one AU)
 */
typedef struct {
    const uint8_t *data;    /* Annex-B AU (including the start code), points into the vcam internal buffer, valid during the callback */
    uint32_t       len;     /* AU byte count */
    uint64_t       ts_ms;   /* monotonic timestamp starting at 0, incremented by 1000/fps */
    BOOL_T         is_i_frame; /* TRUE when the AU contains an IDR (NAL type 5) */
} DEMO_VCAM_FRAME_T;

/**
 * @brief Frame callback type (runs on the vcam thread)
 */
typedef void (*DEMO_VCAM_FRAME_CB)(const DEMO_VCAM_FRAME_T *frame);

/**
 * @brief Open the virtual camera: read the sample, build the AU index, start the frame feed thread
 *
 * @param[in] path sample file path (Annex-B H.264; NULL or an empty string returns OPRT_INVALID_PARM)
 * @param[in] fps  frame feed rate (0 returns OPRT_INVALID_PARM)
 * @param[in] cb   frame callback (NULL returns OPRT_INVALID_PARM)
 * @return OPRT_OK on success; OPRT_NOT_EXIST when the file does not exist;
 *         OPRT_INVALID_PARM when the file has no AU or the parameters are invalid;
 *         OPRT_COM_ERROR on repeated open, or when the previous close timed out
 *         (the thread could not be stopped, state unrecoverable)
 */
OPERATE_RET demo_vcam_open(const char *path, uint32_t fps, DEMO_VCAM_FRAME_CB cb);

/**
 * @brief Close the virtual camera: stop the thread, free the sample buffer, reset the state
 *
 * Returns OPRT_OK directly when not open (reentrant). The frame feed thread is
 * guaranteed to have exited before returning, so the callback will not fire
 * again.
 *
 * Constraint: **must not be called from a frame callback** -- the callback runs
 * on the vcam thread, while close needs that thread to exit first (waiting on
 * itself would run until timeout); close from another thread or the main thread.
 *
 * @return OPRT_OK on success; OPRT_TIMEOUT when the thread does not start or
 *         exit within the limit (the buffer and thread are then kept; leak
 *         rather than use-after-free)
 */
OPERATE_RET demo_vcam_close(void);

/**
 * @brief Request an I frame: the thread jumps to the next IDR AU on the next
 *        tick (rescans from 0 when the end is reached)
 *
 * Corresponds to the App's "request I frame" semantics.
 *
 * @return OPRT_OK on success; OPRT_COM_ERROR when not open
 */
OPERATE_RET demo_vcam_request_i_frame(void);

/**
 * @brief Query whether the camera is open
 *
 * @return TRUE when open
 */
BOOL_T demo_vcam_is_open(void);

/**
 * @brief Query the current index statistics (diagnostics/selftest use; available after a successful open)
 *
 * @param[out] au_num     total AU count
 * @param[out] i_num      IDR AU count
 * @param[out] max_len    maximum AU byte count
 * @param[out] first_is_i whether the first AU is an IDR
 * @return OPRT_OK; OPRT_COM_ERROR when not open
 */
OPERATE_RET demo_vcam_get_stats(uint32_t *au_num, uint32_t *i_num, uint32_t *max_len, BOOL_T *first_is_i);

#ifdef __cplusplus
}
#endif

#endif /* __DEMO_VCAM_H__ */
