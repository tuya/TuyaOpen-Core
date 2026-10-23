/**
 * @file tuya_ai_input.h
 * @brief AI input sessions: per-scode ring buffers that carry uplink audio and
 * text packets into the AI business layer.
 *
 * Each scode keeps an AI_INPUT_SESSION_CTX_T with a ring buffer and a start
 * flag. Audio and text input calls append framed packets to the ring buffer
 * (tuya_ai_audio_input_s / tuya_ai_text_input_s), start/stop control the
 * session, and the deinit path releases every session context.
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

#ifndef __TUYA_AI_INPUT_H__
#define __TUYA_AI_INPUT_H__

#include "tuya_ai_types.h"

#include "tuya_cloud_types.h"
#include "tuya_iot_config.h"
#include "tuya_ai_protocol.h"
#include "tuya_ai_biz.h"

/* tuya_ai_agent.h includes this header; including it back here would create
 * a circular include and leave AI_ATTR_BASE_T/AI_INPUT_SEND_T undefined,
 * depending on expansion order. Only this function declaration is needed
 * here; its definition lives in tuya_ai_agent.h. */
CHAR_T *tuya_ai_agent_get_scode(CHAR_T *scode);

typedef enum {
    /** idle state */
    AI_INPUT_IDLE,
    /** start state */
    AI_INPUT_START_LAZY,
    /** start state */
    AI_INPUT_START,
    /** processing state */
    AI_INPUT_PROC,
    /** stopping state */
    AI_INPUT_STOPPING,
    /** stop state */
    AI_INPUT_STOP
} AI_INPUT_STATE_E;

typedef struct {
    /** video attr */
    AI_VIDEO_ATTR_BASE_T video;
    /** audio attr */
    AI_AUDIO_ATTR_BASE_T audio;
    /** image attr */
    AI_IMAGE_ATTR_BASE_T image;
    /** file attr */
    AI_FILE_ATTR_BASE_T file;
} AI_ATTR_BASE_T;

typedef struct {
    /** send packet type */
    AI_PACKET_PT type;
    /** need used in multi session*/
    BOOL_T multi_session;
    /** send channel get cb */
    AI_BIZ_SEND_GET_CB get_cb;
    /** send channel free cb */
    AI_BIZ_SEND_FREE_CB free_cb;
} AI_INPUT_SEND_T;

/**
 * @brief ai input start
 *
 * @param[in] scode solution code
 * @param[in] force force start new session(break old session)
 *
 * @return VOID
 */
VOID tuya_ai_input_start_s(CHAR_T *scode, BOOL_T force);
#define tuya_ai_input_start(force) tuya_ai_input_start_s(tuya_ai_agent_get_scode(NULL), force)

/**
 * @brief ai input stop
*
* @param[in] scode solution code
* @return VOID
 */
VOID tuya_ai_input_stop_s(CHAR_T *scode);
#define tuya_ai_input_stop() tuya_ai_input_stop_s(tuya_ai_agent_get_scode(NULL))
/**
 * @brief ai audio input
 *
 * @param[in] scode solution code
 * @param[in] timestamp audio timestamp
 * @param[in] data audio data
 * @param[in] len audio data length
 * @param[in] total_len audio total length
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_audio_input_s(CHAR_T *scode, UINT64_T timestamp, UINT64_T pts, BYTE_T *data, UINT_T len, UINT_T total_len);
#define tuya_ai_audio_input(timestamp, pts, data, len, total_len) tuya_ai_audio_input_s(tuya_ai_agent_get_scode(NULL), timestamp, pts, data, len, total_len)

/**
 * @brief ai text input
 *
 * @param[in] scode solution code
 * @param[in] data text data
 * @param[in] len text data length
 * @param[in] total_len text total length
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_text_input_s(CHAR_T *scode, BYTE_T *data, UINT_T len, UINT_T total_len);
#define tuya_ai_text_input(data, len, total_len) tuya_ai_text_input_s(tuya_ai_agent_get_scode(NULL), data, len, total_len)

/**
 * @brief Mute or unmute audio input
 *
 * @param[in] mute TRUE: mute audio input; FALSE: unmute audio input
 *
 * @return none
 */
VOID tuya_ai_input_mute_audio(BOOL_T mute);

#endif // __TUYA_AI_INPUT_H__
