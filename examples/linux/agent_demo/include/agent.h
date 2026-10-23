/**
 * @file agent.h
 * @brief AI link startup orchestration
 *
 * After MQTT connects: initialize the AI agent → register downlink printing
 * (agent_downlink) → start the trigger thread (wait for the client to become
 * ready → upload one preset utterance → use the ASR response as the final
 * criterion). 
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __AGENT_H__
#define __AGENT_H__

/* Uplink voice sample: path injected by CMake (Kconfig AGENT_DEMO_AUDIO_FILE);
 * when empty, the bundled sample in the build directory is used (copied from
 * assets/voice_16k.pcm). Consumed in two places: the startup info print in
 * tuya_main.c and the trigger upload in agent.c. */
#ifndef AGENT_DEMO_AUDIO_FILE
#define AGENT_DEMO_AUDIO_FILE ""
#endif
#define AGENT_AUDIO_SRC_PATH ((AGENT_DEMO_AUDIO_FILE)[0] ? (AGENT_DEMO_AUDIO_FILE) : "voice_16k.pcm")

/**
 * @brief Start the AI link (idempotent): initialize the agent + register downlink printing + start the trigger thread
 *
 * Called from the MQTT_CONNECTED event; repeated calls are safe (see the guard
 * semantics in the implementation).
 */
void agent_start(void);

#endif /* __AGENT_H__ */
