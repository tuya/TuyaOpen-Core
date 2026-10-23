/**
 * @file ai_player_types.h
 * @brief Audio parameter enums owned by the player: sample rate, bit width and
 *        channel count used by the player configuration and decoder output.
 *
 * The three enums define the values the audio_player public configuration
 * (AI_PLAYER_CFG_T) and the decoder output (DECODER_OUTPUT_T) carry. A
 * platform consumer injected by the port maps these values to its own audio
 * configuration; the player holds no platform audio handle and does not
 * include platform audio headers. Consumers should assert at compile time that
 * each value and the type size correspond to their platform audio definitions.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __AI_PLAYER_TYPES_H__
#define __AI_PLAYER_TYPES_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bit width values carried by the player configuration and decoder output */
typedef enum {
    CORE_AUDIO_DATABITS_8   = 8,
    CORE_AUDIO_DATABITS_16  = 16,
    CORE_AUDIO_DATABITS_MAX = 0xFF
} CORE_AUDIO_DATABITS_E;

/* Channel values carried by the player configuration and decoder output; the
 * player itself uses MONO/STEREO, and the TDM entries are available for
 * consumers that support them */
typedef enum {
    CORE_AUDIO_CHANNEL_MONO    = 1,
    CORE_AUDIO_CHANNEL_STEREO,
    CORE_AUDIO_CHANNEL_TDM3CHS,
    CORE_AUDIO_CHANNEL_TDM4CHS,
    CORE_AUDIO_CHANNEL_TDM5CHS,
    CORE_AUDIO_CHANNEL_TDM6CHS,
    CORE_AUDIO_CHANNEL_TDM7CHS,
    CORE_AUDIO_CHANNEL_TDM8CHS
} CORE_AUDIO_CHANNEL_E;

/* Sample rate values in Hz; 11K/22K/44K are the values used by the platform
 * audio definitions */
typedef enum {
    CORE_AUDIO_SAMPLE_8K   = 8000,
    CORE_AUDIO_SAMPLE_11K  = 11000,
    CORE_AUDIO_SAMPLE_12K  = 12000,
    CORE_AUDIO_SAMPLE_16K  = 16000,
    CORE_AUDIO_SAMPLE_22K  = 22000,
    CORE_AUDIO_SAMPLE_24K  = 24000,
    CORE_AUDIO_SAMPLE_32K  = 32000,
    CORE_AUDIO_SAMPLE_44K  = 44000,
    CORE_AUDIO_SAMPLE_48K  = 48000,
    CORE_AUDIO_SAMPLE_MAX  = 0xFFFFFFFF
} CORE_AUDIO_SAMPLE_E;

#ifdef __cplusplus
}
#endif

#endif /* __AI_PLAYER_TYPES_H__ */
