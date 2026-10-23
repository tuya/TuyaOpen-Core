/* tuya_kconfig.h - LINUX static configuration header (hand-editable).
 *
 * Three sections:
 *   1. Feature switches: undefined by default. Enable in one of two ways
 *      (-D wins): a) global macro: -DENABLE_WIRED=1 (only the ones needed)
 *      b) uncomment the corresponding #define below
 *      Switch values must be consistent with the source files actually
 *      compiled, otherwise linking fails.
 *   2. Board macros: LINUX defaults
 *   3. Parameter defaults: all guarded by #ifndef and overridable with -D
 */
#ifndef TUYA_KCONFIG_H
#define TUYA_KCONFIG_H

/* ==================== 1. Switches (off by default, enable as needed) ==================== */
/* #ifndef ENABLE_WIRED
#define ENABLE_WIRED 1
#endif */
/* #ifndef ENABLE_BLUETOOTH
#define ENABLE_BLUETOOTH 1
#endif */
/* #ifndef ENABLE_QRCODE
#define ENABLE_QRCODE 1
#endif */
/* #ifndef ENABLE_TUYA_P2P
#define ENABLE_TUYA_P2P 1
#endif */
/* #ifndef LITTLE_END
#define LITTLE_END 1
#endif */

/* ==================== 2. Board macros (LINUX) ==================== */
#ifndef OPERATING_SYSTEM
#define OPERATING_SYSTEM 100 /* SYSTEM_LINUX */
#endif

#ifndef AUDIO_DRIVER_NAME
#define AUDIO_DRIVER_NAME "audio_driver"
#endif

/* ==================== 3. Parameter defaults ==================== */
#ifndef MAX_SECURITY_LEVEL
#define MAX_SECURITY_LEVEL 1
#endif

#ifndef SERIAL_CLI_STACK_SIZE
#define SERIAL_CLI_STACK_SIZE 3072
#endif

#ifndef TUYA_SECURITY_LEVEL
#define TUYA_SECURITY_LEVEL 1
#endif

#ifndef STACK_SIZE_TIMERQ
#define STACK_SIZE_TIMERQ 4096
#endif

#ifndef STACK_SIZE_WORK_QUEUE
#define STACK_SIZE_WORK_QUEUE 5120
#endif

#ifndef MAX_NODE_NUM_WORK_QUEUE
#define MAX_NODE_NUM_WORK_QUEUE 100
#endif

#ifndef STACK_SIZE_MSG_QUEUE
#define STACK_SIZE_MSG_QUEUE 4096
#endif

#ifndef MAX_NODE_NUM_MSG_QUEUE
#define MAX_NODE_NUM_MSG_QUEUE 100
#endif

#ifndef AI_BIZ_MAX_NUM
#define AI_BIZ_MAX_NUM 5
#endif

#ifndef AI_SESSION_MAX_NUM
#define AI_SESSION_MAX_NUM 2
#endif

#ifndef AI_HEARTBEAT_INTERVAL
#define AI_HEARTBEAT_INTERVAL 120
#endif

#ifndef AI_MAX_FRAGMENT_LENGTH
#define AI_MAX_FRAGMENT_LENGTH 8192
#endif

#ifndef AI_CLIENT_STACK_SIZE
#define AI_CLIENT_STACK_SIZE 4096
#endif

#ifndef AI_PACKET_SECURITY_LEVEL
#define AI_PACKET_SECURITY_LEVEL 4
#endif

#ifndef AI_VERSION
#define AI_VERSION 2
#endif

#ifndef AI_SUB_VERSION
#define AI_SUB_VERSION 1
#endif

#endif /* TUYA_KCONFIG_H */
