/**
 * @file play_local_in.c
 * @brief agent_demo audio input source (Linux): PCM file read through stdio
 *
 * The source is a PCM file opened with fopen and read with fread; the demo has
 * no ALSA/microphone capture path. One source is open at a time. The read
 * contract distinguishes >0 bytes, 0 for end of data and <0 for an I/O error,
 * and agent_audio.c consumes it in 80ms frames (2560 bytes at
 * 16kHz/16bit/mono).
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "play_local_in.h"

#include <stdio.h>

/* Current audio source (single instance; replace with a microphone handle on real devices) */
static FILE *s_ai_src = NULL;

OPERATE_RET play_local_in_open(const char *src_path)
{
    if (s_ai_src) {
        return OPRT_COM_ERROR; /* already open, guard against re-entry */
    }
    if (!src_path) {
        return OPRT_INVALID_PARM;
    }

    s_ai_src = fopen(src_path, "rb");
    if (!s_ai_src) {
        printf("play_local_in: open %s failed\n", src_path);
        return OPRT_NOT_FOUND;
    }

    return OPRT_OK;
}

INT_T play_local_in_read(UINT8_T *buf, UINT_T len)
{
    size_t n = 0;

    if (!s_ai_src || !buf || !len) {
        return -1;
    }

    n = fread(buf, 1, len, s_ai_src);
    if ((0 == n) && ferror(s_ai_src)) {
        /* I/O error (distinct from EOF): the contract requires <0 and 0 (EOF)
         * to be reported separately. Swallowing ferror would treat a disk read
         * error / a directory used as the sample as "read finished", and the
         * caller would report success with 0 bytes. */
        return -1;
    }

    return (INT_T)n; /* 0 = end of data (EOF) */
}

OPERATE_RET play_local_in_close(VOID)
{
    if (s_ai_src) {
        fclose(s_ai_src);
        s_ai_src = NULL;
    }

    return OPRT_OK;
}
