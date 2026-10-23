/**
 * @file skill_emotion.c
 * @brief Emotion skill implementation.
 *
 * This file provides functions for parsing and processing emotion expressions
 * from AI skill responses, including emoji conversion and playback.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */
#include "tuya_cloud_types.h"
#include "mix_method.h"

#include "core_api.h"
#include "cJSON.h"

#include "ai_user_event.h"
#include "skill_emotion.h"

/***********************************************************
************************macro define************************
***********************************************************/


/***********************************************************
***********************typedef define***********************
***********************************************************/
AI_AGENT_EMO_T cAI_AGENT_EMO[] = {
    {"U+1F636", EMOJI_NEUTRAL},      /* 😶 */
    {"U+1F642", EMOJI_HAPPY},        /* 🙂 */
    {"U+1F606", EMOJI_LAUGHING},     /* 😆 */
    {"U+1F602", EMOJI_FUNNY},        /* 😂 */
    {"U+1F614", EMOJI_SAD},          /* 😔 */
    {"U+1F620", EMOJI_ANGRY},        /* 😠 */
    {"U+1F62D", EMOJI_FEARFUL},      /* 😭 */
    {"U+1F60D", EMOJI_LOVING},       /* 😍 */
    {"U+1F633", EMOJI_EMBARRASSED},  /* 😳 */
    {"U+1F62F", EMOJI_SURPRISE},     /* 😯 */
    {"U+1F631", EMOJI_SHOCKED},      /* 😱 */
    {"U+1F914", EMOJI_THINKING},     /* 🤔 */
    {"U+1F609", EMOJI_WINK},         /* 😉 */
    {"U+1F60E", EMOJI_COOL},         /* 😎 */
    {"U+1F60C", EMOJI_RELAXED},      /* 😌 */
    {"U+1F924", EMOJI_DELICIOUS},    /* 🤤 */
    {"U+1F618", EMOJI_KISSY},        /* 😘 */
    {"U+1F60F", EMOJI_CONFIDENT},    /* 😏 */
    {"U+1F634", EMOJI_SLEEP},        /* 😴 */
    {"U+1F61C", EMOJI_SILLY},        /* 😜 */
    {"U+1F644", EMOJI_CONFUSED}      /* 🙄 */
};

/***********************************************************
***********************variable define**********************
***********************************************************/


/***********************************************************
***********************function define**********************
***********************************************************/
static const char *__json_get_string(const cJSON *item)
{
    if (item == NULL || !cJSON_IsString(item) || item->valuestring == NULL) {
        return NULL;
    }
    return item->valuestring;
}

/**
 * @brief Process emotion skill data from JSON.
 *
 * @param json JSON object containing emotion skill data.
 * @return OPERATE_RET Operation result code.
 */
OPERATE_RET ai_skill_emo_process(cJSON *json)
{
    cJSON *emotion, *text;
    AI_AGENT_EMO_T emo;
    int emo_cnt = 0;

    json = cJSON_GetObjectItem(json, "skillContent");
    TUYA_CHECK_NULL_RETURN(json, OPRT_OK);

    emotion = cJSON_GetObjectItem(json, "emotion");
    text = cJSON_GetObjectItem(json, "text");
    TUYA_CHECK_NULL_RETURN(emotion, OPRT_CJSON_GET_ERR);
    TUYA_CHECK_NULL_RETURN(text, OPRT_CJSON_GET_ERR);

    if (!cJSON_IsArray(emotion) || !cJSON_IsArray(text)) {
        PR_ERR("emotion or text is not array");
        return OPRT_CJSON_GET_ERR;
    }

    emo_cnt = cJSON_GetArraySize(emotion);
    if (emo_cnt == 0) {
        PR_ERR("emo array is empty");
        return OPRT_CJSON_GET_ERR;
    }

    for (int i = 0; i < emo_cnt; i++) {
        emo.emoji = __json_get_string(cJSON_GetArrayItem(text, i));
        emo.name = __json_get_string(cJSON_GetArrayItem(emotion, i));
        if (!emo.emoji || !emo.name) {
            PR_ERR("emo item at index %d is NULL", i);
            continue;
        }
        ai_agent_play_emo(&emo);
    }

    return OPRT_OK;
}


const char* ai_agent_emoji_get_name(const char* emoji)
{
    TUYA_CHECK_NULL_RETURN(emoji, NULL);
    
    for (size_t i = 0; i < CNTSOF(cAI_AGENT_EMO); i++) {
        if (strcmp(cAI_AGENT_EMO[i].emoji, emoji) == 0) {
            return cAI_AGENT_EMO[i].name;
        }
    }

    PR_NOTICE("not found emoji: %s return %s as default", emoji, cAI_AGENT_EMO[0].name);
    
    return cAI_AGENT_EMO[0].name; /* Emoji not found, use neutral as default */
}

/**
 * @brief Play emotion expression.
 *
 * @param emo Pointer to emotion structure.
 * @return OPERATE_RET Operation result code.
 */
OPERATE_RET ai_agent_play_emo(AI_AGENT_EMO_T *emo)
{
    /* Send data to registered callback */
    ai_user_event_notify(AI_USER_EVT_EMOTION, emo);

    return OPRT_OK;    
}
