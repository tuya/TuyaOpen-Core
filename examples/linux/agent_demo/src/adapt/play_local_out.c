/**
 * @file play_local_out.c
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
#if defined(ENABLE_AI_PLAYER)

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <string.h>

#include "core_log.h"
#include "svc_ai_player.h" /* AI_PLAYER_CONSUMER_T / PLAYER_CONSUMER_HANDLE / CORE_AUDIO_* */
#include "play_local_out.h"

/* ---------------------------------------------------------------------------
 * Output format: consistent with the service_init convention
 * (16k/16bit/mono). The mirror types (CORE_AUDIO_* in ai_player_types.h) are
 * asserted value by value: if the core mirror drifts (sample rate / bit width /
 * channel values change), the build fails here instead of silently playing in
 * the wrong format. 
 * --------------------------------------------------------------------------- */
#define PLAY_LOCAL_OUT_SAMPLE_RATE 16000
#define PLAY_LOCAL_OUT_CHANNELS    1
#define PLAY_LOCAL_OUT_FRAME_BYTES 2 /* 16bit × 1ch */

typedef char _ai_player_assert_sample[(CORE_AUDIO_SAMPLE_16K == PLAY_LOCAL_OUT_SAMPLE_RATE) ? 1 : -1];
typedef char _ai_player_assert_databits[(CORE_AUDIO_DATABITS_16 == 16) ? 1 : -1];
typedef char _ai_player_assert_channel[(CORE_AUDIO_CHANNEL_MONO == PLAY_LOCAL_OUT_CHANNELS) ? 1 : -1];

/* ALSA device name: "default" by default (auto-resampled through PulseAudio/
 * dmix plugins, works out of the box on a normal desktop); scenarios without an
 * audio service / connecting directly to hardware can use
 * -DPLAY_LOCAL_OUT_PCM_DEVICE='"plughw:0,0"' to override it. */
#ifndef PLAY_LOCAL_OUT_PCM_DEVICE
#define PLAY_LOCAL_OUT_PCM_DEVICE "default"
#endif

/* Device buffer: 20ms period x 4 = ~80ms backlog (upper bound of the blocking write latency; upper bound of the drain wait) */
#define PLAY_LOCAL_OUT_PERIOD_FRAMES (PLAY_LOCAL_OUT_SAMPLE_RATE / 50)
#define PLAY_LOCAL_OUT_PERIODS       (4)

/* Software gain chunk buffer: write's len is capped by the decode buffer
 * (AI_PLAYER_DECODEBUF_SIZE, 8KB by default), so 4KB chunks are enough; used
 * only when volume is in (0,100) */
#define PLAY_LOCAL_OUT_GAIN_BUF_SIZE (4096)

typedef struct {
    snd_pcm_t      *pcm;    /* NULL = not open (no sound card / already closed): write degrades to discard */
    uint32_t        volume; /* 0-100, default 100 (full); 0 = mute (writes zero samples) */
    pthread_mutex_t lock;   /* protects the pcm lifecycle: write/stop run on the playback thread,
                             * close/open may run in set_consumer (main thread) — see svc_ai_player.c */
    uint8_t         gain_buf[PLAY_LOCAL_OUT_GAIN_BUF_SIZE];
    /* Dedicated zero buffer for mute: no code path writes to it, all-zero is
     * guaranteed by static initialization — mute must not reuse gain_buf (a
     * gain_buf used by the gain path still holds the previous audio samples and
     * reusing it would play those old samples as "zero") */
    uint8_t         zero_buf[PLAY_LOCAL_OUT_GAIN_BUF_SIZE];
} PLAY_LOCAL_OUT_CONSUMER_CTX_T;

/* Singleton: svc attaches only one consumer globally (tuya_ai_player_set_consumer
 * also replaces globally). The struct is permanent (not freed on close); after
 * close, write only sees pcm==NULL and returns safely. */
