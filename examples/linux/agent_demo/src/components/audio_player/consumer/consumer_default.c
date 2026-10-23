/**
 * @file consumer_default.c
 * @brief audio_player default consumer (AI_PLAYER_CONSUMER_T): empty implementation on the core side
 *
 * The core side uses an empty placeholder:
 *
 * - All six callbacks return OPRT_OK and produce no sound;
 * - Platforms (port) must implement their own AI_PLAYER_CONSUMER_T (writing PCM
 *   to the platform audio output) and inject it through
 *   tuya_ai_player_set_consumer() after initialization;
 * - tuya_ai_player_service_init() loads this default instance first, so without
 *   an injection the player still runs the whole unpack → decode → mix/resample
 *   chain, with the final PCM discarded — handy for testing the link without
 *   audio hardware, and it also keeps core from depending on platform audio
 *   services.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "core_log.h"
#include "svc_ai_player.h"

static OPERATE_RET consumer_default_open(PLAYER_CONSUMER_HANDLE *handle)
{
    if (handle) {
        *handle = NULL;
    }

    PR_DEBUG("ai player default consumer: no audio output, "
             "inject a platform consumer via tuya_ai_player_set_consumer()");

    return OPRT_OK;
}

static OPERATE_RET consumer_default_close(PLAYER_CONSUMER_HANDLE handle)
{
    return OPRT_OK;
}

static OPERATE_RET consumer_default_start(PLAYER_CONSUMER_HANDLE handle)
{
    return OPRT_OK;
}

static OPERATE_RET consumer_default_write(PLAYER_CONSUMER_HANDLE handle, const void *buf, uint32_t len)
{
    return OPRT_OK;
}

static OPERATE_RET consumer_default_stop(PLAYER_CONSUMER_HANDLE handle)
{
    return OPRT_OK;
}

static OPERATE_RET consumer_default_set_volume(PLAYER_CONSUMER_HANDLE handle, uint32_t volume)
{
    return OPRT_OK;
}

AI_PLAYER_CONSUMER_T g_consumer_default = {
    .open = consumer_default_open,
    .close = consumer_default_close,
    .start = consumer_default_start,
    .write = consumer_default_write,
    .stop = consumer_default_stop,
    .set_volume = consumer_default_set_volume
};
