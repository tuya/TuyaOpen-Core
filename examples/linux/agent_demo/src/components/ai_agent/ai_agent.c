/**
 * @file ai_agent.c
 * @brief AI agent module: cloud AI session setup and output callback dispatch
 *
 * ai_agent_init() configures the agent attributes (uplink PCM
 * 16kHz/16bit/mono), installs the output callbacks (event, media attribute,
 * media data, text, alert) and subscribes the session initialization callback
 * to EVENT_AI_CLIENT_RUN. Received text is dispatched to ai_text_process();
 * audio events start, stop and feed the TTS player when ENABLE_COMP_AI_AUDIO
 * is on; alerts other than AT_PLEASE_AGAIN are forwarded as
 * AI_USER_EVT_PLAY_ALERT.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_cloud_types.h"

#include <stdio.h>

#include "core_api.h"
#include "cJSON.h"
#include "atop_service.h"
#include "tuya_ai_agent.h"
#include "tuya_ai_output.h"

#include "ai_user_event.h"
#include "ai_skill.h"

#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
#include "ai_audio_player.h"
#endif

#if defined(ENABLE_AI_MONITOR) && (ENABLE_AI_MONITOR == 1)
#include "tuya_ai_monitor.h"
#endif
/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
***********************variable define**********************
***********************************************************/
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
static uint16_t __s_audio_codec_type = AI_AUDIO_CODEC_MP3;
#endif
/***********************************************************
***********************function define**********************
***********************************************************/
/**
@brief Callback function for AI agent events
@param type Event type
@param ptype Packet type
@param eid Event ID
@return OPERATE_RET Operation result
*/
OPERATE_RET __ai_agent_event_cb(AI_EVENT_TYPE type, AI_PACKET_PT ptype, AI_EVENT_ID eid)
{
    PR_DEBUG("ai agent -> recv event type: %d packet_tp:%d", type, ptype);

    if (AI_EVENT_START == type) {
        if (AI_PT_AUDIO == ptype) {
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
            /* Start audio player */
            ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_START, __s_audio_codec_type, NULL, 0);
#endif
        }
    } else if ((AI_EVENT_CHAT_BREAK == type)) {
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
        /* Cloud break, stop audio player */
        ai_audio_player_stop(AI_AUDIO_PLAYER_FG);
#endif
        ai_user_event_notify(AI_USER_EVT_CHAT_BREAK, NULL);
    }else if(AI_EVENT_SERVER_VAD == type) {
		ai_user_event_notify(AI_USER_EVT_SERVER_VAD, NULL);
	} else if ((AI_EVENT_END == type)) {
        if (AI_PT_AUDIO == ptype) {
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
            /* Stop audio player */
            ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_STOP, __s_audio_codec_type, NULL, 0);
#endif
        }
    } else if (AI_EVENT_CHAT_EXIT == type) {
        /* Stop audio player */
        ai_user_event_notify(AI_USER_EVT_EXIT, NULL);
    }

    return OPRT_OK;
}

/**
@brief Callback function for receiving media attributes
@param attr Pointer to media attribute information
@return OPERATE_RET Operation result
*/
OPERATE_RET __ai_agent_media_attr_cb(AI_BIZ_ATTR_INFO_T *attr)
{
    /* PR_DEBUG(" ai agent -> recv media attr type: %d", attr->type); */
    if (attr->type == AI_PT_AUDIO && attr->flag & AI_HAS_ATTR) {
        PR_DEBUG("ai agent -> audio codec type: %d", attr->value.audio.base.codec_type);
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
        switch (attr->value.audio.base.codec_type) {
            case AUDIO_CODEC_MP3:
                __s_audio_codec_type = AI_AUDIO_CODEC_MP3;
                break;
            case AUDIO_CODEC_OPUS:
                __s_audio_codec_type = AI_AUDIO_CODEC_OPUS;
                break;
            default:
                __s_audio_codec_type = AI_AUDIO_CODEC_MAX;
                break;
        }
#endif
    }
    return OPRT_OK;
}