static PLAY_LOCAL_OUT_CONSUMER_CTX_T s_ai_player_consumer = {
    .pcm    = NULL,
    .volume = 100,
    .lock   = PTHREAD_MUTEX_INITIALIZER,
};

/* ---------------------------------------------------------------------------
 * Open/Close
 * --------------------------------------------------------------------------- */

static OPERATE_RET __ai_player_consumer_open(PLAYER_CONSUMER_HANDLE *handle)
{
    snd_pcm_t          *pcm    = NULL;
    snd_pcm_hw_params_t *params = NULL;
    unsigned int        rate    = PLAY_LOCAL_OUT_SAMPLE_RATE;
    unsigned int        periods = PLAY_LOCAL_OUT_PERIODS;
    snd_pcm_uframes_t   period  = PLAY_LOCAL_OUT_PERIOD_FRAMES;
    int                 err     = 0;

    pthread_mutex_lock(&s_ai_player_consumer.lock);

    if (s_ai_player_consumer.pcm) { /* idempotent: reuse if already open (prevents leaks from repeated open) */
        if (handle) {
            *handle = (PLAYER_CONSUMER_HANDLE)&s_ai_player_consumer;
        }
        pthread_mutex_unlock(&s_ai_player_consumer.lock);
        return OPRT_OK;
    }

    err = snd_pcm_open(&pcm, PLAY_LOCAL_OUT_PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        PR_ERR("ai player consumer: snd_pcm_open(%s) failed: %s", PLAY_LOCAL_OUT_PCM_DEVICE, snd_strerror(err));
        goto __fail;
    }

    /* Full hw_params sequence: interleaved access / S16_LE / mono / 16k (near lets the plugin layer negotiate) */
    snd_pcm_hw_params_alloca(&params);
    if ((err = snd_pcm_hw_params_any(pcm, params)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params_set_channels(pcm, params, PLAY_LOCAL_OUT_CHANNELS)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params_set_rate_near(pcm, params, &rate, 0)) < 0) {
        goto __fail;
    }
    if (rate != PLAY_LOCAL_OUT_SAMPLE_RATE) {
        /* The plug layer of the default device resamples, so a different negotiated value is not fatal; logged as-is for troubleshooting */
        PR_WARN("ai player consumer: rate negotiated to %u (expected %d)", rate, PLAY_LOCAL_OUT_SAMPLE_RATE);
    }
    if ((err = snd_pcm_hw_params_set_period_size_near(pcm, params, &period, 0)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params_set_periods_near(pcm, params, &periods, 0)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params(pcm, params)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_prepare(pcm)) < 0) {
        goto __fail;
    }

    s_ai_player_consumer.pcm = pcm;
    if (handle) {
        *handle = (PLAYER_CONSUMER_HANDLE)&s_ai_player_consumer;
    }
    pthread_mutex_unlock(&s_ai_player_consumer.lock);

    PR_NOTICE("ai player consumer: ALSA %s opened (%d Hz, 16 bit, mono, period=%lu x %u)",
              PLAY_LOCAL_OUT_PCM_DEVICE, PLAY_LOCAL_OUT_SAMPLE_RATE, (unsigned long)period, periods);
    return OPRT_OK;

__fail:
    if (pcm) {
        snd_pcm_close(pcm);
    }
    if (handle) {
        *handle = NULL;
    }
    pthread_mutex_unlock(&s_ai_player_consumer.lock);

    /* Degrade instead of fail: the playback link (decode/resample/mix) keeps running, only the sound output is gone */
    PR_ERR("ai player consumer: ALSA open failed: %s (audio output disabled)", snd_strerror(err));
    return OPRT_COM_ERROR;
}

