/**
 * @file demo_vcam.c
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

#include <stdio.h>
#include <string.h>

#include "demo_vcam.h"
#include "core_log.h"
#include "core_memory.h"
#include "core_system.h"
#include "core_thread.h"

/* Sample size safety limit: the demo keeps the whole file resident but rejects clearly abnormal files */
#define DEMO_VCAM_MAX_FILE_SIZE (64 * 1024 * 1024)

/* Frame feed thread: stack size and priority (4096 / THREAD_PRIO_3) */
#define DEMO_VCAM_THREAD_STACK 4096

/* Tick wait slice: the thread wakes at <=10ms granularity to check the stop/I-frame flags, so close can return promptly */
#define DEMO_VCAM_WAIT_SLICE_MS 10

/* Close waits up to this limit for the thread to exit (core_thread_delete only sets the stop state; the actual exit happens inside the vcam thread) */
#define DEMO_VCAM_EXIT_WAIT_MS 2000

/**
 * @brief AU index entry
 */
typedef struct {
    uint32_t offset; /* offset of the AU first byte in the sample buffer (including the start code) */
    uint32_t len;    /* AU byte count */
    BOOL_T   is_i;   /* whether the AU contains an IDR (type 5) */
} DEMO_VCAM_AU_T;

/**
 * @brief vcam global state (single instance; the demo opens only one virtual camera at a time)
 */
static struct {
    uint8_t         *buf;     /* whole-file sample buffer (core_malloc) */
    uint32_t         buf_len; /* sample byte count */
    DEMO_VCAM_AU_T  *aus;     /* AU index array */
    uint32_t         au_num;  /* total AU count */
    uint32_t         i_num;   /* I-frame (IDR AU) count */
    uint32_t         fps;
    DEMO_VCAM_FRAME_CB cb;
    THREAD_HANDLE    thread;
    volatile BOOL_T  running;      /* TRUE between a successful open and close */
    volatile BOOL_T  stop;         /* set by close; the thread exits on the next tick */
    volatile BOOL_T  started;      /* the thread function has been entered (its state is necessarily RUNNING) */
    volatile BOOL_T  exited;       /* the thread function has returned: buf is no longer touched, close may free it safely */
    volatile BOOL_T  want_i_frame; /* set by request_i_frame; the thread jumps to an IDR on the next tick */
    volatile BOOL_T  poisoned;     /* close timed out (the thread could not be stopped): unrecoverable state, reopen is refused */
} s_vcam;

/**
 * @brief Find the next Annex-B start code (00 00 01 / 00 00 00 01) in [from, len)
 *
 * @param[in]  buf    buffer
 * @param[in]  len    buffer length
 * @param[in]  from   offset to start searching from
 * @param[out] off    offset of the first start-code byte
 * @param[out] sc_len start-code length (3 or 4)
 * @return TRUE when found
 */
