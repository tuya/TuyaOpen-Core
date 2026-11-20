#ifndef _CORE_MQTT_CONFIG_H_
#define _CORE_MQTT_CONFIG_H_

/**************************************************/
/******* DO NOT CHANGE the following order ********/
/**************************************************/

/************ End of logging configuration ****************/

/**
 * @brief Determines the maximum number of MQTT PUBLISH messages, pending
 * acknowledgement at a time, that are supported for incoming and outgoing
 * direction of messages, separately.
 *
 * QoS 1 and 2 MQTT PUBLISHes require acknowledgement from the server before
 * they can be completed. While they are awaiting the acknowledgement, the
 * client must maintain information about their state. The value of this
 * macro sets the limit on how many simultaneous PUBLISH states an MQTT
 * context maintains, separately, for both incoming and outgoing direction of
 * PUBLISHes.
 *
 * @note The MQTT context maintains separate state records for outgoing
 * and incoming PUBLISHes, and thus, 2 * MQTT_STATE_ARRAY_MAX_COUNT amount
 * of memory is statically allocated for the state records.
 */
#define MQTT_STATE_ARRAY_MAX_COUNT (10U)

/**
 * @brief Number of milliseconds to wait for a ping response to a ping
 * request as part of the keep-alive mechanism.
 *
 * If a ping response is not received before this timeout, then
 * #MQTT_ProcessLoop will return #MQTTKeepAliveTimeout.
 */
#define MQTT_PINGRESP_TIMEOUT_MS (10000U)

/**
 * @brief CORE_MQTT_BUFFER_SIZE
 *
 * coreMQTT uses one network buffer for both RX and TX. RTC signaling
 * carries ~1.5-1.8 KB SDP offers/answers; with a 2048-byte buffer,
 * publishing the answer from the receive callback can fail with
 * MQTTSendFailed and drop the connection on the first live attempt.
 * Keep 4096 by default; applications may override it.
 */
#ifndef CORE_MQTT_BUFFER_SIZE
#define CORE_MQTT_BUFFER_SIZE (4096U)
#endif

/**
 * @brief Idle poll period for the MQTT receive loop, in milliseconds.
 *
 * mqtt_client_yield() waits for socket readability *without* holding the MQTT
 * lock, so idle wake-ups drop from 5/s (200 ms read cap) to 1/period, and a
 * publisher never waits behind an idle receive. Keepalive is still serviced
 * every period. 1000 ms keeps tuya_iot_yield()/matop timeouts responsive;
 * battery products can override it (e.g. 5000) at compile time. The P2P/DP
 * publish path does not depend on this period.
 */
#ifndef MQTT_IDLE_POLL_MS
#define MQTT_IDLE_POLL_MS (1000U)
#endif

/**
 * @brief Granularity of the idle poll, in milliseconds. 0 = wait the whole
 * MQTT_IDLE_POLL_MS in one select.
 *
 * The poll loop checks the "disconnect requested" flag between slices. A
 * non-zero slice lets stop/destroy return sooner, but every slice is a CPU
 * wake-up: 100 ms slices would wake 10 times/s and defeat the power goal.
 * Leave 0 for the lowest idle wake rate; set e.g. 100 only if the
 * application needs faster cancellation than the idle period.
 */
#ifndef MQTT_IDLE_POLL_SLICE_MS
#define MQTT_IDLE_POLL_SLICE_MS (0U)
#endif

/**
 * @brief Upper bound for a single receive inside MQTT_ProcessLoop.
 *
 * The long wait now happens in the lock-free poll; this only bounds the
 * locked read that finishes a TLS record or services keepalive, so a
 * publisher waits at most this long behind the cloud task.
 */
#ifndef MQTT_READ_TIMEOUT_MS
#define MQTT_READ_TIMEOUT_MS (200U)
#endif

/**
 * @brief Receive timeout for the ProcessLoop that follows a poll or drains
 * already-available data, in milliseconds.
 *
 * The long wait already happened in the lock-free poll (or the data is known
 * to be buffered), so this read only has to finish a record that is already
 * on the wire. Keeping it short bounds the MQTT lock hold and avoids a
 * 200 ms wake-up every idle period.
 */
#ifndef MQTT_READ_AFTER_ACTIVITY_MS
#define MQTT_READ_AFTER_ACTIVITY_MS (20U)
#endif

#endif /* ifndef CORE_MQTT_CONFIG_H_ */
