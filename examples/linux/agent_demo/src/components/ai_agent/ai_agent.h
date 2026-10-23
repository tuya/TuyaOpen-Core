/**
 * @file ai_agent.h
 * @brief AI agent module header
 *
 * This header file defines the functions for the AI agent module, which handles
 * communication with the Tuya AI cloud service, including text, file, and
 * image input, as well as cloud alerts and role switching.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __AI_AGENT_H__
#define __AI_AGENT_H__

#include "tuya_cloud_types.h"
#include "tuya_ai_output.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/


/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/
/**
@brief Initialize the AI agent module
@return OPERATE_RET Operation result
*/
OPERATE_RET ai_agent_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __AI_AGENT_H__ */