static BOOL_T __demo_vcam_find_sc(const uint8_t *buf, uint32_t len, uint32_t from, uint32_t *off, uint32_t *sc_len)
{
    uint32_t i = 0;

    for (i = from; i + 3 <= len; i++) {
        if (0 != buf[i] || 0 != buf[i + 1]) {
            continue;
        }
        if (1 == buf[i + 2]) { /* 00 00 01 */
            *off    = i;
            *sc_len = 3;
            return TRUE;
        }
        /* A 4-byte start code must be recognized before the 3-byte check, otherwise the leading 0 is dropped */
        if (0 == buf[i + 2] && i + 4 <= len && 1 == buf[i + 3]) { /* 00 00 00 01 */
            *off    = i;
            *sc_len = 4;
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * @brief Record one AU (when the array is full the AU is only counted; capacity is the caller's responsibility)
 */
static void __demo_vcam_emit(DEMO_VCAM_AU_T *aus, uint32_t cap, uint32_t idx, uint32_t off, uint32_t len, BOOL_T is_i)
{
    if (NULL == aus || idx >= cap) {
        return;
    }
    aus[idx].offset = off;
    aus[idx].len    = len;
    aus[idx].is_i   = is_i;
}

/**
 * @brief Scan the sample and build the AU index
 *
 * @param[in]  buf sample buffer
 * @param[in]  len buffer length
 * @param[out] aus AU array; when NULL only count the AUs (first pass to size the array)
 * @param[in]  cap array capacity
 * @return total AU count (when aus is non-NULL only the first cap entries are filled)
 */
static uint32_t __demo_vcam_index(const uint8_t *buf, uint32_t len, DEMO_VCAM_AU_T *aus, uint32_t cap)
{
    uint32_t count      = 0;
    uint32_t cur_off    = 0;
    uint32_t cur_end    = 0;
    BOOL_T   cur_open   = FALSE;
    BOOL_T   cur_has_vcl = FALSE;
    BOOL_T   cur_is_i   = FALSE;
    uint32_t sc_off     = 0;
    uint32_t sc_len     = 0;
    BOOL_T   has        = __demo_vcam_find_sc(buf, len, 0, &sc_off, &sc_len);

    while (has) {
        uint32_t nal_off = sc_off;
        uint32_t nal_end = len;
        uint32_t next_off = 0;
        uint32_t next_len = 0;
        BOOL_T   has_next = FALSE;
        uint8_t  nal_type = 0;
        BOOL_T   is_vcl   = FALSE;
        BOOL_T   new_au   = FALSE;

        /* Stop scanning when the NAL payload first byte is missing (start code at end of file) */
        if (nal_off + sc_len >= len) {
            break;
        }
        nal_type = buf[nal_off + sc_len] & 0x1F;
        is_vcl   = (nal_type >= 1 && nal_type <= 5);

        has_next = __demo_vcam_find_sc(buf, len, nal_off + sc_len, &next_off, &next_len);
        if (has_next) {
            nal_end = next_off;
        }

        if (cur_open) {
            if (9 == nal_type) { /* AUD: start of a new AU */
                new_au = TRUE;
            } else if (is_vcl && cur_has_vcl) { /* second VCL: the previous AU ends */
                new_au = TRUE;
            }
        }
        if (new_au) {
            __demo_vcam_emit(aus, cap, count, cur_off, cur_end - cur_off, cur_is_i);
            count++;
            cur_open = FALSE;
        }
        if (!cur_open) {
            cur_off     = nal_off;
            cur_has_vcl = FALSE;
            cur_is_i    = FALSE;
            cur_open    = TRUE;
        }
        cur_end = nal_end;
        if (is_vcl) {
            cur_has_vcl = TRUE;
            if (5 == nal_type) { /* IDR slice */
                cur_is_i = TRUE;
            }
        }

        if (has_next) {
            sc_off = next_off;
            sc_len = next_len;
        }
        has = has_next;
    }

    /* Finish: emit the last AU */
    if (cur_open && cur_end > cur_off) {
        __demo_vcam_emit(aus, cap, count, cur_off, cur_end - cur_off, cur_is_i);
        count++;
    }

    return count;
}

/**
 * @brief Find the next I-frame AU after cur (wrap to 0 at the end; stay at cur when none exists)
 */
static uint32_t __demo_vcam_next_i(uint32_t cur)
{
    uint32_t i = 0;

    for (i = cur + 1; i < s_vcam.au_num; i++) {
        if (s_vcam.aus[i].is_i) {
            return i;
        }
    }
    for (i = 0; i < cur; i++) {
        if (s_vcam.aus[i].is_i) {
            return i;
        }
    }

    return cur;
}

/**
 * @brief Frame feed thread: delivers AUs on a 1000/fps tick, loops at EOF, and
 *        responds to stop/I-frame requests
 */
static void __demo_vcam_thread(void *args)
{
    uint32_t period_ms = 0;
    uint32_t idx       = 0;
    uint64_t tick      = 0;
    uint64_t start_ms  = 0;

    (void)args;

    /* Set started first: close waits for it before core_thread_delete (delete is ineffective in the EMPTY state) */
    s_vcam.started = TRUE;

    period_ms = 1000 / s_vcam.fps;
    if (0 == period_ms) { /* at least one 1ms tick when fps > 1000 */
        period_ms = 1;
    }
    start_ms = core_system_get_millisecond();

    while (!s_vcam.stop) {
        uint64_t due_ms = 0;
        DEMO_VCAM_FRAME_T frame;

        /* App I-frame request: the next tick jumps directly to the next IDR AU */
        if (s_vcam.want_i_frame) {
            s_vcam.want_i_frame = FALSE;
            idx                 = __demo_vcam_next_i(idx);
            PR_DEBUG("vcam request i-frame -> AU %u/%u", idx, s_vcam.au_num);
        }

        /* Drift correction: the target time of tick n is start + n*period, not a sleep(period) per tick */
        due_ms = start_ms + tick * period_ms;
        while (!s_vcam.stop) {
            uint64_t now_ms = core_system_get_millisecond();
            uint32_t slice  = 0;

            if (now_ms >= due_ms) {
                break;
            }
            slice = (due_ms - now_ms) > DEMO_VCAM_WAIT_SLICE_MS ? DEMO_VCAM_WAIT_SLICE_MS : (uint32_t)(due_ms - now_ms);
            core_system_sleep(slice ? slice : 1);
        }
        if (s_vcam.stop) {
            break;
        }

        frame.data       = s_vcam.buf + s_vcam.aus[idx].offset;
        frame.len        = s_vcam.aus[idx].len;
        frame.ts_ms      = tick * 1000 / s_vcam.fps; /* monotonic timestamp matching the tick, strictly increasing */
        frame.is_i_frame = s_vcam.aus[idx].is_i;
        if (s_vcam.cb) {
            s_vcam.cb(&frame);
        }

        idx = (idx + 1) % s_vcam.au_num; /* loop back to frame 0 at EOF */
        tick++;
    }

    /* buf/aus are no longer touched after this point: close relies on it to free safely */
    s_vcam.exited = TRUE;
}

OPERATE_RET demo_vcam_open(const char *path, uint32_t fps, DEMO_VCAM_FRAME_CB cb)
{
    FILE            *fp     = NULL;
    long             fsize  = 0;
    size_t           got    = 0;
    uint8_t         *buf    = NULL;
    DEMO_VCAM_AU_T  *aus    = NULL;
    uint32_t         au_num = 0;
    uint32_t         i_num  = 0;
    uint32_t         i      = 0;
    THREAD_CFG_T     cfg    = {0};
    OPERATE_RET      rt     = OPRT_OK;

    if (NULL == path || '\0' == path[0] || 0 == fps || NULL == cb) {
        PR_ERR("vcam open: invalid parm (path/fps/cb)");
        return OPRT_INVALID_PARM;
    }
    if (s_vcam.running) {
        PR_ERR("vcam open: already open");
        return OPRT_COM_ERROR;
    }
    if (s_vcam.poisoned) {
        /* The previous close timed out: the old thread may still be alive and
         * reading the global buf/aus; opening again would overwrite the global
         * state (two threads feeding frames). This state is unrecoverable and
         * indicates the process should be restarted */
        PR_ERR("vcam open: unusable after a timed-out close, restart the process");
        return OPRT_COM_ERROR;
    }

    /* 1) Read the whole file (the bundled video is about 4.3MB; full residency is allowed) */
    fp = fopen(path, "rb");
    if (NULL == fp) {
        PR_ERR("vcam open: cannot open sample '%s'", path);
        return OPRT_NOT_EXIST;
    }
    if (0 != fseek(fp, 0, SEEK_END) || (fsize = ftell(fp)) <= 0 || fsize > DEMO_VCAM_MAX_FILE_SIZE) {
        /* The failure reason includes the size limit, so switching to a larger file needs no guesswork */
        PR_ERR("vcam open: file '%s' size invalid (%ld), limit %u", path, fsize,
               (uint32_t)DEMO_VCAM_MAX_FILE_SIZE);
        fclose(fp);
        return OPRT_INVALID_PARM;
    }
    rewind(fp);
    buf = (uint8_t *)core_malloc((size_t)fsize);
    if (NULL == buf) {
        PR_ERR("vcam open: malloc %ld failed", fsize);
        fclose(fp);
        return OPRT_MALLOC_FAILED;
    }
    got = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);
    fp = NULL;
    if (got != (size_t)fsize) {
        PR_ERR("vcam open: read '%s' short (%u/%ld)", path, (uint32_t)got, fsize);
        core_free(buf);
        return OPRT_COM_ERROR;
    }

    /* 2) Scan AUs: first pass counts, second pass fills the index */
    au_num = __demo_vcam_index(buf, (uint32_t)fsize, NULL, 0);
    if (0 == au_num) {
        PR_ERR("vcam open: no Annex-B AU in '%s'", path);
        core_free(buf);
        return OPRT_INVALID_PARM;
    }
    aus = (DEMO_VCAM_AU_T *)core_malloc(au_num * sizeof(DEMO_VCAM_AU_T));
    if (NULL == aus) {
        PR_ERR("vcam open: malloc au index failed (%u)", au_num);
        core_free(buf);
        return OPRT_MALLOC_FAILED;
    }
    au_num = __demo_vcam_index(buf, (uint32_t)fsize, aus, au_num);
    for (i = 0; i < au_num; i++) {
        if (aus[i].is_i) {
            i_num++;
        }
    }
    if (0 == i_num) {
        PR_ERR("vcam open: no IDR AU in '%s'", path);
        core_free(aus);
        core_free(buf);
        return OPRT_INVALID_PARM;
    }

    /* 3) Mark the state ready and start the frame feed thread */
    s_vcam.buf          = buf;
    s_vcam.buf_len      = (uint32_t)fsize;
    s_vcam.aus          = aus;
    s_vcam.au_num       = au_num;
    s_vcam.i_num        = i_num;
    s_vcam.fps          = fps;
    s_vcam.cb           = cb;
    s_vcam.stop         = FALSE;
    s_vcam.started      = FALSE;
    s_vcam.exited       = FALSE;
    s_vcam.want_i_frame = FALSE;
    s_vcam.thread       = NULL;

    PR_NOTICE("vcam open: %s, %u bytes, %u AU, %u I-frame, %ufps", path, s_vcam.buf_len, au_num, i_num, fps);

    cfg.stackDepth = DEMO_VCAM_THREAD_STACK;
    cfg.priority   = THREAD_PRIO_3;
    cfg.thrdname   = "demo_vcam";
    rt             = core_thread_create_and_start(&s_vcam.thread, NULL, NULL, __demo_vcam_thread, NULL, &cfg);
    if (OPRT_OK != rt) {
        PR_ERR("vcam open: create thread failed (%d)", rt);
        core_free(aus);
        core_free(buf);
        s_vcam.buf    = NULL;
        s_vcam.aus    = NULL;
        s_vcam.thread = NULL;
        return rt;
    }

    s_vcam.running = TRUE;

    return OPRT_OK;
}

OPERATE_RET demo_vcam_close(void)
{
    uint32_t waited_ms = 0;

    if (!s_vcam.running) {
        return OPRT_OK; /* not open: reentrant */
    }
    s_vcam.running = FALSE;

    /* Stop the thread: core_thread_delete only sets the stop state; the actual
     * exit (including self-release of thread resources) happens inside the vcam
     * thread, so wait for the exited flag to confirm it no longer touches the
     * sample buffer */
    s_vcam.stop = TRUE;
    if (NULL != s_vcam.thread) {
        /* First wait for the thread entry to set started: core_thread_delete on
         * the EMPTY state (created but not yet RUNNING) returns OPRT_COM_ERROR
         * and does not set the stop state, so the thread wrapper would wait for
         * STOP forever (zombie thread). When the entry has set started, the
         * state is necessarily RUNNING */
        while (!s_vcam.started && waited_ms < DEMO_VCAM_EXIT_WAIT_MS) {
            core_system_sleep(5);
            waited_ms += 5;
        }
        if (!s_vcam.started) {
            /* The thread did not start (timeout): do not delete it, do not free the buffer; leak rather than use-after-free */
            s_vcam.poisoned = TRUE;
            PR_ERR("vcam close: thread not started in %ums, keep buffers", waited_ms);
            return OPRT_TIMEOUT;
        }
        core_thread_delete(s_vcam.thread);
        s_vcam.thread = NULL;
        while (!s_vcam.exited && waited_ms < DEMO_VCAM_EXIT_WAIT_MS) {
            core_system_sleep(5);
            waited_ms += 5;
        }
        if (!s_vcam.exited) {
            /* The thread still has not exited (callback blocked, etc.): likewise keep the buffer, leak rather than use-after-free */
            s_vcam.poisoned = TRUE;
            PR_ERR("vcam close: thread not exited in %ums, keep buffers", waited_ms);
            return OPRT_TIMEOUT;
        }
    }

    /* Reset the state and free the buffer */
    s_vcam.cb           = NULL;
    s_vcam.fps          = 0;
    s_vcam.au_num       = 0;
    s_vcam.i_num        = 0;
    s_vcam.buf_len      = 0;
    s_vcam.want_i_frame = FALSE;
    core_free(s_vcam.aus);
    core_free(s_vcam.buf);
    s_vcam.aus = NULL;
    s_vcam.buf = NULL;

    PR_NOTICE("vcam close");

    return OPRT_OK;
}

OPERATE_RET demo_vcam_request_i_frame(void)
{
    if (!s_vcam.running) {
        return OPRT_COM_ERROR;
    }
    s_vcam.want_i_frame = TRUE;

    return OPRT_OK;
}

BOOL_T demo_vcam_is_open(void)
{
    return s_vcam.running;
}

OPERATE_RET demo_vcam_get_stats(uint32_t *au_num, uint32_t *i_num, uint32_t *max_len, BOOL_T *first_is_i)
{
    uint32_t i;
    uint32_t maxl = 0;

    if (!s_vcam.running || s_vcam.aus == NULL) {
        return OPRT_COM_ERROR;
    }
    for (i = 0; i < s_vcam.au_num; i++) {
        if (s_vcam.aus[i].len > maxl) {
            maxl = s_vcam.aus[i].len;
        }
    }
    if (au_num != NULL) {
        *au_num = s_vcam.au_num;
    }
    if (i_num != NULL) {
        *i_num = s_vcam.i_num;
    }
    if (max_len != NULL) {
        *max_len = maxl;
    }
    if (first_is_i != NULL) {
        *first_is_i = (s_vcam.au_num > 0) ? s_vcam.aus[0].is_i : FALSE;
    }
    return OPRT_OK;
}
