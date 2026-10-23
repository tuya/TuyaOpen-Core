/**
 * @file port_log.h
 * @brief Port logging macros (port-internal interface).
 *
 * PORT_LOG_ERR / PORT_LOG_WARN / PORT_LOG_NOTICE / PORT_LOG_DEBUG write one
 * prefixed line to stdout. The port does not call core, so it does not use
 * core's PR_* macros; the same format is used by the rest of the port (the
 * card's .log_output is also printf-based).
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __PORT_LOG_H__
#define __PORT_LOG_H__

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_LOG_ERR(fmt, ...)     printf("[port][E] " fmt "\n", ##__VA_ARGS__)
#define PORT_LOG_WARN(fmt, ...)    printf("[port][W] " fmt "\n", ##__VA_ARGS__)
#define PORT_LOG_NOTICE(fmt, ...)  printf("[port][N] " fmt "\n", ##__VA_ARGS__)
#define PORT_LOG_DEBUG(fmt, ...)   printf("[port][D] " fmt "\n", ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* __PORT_LOG_H__ */