static OPERATE_RET __ai_player_consumer_close(PLAYER_CONSUMER_HANDLE handle)
{
    (void)handle;

    pthread_mutex_lock(&s_ai_player_consumer.lock);

    if (s_ai_player_consumer.pcm) {
        /* drop: do not wait for the device buffer to finish (close is used for
         * set_consumer replacement / service exit and stops immediately; the
         * "play the tail out" semantics live in stop's drain) */
        snd_pcm_drop(s_ai_player_consumer.pcm);
        snd_pcm_close(s_ai_player_consumer.pcm);
        s_ai_player_consumer.pcm = NULL;
    }

    pthread_mutex_unlock(&s_ai_player_consumer.lock);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Playback control
 * --------------------------------------------------------------------------- */

static OPERATE_RET __ai_player_consumer_start(PLAYER_CONSUMER_HANDLE handle)
{
    int err = 0;

    (void)handle;

    pthread_mutex_lock(&s_ai_player_consumer.lock);

    if (s_ai_player_consumer.pcm) {
        /* prepared on open; after the previous stop's drain the device is back
         * in SETUP, so prepare again before replaying (idempotent for an
         * already PREPARED device) */
        err = snd_pcm_prepare(s_ai_player_consumer.pcm);
        if (err < 0) {
            PR_ERR("ai player consumer: snd_pcm_prepare failed: %s", snd_strerror(err));
        }
    }

    pthread_mutex_unlock(&s_ai_player_consumer.lock);
    return (err < 0) ? OPRT_COM_ERROR : OPRT_OK;
}

/**
 * @brief Blocking write of exactly len bytes (absorbs short writes in a loop; xrun/suspend recovered and retried)
 *
 * The caller (svc_ai_player.c) ignores the write return value, so short writes
 * must be completed in a loop here; when len is not a multiple of the frame size
 * the trailing bytes of a partial frame are dropped (mono/16bit frame = 2 bytes,
 * and callers always pass whole frames, so this never triggers). 
 */
static OPERATE_RET __ai_player_consumer_write_all(const uint8_t *data, uint32_t len)
{
    uint32_t frames = len / PLAY_LOCAL_OUT_FRAME_BYTES;
    uint32_t done   = 0;

    while (done < frames) {
        snd_pcm_sframes_t n = snd_pcm_writei(s_ai_player_consumer.pcm, data + done * PLAY_LOCAL_OUT_FRAME_BYTES,
                                             frames - done);
        if (n < 0) {
            /* -EPIPE: xrun (underrun); -ESTRPIPE: device suspended; -EINTR: interrupted by a signal.
             * snd_pcm_recover prepares/resumes/retries respectively, then writing continues on success */
            int rc = snd_pcm_recover(s_ai_player_consumer.pcm, (int)n, 1);
            if (rc < 0) {
                PR_ERR("ai player consumer: pcm write error: %s (recover failed: %s)", snd_strerror((int)n),
                       snd_strerror(rc));
                return OPRT_COM_ERROR;
            }
            PR_WARN("ai player consumer: pcm write recovered: %s", snd_strerror((int)n));
            continue;
        }
        done += (uint32_t)n;
    }

    return OPRT_OK;
}

/**
 * @brief Software gain: 16bit sample x volume/100 (fixed point q = volume*256/100)
 *
 * The target platforms (Linux x86/arm64) are little-endian, matching the S16_LE
 * sample byte order, so samples are handled directly as int16_t; for q <= 256,
 * 32767*q>>8 <= 32767 and there is no overflow. 
 */
static void __ai_player_consumer_apply_gain(uint8_t *dst, const uint8_t *src, uint32_t len, uint32_t volume)
{
    uint32_t       q = volume * 256 / 100;
    const int16_t *in = (const int16_t *)src;
    int16_t       *out = (int16_t *)dst;
    uint32_t       i   = 0;

    for (i = 0; i < len / sizeof(int16_t); i++) {
        out[i] = (int16_t)(((int32_t)in[i] * (int32_t)q) >> 8);
    }
}

static OPERATE_RET __ai_player_consumer_write(PLAYER_CONSUMER_HANDLE handle, const void *buf, uint32_t len)
{
    OPERATE_RET rt = OPRT_OK;

    (void)handle;

    if (!buf || !len) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&s_ai_player_consumer.lock);

    if (!s_ai_player_consumer.pcm) {
        pthread_mutex_unlock(&s_ai_player_consumer.lock);
        return OPRT_COM_ERROR; /* device not open: discard (svc ignores the return value, the link is unaffected) */
    }

    if (0 == s_ai_player_consumer.volume) {
        /* Mute: write zero samples (keeps device timing and playback duration
         * instead of discarding data and fast-forwarding). Uses the dedicated
         * zero_buf (statically all-zero, never written), not gain_buf */
        uint32_t remain = len;
        while (remain) {
            uint32_t chunk = (remain > sizeof(s_ai_player_consumer.zero_buf)) ? sizeof(s_ai_player_consumer.zero_buf)
                                                                            : remain;
            rt = __ai_player_consumer_write_all(s_ai_player_consumer.zero_buf, chunk);
            if (OPRT_OK != rt) {
                break;
            }
            remain -= chunk;
        }
    } else if (s_ai_player_consumer.volume < 100) {
        /* Intermediate volume: apply gain per chunk then write (does not modify the caller's const buf) */
        const uint8_t *p      = (const uint8_t *)buf;
        uint32_t       remain = len;

        while (remain) {
            uint32_t chunk = (remain > sizeof(s_ai_player_consumer.gain_buf)) ? sizeof(s_ai_player_consumer.gain_buf)
                                                                             : remain;
            __ai_player_consumer_apply_gain(s_ai_player_consumer.gain_buf, p, chunk, s_ai_player_consumer.volume);
            rt = __ai_player_consumer_write_all(s_ai_player_consumer.gain_buf, chunk);
            if (OPRT_OK != rt) {
                break;
            }
            p += chunk;
            remain -= chunk;
        }
    } else {
        /* Full volume (default): pass through with zero copy */
        rt = __ai_player_consumer_write_all((const uint8_t *)buf, len);
    }

    pthread_mutex_unlock(&s_ai_player_consumer.lock);
    return rt;
}

