/**
 * @file ai_user_event.h
 * @brief AI user event interface: the event number enumeration, the event and
 *        payload types, and the register / notify functions.
 *
 * A single registered callback (AI_USER_EVENT_NOTIFY) receives every
 * AI_NOTIFY_EVENT_T. The data pointer of that structure carries the payload
 * defined for the event: AI_NOTIFY_TEXT_T for text streams, AI_AGENT_EMO_T for
 * emotions, and AI_NOTIFY_MIC_DATA_T for microphone data. Components publish
 * events with ai_user_event_notify; the application registers its handler with
 * ai_user_event_notify_register.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __AI_USER_EVENT_H__
#define __AI_USER_EVENT_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
/* Every event number is assigned explicitly so it stays stable. Append new
 * values for new events and never reuse the number of an unused slot: new
 * semantics must not take over old numbers. Application subscriptions rely on
 * these numbers. */
typedef enum {
    AI_USER_EVT_ASR_EMPTY = 0,
    AI_USER_EVT_ASR_OK = 1,
    AI_USER_EVT_TTS_PRE = 4,
    AI_USER_EVT_TTS_START = 5,
    AI_USER_EVT_TTS_DATA = 6,
    AI_USER_EVT_TTS_STOP = 7,
    AI_USER_EVT_TTS_ABORT = 8,
    AI_USER_EVT_TTS_ERROR = 9, /* application-facing event */
    AI_USER_EVT_TEXT_STREAM_START = 11,
    AI_USER_EVT_TEXT_STREAM_DATA = 12,
    AI_USER_EVT_TEXT_STREAM_STOP = 13,
    AI_USER_EVT_EMOTION = 15,
    AI_USER_EVT_SKILL = 17,
    AI_USER_EVT_CHAT_BREAK = 18,
    AI_USER_EVT_SERVER_VAD = 19,
    AI_USER_EVT_PLAY_CTL_PLAY = 21,
    AI_USER_EVT_PLAY_CTL_RESUME = 22,
    AI_USER_EVT_PLAY_CTL_PAUSE = 23,
    AI_USER_EVT_PLAY_CTL_REPLAY = 24,
    AI_USER_EVT_PLAY_CTL_PREV = 25,
    AI_USER_EVT_PLAY_CTL_NEXT = 26,
    AI_USER_EVT_PLAY_CTL_SEQUENTIAL = 27,
    AI_USER_EVT_PLAY_CTL_SEQUENTIAL_LOOP = 28,
    AI_USER_EVT_PLAY_CTL_SINGLE_LOOP = 29,
    AI_USER_EVT_PLAY_CTL_END = 30,
    AI_USER_EVT_PLAY_END = 31,
    AI_USER_EVT_PLAY_ALERT = 32,
    AI_USER_EVT_EXIT = 38,
} AI_USER_EVT_TYPE_E;

typedef struct {
    AI_USER_EVT_TYPE_E type;
    void              *data;
} AI_NOTIFY_EVENT_T;

typedef void (*AI_USER_EVENT_NOTIFY)(AI_NOTIFY_EVENT_T *event);

typedef struct {
    char    *data;
    uint16_t datalen;
    uint32_t timeindex;
} AI_NOTIFY_TEXT_T;

/* Emotion payload type: carried as the data pointer of AI_USER_EVT_EMOTION.
 * It is defined here rather than in ai_skills so that utility stays free of a
 * dependency on the skill layer; skill_emotion.h includes this header and
 * reuses AI_AGENT_EMO_T. */
typedef struct {
    const char  *emoji;
    const char  *name;
} AI_AGENT_EMO_T;

typedef struct {
    uint8_t *data;
    uint32_t data_len;
} AI_NOTIFY_MIC_DATA_T;

/***********************************************************
********************function declaration********************
***********************************************************/
/**
@brief Register a callback function for AI user event notifications
@param cb Callback function pointer to be called when events occur
@return None
*/
void ai_user_event_notify_register(AI_USER_EVENT_NOTIFY cb);

/**
@brief Notify registered callback about an AI user event
@param type Event type to notify
@param data Pointer to event data (can be NULL)
@return None
*/
void ai_user_event_notify(AI_USER_EVT_TYPE_E type, void *data);

#ifdef __cplusplus
}
#endif

#endif /* __AI_USER_EVENT_H__ */