/**
@brief Callback function for receiving media data
@param type Packet type
@param data Pointer to media data
@param len Data length
@param total_len Total data length
@return OPERATE_RET Operation result
*/
OPERATE_RET __ai_agent_media_data_cb(AI_PACKET_PT type, char *data, uint32_t len, uint32_t total_len)
{
    /* CORE_PR_NOTICE(" ai agent -> recv media type %d", type); */
    OPERATE_RET rt = OPRT_OK;
    if(type == AI_PT_AUDIO) {
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
        rt = ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_DATA, __s_audio_codec_type, (char*)data, len);
#endif
    } else if(type == AI_PT_VIDEO) {
        /* TBD */
    } else if(type == AI_PT_IMAGE) {
        PR_DEBUG("recv image data, len:%d, total_len:%d", len, total_len);
    } else if(type == AI_PT_FILE) {
        /* TBD */
    }
    return rt;
}

/**
@brief Callback function for receiving text data
@param type Text type (ASR, NLG, SKILL, etc.)
@param root JSON root object containing text data
@param eof End of file flag
@return OPERATE_RET Operation result
*/
OPERATE_RET __ai_agent_text_cb(AI_TEXT_TYPE_E type, cJSON *root, BOOL_T eof)
{    
    return ai_text_process(type, root, eof);
}

/**
@brief Callback function for receiving alert notifications
@param type Alert type
@return OPERATE_RET Operation result
*/
OPERATE_RET __ai_agent_alert_cb(AI_ALERT_TYPE_E type)
{
    /* PR_DEBUG(" ai agent -> alert type: %d", type); */
    if (type == AT_PLEASE_AGAIN) {
        PR_DEBUG("ignored alert: %d", type);
        return OPRT_OK;
    }

    ai_user_event_notify(AI_USER_EVT_PLAY_ALERT, (void*)type);   

    return OPRT_OK;
}

/**
 * @brief Session initialization callback function
 *
 * This callback is called to initialize a new session.
 *
 * @param[in] data  Reserved parameter, currently unused.
 * @return int      Returns 0 on success.
 */
static int session_init_cb(void *data)
{
    (void)data;
    tuya_ai_agent_crt_session("", 0, 0, NULL, 0);
    return 0;
}

/**
@brief Initialize the AI agent module
@return OPERATE_RET Operation result
*/
OPERATE_RET ai_agent_init(void)
{
    OPERATE_RET rt = OPRT_OK;
    AI_AGENT_CFG_T ai_agent_cfg = {0};

    memset(&ai_agent_cfg, 0x00, sizeof(AI_AGENT_CFG_T));

    /* The application feeds PCM directly on the uplink (the port's file
     * playback), with attributes fixed to the sample format 16kHz/16bit/mono
     * and no encoder (PCM pass-through).
     * Note: if not set, attr.audio is all 0, the cloud cannot get the format
     * and will not return ASR/NLG. */
    ai_agent_cfg.attr.audio.codec_type  = AUDIO_CODEC_PCM;
    ai_agent_cfg.attr.audio.sample_rate = 16000;
    ai_agent_cfg.attr.audio.channels    = AUDIO_CHANNELS_MONO;
    ai_agent_cfg.attr.audio.bit_depth   = 16;

    ai_agent_cfg.output.alert_cb      = __ai_agent_alert_cb;
    ai_agent_cfg.output.text_cb       = __ai_agent_text_cb;
    ai_agent_cfg.output.media_data_cb = __ai_agent_media_data_cb;
    ai_agent_cfg.output.media_attr_cb = __ai_agent_media_attr_cb;
    ai_agent_cfg.output.event_cb      = __ai_agent_event_cb;

    TUYA_CALL_ERR_RETURN(tuya_ai_agent_init(&ai_agent_cfg));

    TUYA_CALL_ERR_LOG(core_event_subscribe(EVENT_AI_CLIENT_RUN, "agent_session_init", session_init_cb, SUBSCRIBE_TYPE_NORMAL));

#if defined(ENABLE_AI_MONITOR) && (ENABLE_AI_MONITOR == 1)
    /* monitor is a debug monitoring service (TCP 5055): failures (e.g. port in
     * use) only raise a degraded warning and do not block the AI link —
     * otherwise a monitor fault would make ai_agent_init return an error in the
     * half-success state "AI already initialized", and a caller retrying on the
     * failure would initialize again. */
    ai_monitor_config_t monitor_cfg = AI_MONITOR_CFG_DEFAULT;
    TUYA_CALL_ERR_LOG(tuya_ai_monitor_init(&monitor_cfg));
#endif

    return rt;
}
