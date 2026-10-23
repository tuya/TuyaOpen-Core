/**
 * @file play_local_in.h
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

#ifndef __PLAY_LOCAL_IN_H__
#define __PLAY_LOCAL_IN_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the audio source
 *
 * @param[in] src_path audio source path (Linux: PCM file path; real devices may ignore it or use device parameters)
 * @return OPRT_OK on success; others see tuya_error_code.h
 */
OPERATE_RET play_local_in_open(const char *src_path);

/**
 * @brief Read a chunk of PCM data
 *
 * @param[out] buf output buffer
 * @param[in]  len expected number of bytes
 * @return >0 actual bytes read; 0 end of data (EOF); <0 error
 */
INT_T play_local_in_read(UINT8_T *buf, UINT_T len);

/**
 * @brief Close the audio source
 *
 * @return OPRT_OK on success
 */
OPERATE_RET play_local_in_close(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __PLAY_LOCAL_IN_H__ */