static OPERATE_RET __ai_player_consumer_stop(PLAYER_CONSUMER_HANDLE handle)
{
    int err = 0;

    (void)handle;

    pthread_mutex_lock(&s_ai_player_consumer.lock);

    if (s_ai_player_consumer.pcm) {
        /* drain: play out the data already written into the device buffer
         * before stopping (TTS tail is not cut off). Failures (e.g. state after
         * an xrun) are ignored — the next start's prepare recovers */
        err = snd_pcm_drain(s_ai_player_consumer.pcm);
        if (err < 0) {
            PR_WARN("ai player consumer: snd_pcm_drain failed: %s", snd_strerror(err));
        }
    }

    pthread_mutex_unlock(&s_ai_player_consumer.lock);
    return OPRT_OK;
}

static OPERATE_RET __ai_player_consumer_set_volume(PLAYER_CONSUMER_HANDLE handle, uint32_t volume)
{
    (void)handle;

    if (volume > 100) {
        volume = 100;
    }

    pthread_mutex_lock(&s_ai_player_consumer.lock);
    s_ai_player_consumer.volume = volume;
    pthread_mutex_unlock(&s_ai_player_consumer.lock);

    PR_DEBUG("ai player consumer: volume set to %u", volume);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Injection entry
 * --------------------------------------------------------------------------- */

AI_PLAYER_CONSUMER_T g_ai_player_consumer = {
    .open       = __ai_player_consumer_open,
    .close      = __ai_player_consumer_close,
    .start      = __ai_player_consumer_start,
    .write      = __ai_player_consumer_write,
    .stop       = __ai_player_consumer_stop,
    .set_volume = __ai_player_consumer_set_volume,
};

OPERATE_RET play_local_out_install(void)
{
    /* Must be called after tuya_ai_player_service_init(): service_init loads the
     * default empty consumer and overwrites consumer, so set_consumer here is the
     * final replacement point (see svc_ai_player.c) */
    return tuya_ai_player_set_consumer(&g_ai_player_consumer);
}

#endif /* ENABLE_AI_PLAYER */
