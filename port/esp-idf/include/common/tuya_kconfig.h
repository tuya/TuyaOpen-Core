/* tuya_kconfig.h -- ESP32 (esp-idf / FreeRTOS) configuration header (static,
 * manually editable).
 *
 * Three parts:
 *   1. Switch macros: all undefined by default. Three ways to enable them (in
 *      priority order):
 *      a) Build-system injection: under esp-idf, sdkconfig (Kconfig.projbuild ->
 *         CONFIG_*) is converted by tuyaopen-core.cmake's macro injection table
 *         into ENABLE_*=1 and injected into tuyaopen-headers (core modules);
 *         the port target receives only ENABLE_BLUETOOTH (network no longer
 *         needs port-side switch macros);
 *      b) Compiler command line / IDE global macros: -DENABLE_WIRED=1 (pass
 *         only the ones to enable);
 *      c) Uncomment the corresponding #define below.
 *      Note: switch values must be consistent with the source files actually
 *      compiled, otherwise linking fails.
 *   2. Board macros: defaults for the ESP32 (FreeRTOS) platform
 *   3. Parameter defaults: all protected by #ifndef, overridable with -D
 */
#ifndef TUYA_KCONFIG_H
#define TUYA_KCONFIG_H

/* ==================== 1. Switches (all off by default, enable as needed) ==================== */
/* Prefer sdkconfig (the tuyaopen-core menu in menuconfig); normally no need to
 * change anything here.
 * #ifndef ENABLE_WIRED
 * #define ENABLE_WIRED 1
 * #endif */
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

/* ==================== 2. Board macros (ESP32 / FreeRTOS) ==================== */
#ifndef OPERATING_SYSTEM
#define OPERATING_SYSTEM 98 /* SYSTEM_FREERTOS */
#endif

#ifndef AUDIO_DRIVER_NAME
#define AUDIO_DRIVER_NAME "audio_driver"
#endif

/* ==================== 3. Parameter defaults ==================== */
#ifndef MAX_SECURITY_LEVEL
#define MAX_SECURITY_LEVEL 1
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
