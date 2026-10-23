/**
 * @file ai_player_decoder.h
 * @brief Audio decoder interface of the player: decoder handle, decoded output
 *        descriptor and the decoder lifecycle operations.
 *
 * A decoder is created with ai_player_decoder_init, bound to a codec with
 * ai_player_decoder_start (MP3 and WAV are available; OPUS and OGGOPUS are
 * enabled by their build switches), fed through ai_player_decoder_process into
 * a caller-provided output buffer, and released with ai_player_decoder_deinit.
 * DECODER_OUTPUT_T reports the output sample rate, bit width, channel count
 * and frame size; ai_player_decoder_get_codec returns the codec currently
 * bound.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 */

#ifndef __AI_PLAYER_DECODER_H__
#define __AI_PLAYER_DECODER_H__
#include "tuya_cloud_types.h"
#include "ai_player_types.h"   // audio parameter enums used by the player (CORE_AUDIO_*)
#include "svc_ai_player.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* PLAYER_DECODER;

typedef struct {
    CORE_AUDIO_SAMPLE_E sample;      // Audio sample rate
    CORE_AUDIO_DATABITS_E datebits;  // Audio data bits per sample
    CORE_AUDIO_CHANNEL_E channel;    // Audio channel count
    uint32_t samples;                 // Audio samples
    uint32_t used_size;               // Audio frame size in bytes
} DECODER_OUTPUT_T;

OPERATE_RET ai_player_decoder_init(PLAYER_DECODER *handle);
OPERATE_RET ai_player_decoder_deinit(PLAYER_DECODER handle);
OPERATE_RET ai_player_decoder_start(PLAYER_DECODER handle, AI_AUDIO_CODEC_E codec);
OPERATE_RET ai_player_decoder_stop(PLAYER_DECODER handle);
int ai_player_decoder_process(PLAYER_DECODER handle, uint8_t *in_buf, int in_len, uint8_t *out_buf, int out_size, DECODER_OUTPUT_T *output);
AI_AUDIO_CODEC_E ai_player_decoder_get_codec(PLAYER_DECODER handle);

#ifdef __cplusplus
}
#endif

#endif  // __AI_PLAYER_DECODER_H__
