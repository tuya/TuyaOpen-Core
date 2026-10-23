/**
 * @file tuya_ai_agent.h
 * @brief
 * @version 0.1
 * @date 2025-04-17
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

#ifndef __TUYA_AI_AGENT_H__
#define __TUYA_AI_AGENT_H__

#include "tuya_ai_types.h"

#include "tuya_cloud_types.h"
#include "tuya_ai_client.h"
#include "tuya_ai_biz.h"
#include "tuya_ai_event.h"
#include "tuya_ai_input.h"
#include "tuya_ai_output.h"
#if defined(ENABLE_JOYINSIDE) && (ENABLE_JOYINSIDE == 1)
#include "joyinside_client.h"
#include "joyinside_biz.h"
#endif

/* MCP tool callback */
typedef OPERATE_RET (*TY_AI_MCP_CB)(CHAR_T *sid, CHAR_T *eid, CONST ty_cJSON *json, VOID *user_data);

typedef BYTE_T AI_VIDEO_STREAM_E;
#define AI_VIDEO_STREAM_MAIN   0x00

typedef struct {
    UINT32_T bit_rate;          // bit rate in bps, default is 16000, set 48000 or 64000 to reduce latency in poor network conditions
    UINT32_T sample_rate;       // sample rate in Hz, default is 16000
    CONST CHAR_T *format;       // default is "mp3", set to "opus" if your player supports opus decoding
} AI_AGENT_TTS_CFG_T;

typedef struct {
    AI_PACKET_PT type;
    USHORT_T id;
    BOOL_T first_pkt;
} AI_AGENT_ID_T;
typedef struct {
    AI_PACKET_PT type;
    AI_BIZ_RECV_CB cb;
} AI_AGENT_RECV_CB_T;
typedef struct {
    CHAR_T scode[AI_SOLUTION_CODE_LEN];
    AI_AGENT_RECV_CB_T arc[AI_BIZ_MAX_NUM];
    AI_EVENT_CB event_cb;
} AI_AGENT_RECV_DATA_T;
typedef struct {
    CHAR_T scode[AI_SOLUTION_CODE_LEN];
    CHAR_T sid[AI_UUID_V4_LEN + 1];
    CHAR_T eid[AI_UUID_V4_LEN + 1];
    AI_AGENT_ID_T send[AI_BIZ_MAX_NUM];
    CHAR_T token[AI_AGENT_TOKEN_LEN];
} AI_AGENT_SESSION_T;

typedef struct {
    CHAR_T scode[AI_SOLUTION_CODE_LEN];
    AI_ATTR_BASE_T attr;
    AI_INPUT_SEND_T biz_get[AI_BIZ_MAX_NUM];
    AI_OUTPUT_CBS_T output;
    AI_AGENT_TTS_CFG_T tts_cfg;
    AI_SECURITY_CFG_T security_cfg;
    BOOL_T enable_crt_session_ext; // enable crt session external
    BOOL_T enable_internal_scode; // enable internal solution code
    BOOL_T enable_mcp; // enable mcp tools
#if defined(ENABLE_JOYINSIDE) && (ENABLE_JOYINSIDE == 1)
    JD_CHAT_CFG_S *jd_cfg; // joyinside chat config
#endif
} AI_AGENT_CFG_T;

/**
 * @brief ai agent init
 *
 * @param[in] cfg agent cfg
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_agent_init(AI_AGENT_CFG_T *cfg);

/**
 * @brief ai agent deinit
 *
 */
VOID tuya_ai_agent_deinit(VOID);

/**
 * @brief ai agent create session
 *
 * @param[in] scode solution code
 * @param[in] bizCode business code
 * @param[in] bizTag business tag
 * @param[in] attr user attribute data
 * @param[in] attr_len user attribute data length
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_agent_crt_session(CHAR_T *scode, UINT_T bizCode, UINT64_T bizTag, BYTE_T *attr, UINT_T attr_len);

/**
 * @brief ai agent event
 *
 * @param[in] scode solution code
 * @param[in] etype event type
 * @param[in] ptype packet type
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_agent_event_s(CHAR_T *scode, AI_EVENT_TYPE etype, AI_PACKET_PT ptype);
#define tuya_ai_agent_event(etype, ptype) tuya_ai_agent_event_s(tuya_ai_agent_get_scode(NULL), etype, ptype)

/**
 * @brief get ai agent solution code
 *
 * @param[in] scode solution code
 *
 * @return solution code
 */
CHAR_T* tuya_ai_agent_get_scode(CHAR_T *scode);

/**
 * @brief is internal solution code
 *
 * @return TRUE: internal solution code; FALSE: user solution code
 */
BOOL_T tuya_ai_agent_is_internal_scode(VOID);

/**
 * @brief get down attribute
 *
 * @return down attribute pointer
 */
AI_ATTR_BASE_T *tuya_ai_agent_get_down_attr(VOID);

/**
 * @brief ai agent mcp set callback
 *
 * @param[in] cb callback function
 * @param[in] user_data user data for callback
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_agent_mcp_set_cb(TY_AI_MCP_CB cb, VOID *user_data);

/**
 * @brief ai agent mcp response
 *
 * @param[in] sid session id
 * @param[in] eid event id
 * @param[in] message response message
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_agent_mcp_response(CHAR_T *sid, CHAR_T *eid, CHAR_T *message);

/**
 * @brief is ai agent ready
 *
 * @return TRUE is ready, FALSE is not ready
 */
BOOL_T tuya_ai_agent_is_ready(VOID);
#endif // __TUYA_AI_AGENT_H__