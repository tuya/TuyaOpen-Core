/**
 * @file mqtt_client_wrapper.c
 * @brief coreMQTT client wrapper: connection lifecycle, publish/subscribe and
 * the receive loop over the transport layer.
 *
 * mqtt_client_new()/mqtt_client_free() own the client object;
 * mqtt_client_init()/mqtt_client_deinit() configure and tear down the coreMQTT
 * context with its TLS transport; connect/disconnect, subscribe/unsubscribe
 * and publish forward to coreMQTT; mqtt_client_yield() drives the receive loop
 * and dispatches incoming packets to the library callback. A registry of live
 * contexts backs the time and transport helper callbacks.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */
#include "tuya_error_code.h"
#include "core_mqtt_config.h"
#include "core_mqtt.h"

#include "mqtt_client_interface.h"

#include "tuya_tls.h"
#include "tuya_transporter.h"
#include "core_log.h"
#include "core_system.h"
#include "core_memory.h"
#include "core_mutex.h"

#define log_debug PR_DEBUG
#define log_error PR_ERR

#define MQTT_WRAPPER_MAX_PENDING 10

/* coreMQTT is not thread-safe: the cloud task runs MQTT_ProcessLoop while
 * P2P/DP/OTA callbacks publish from other tasks. Serialize every coreMQTT
 * operation. The core mutex is recursive, so a publish issued from inside the
 * receive callback (same thread) still works.
 *
 * The connect/disconnect path runs under a separate conn lock and keeps the
 * TCP/TLS handshake outside this lock (see mqtt_client_connect): holding it
 * across the handshake was the main cause of multi-second P2P publish waits.
 * Publishers now fail fast while the link is CONNECTING.
 *
 * mqtt_client_yield() waits for socket readability *without* holding the MQTT
 * lock (option B): an idle link does not block a publisher, and an IoT device
 * wakes once per MQTT_IDLE_POLL_MS instead of every 200 ms. mbedtls-buffered
 * plaintext is detected through TUYA_TRANSPORTER_TLS_PENDING before waiting.
 * After unlocking, it sleeps MQTT_YIELD_GAP_MS so a burst of back-to-back
 * packets cannot starve a publisher between iterations. */
static MUTEX_HANDLE s_mqtt_lock = NULL;

/* Give a publisher blocked on s_mqtt_lock one scheduling window after the
 * receiver releases it. A port with a coarse scheduler tick may round a 1 ms
 * delay up to the tick (for example 10 ms), still far above what the cloud
 * link needs. */
#define MQTT_YIELD_GAP_MS 1

static inline void __mqtt_lock(void)
{
    if (s_mqtt_lock != NULL) {
        core_mutex_lock(s_mqtt_lock);
    }
}

static inline void __mqtt_unlock(void)
{
    if (s_mqtt_lock != NULL) {
        core_mutex_unlock(s_mqtt_lock);
    }
}

/* Serializes connect/disconnect (including the TCP/TLS handshake and close)
 * between the cloud task and an application thread calling
 * tuya_mqtt_start/stop. Lock order is always conn_lock -> mqtt_lock; publishers
 * take only mqtt_lock and never wait here. */
static MUTEX_HANDLE s_conn_lock = NULL;

static inline void __conn_lock(void)
{
    if (s_conn_lock != NULL) {
        core_mutex_lock(s_conn_lock);
    }
}

static inline void __conn_unlock(void)
{
    if (s_conn_lock != NULL) {
        core_mutex_unlock(s_conn_lock);
    }
}

typedef struct {
    mqtt_client_config_t config;
    MQTTContext_t mqclient;
    tuya_transporter_t network;
    /* Link state for fail-fast publishers: coreMQTT may only be touched while
     * CONNECTED, and the transport connect/handshake runs outside the MQTT
     * lock. Read with atomics from publisher threads. */
    volatile int link_state;
    /* Lifetime references: 1 for the owner of the client object, +1 for every
     * public call in flight. mqtt_client_free() drops the owner's reference
     * and waits for the rest, closing the free-after-lock window where a
     * publisher blocked on s_mqtt_lock would touch freed memory. */
    volatile int refs;
    /* Set without taking the connection lock by disconnect/deinit/free so the
     * lock-free idle poll stops within one slice instead of a full period. */
    volatile int poll_abort;
    /* Any packet handled by the coreMQTT callback since the last check: skip
     * the long idle poll once, so back-to-back packets are drained promptly. */
    volatile int rx_activity;
    /* One-shot receive timeout for the next network_read (0 = default cap).
     * Written and consumed on the cloud task only; set when draining. */
    int read_timeout_override_ms;
    uint8_t mqttbuffer[CORE_MQTT_BUFFER_SIZE];
    /* Since coreMQTT v2.x, QoS1/QoS2 require state records (initialized by
     * MQTT_InitStatefulQoS) */
    MQTTPubAckInfo_t outgoing_records[MQTT_WRAPPER_MAX_PENDING];
    MQTTPubAckInfo_t incoming_records[MQTT_WRAPPER_MAX_PENDING];
} mqtt_client_context_t;

#define MQTT_LINK_DISCONNECTED 0
#define MQTT_LINK_CONNECTING   1
#define MQTT_LINK_CONNECTED    2
#define MQTT_LINK_DEAD         3 /* deinit/free: no further use */

static inline int __link_state_get(const mqtt_client_context_t *context)
{
    return __atomic_load_n(&context->link_state, __ATOMIC_ACQUIRE);
}

static inline void __link_state_set(mqtt_client_context_t *context, int state)
{
    __atomic_store_n(&context->link_state, state, __ATOMIC_RELEASE);
}

/* Take a lifetime reference. Returns false once the owner has freed the
 * object (refs reached 0), in which case the caller must not touch it. */
static inline bool __client_ref(mqtt_client_context_t *context)
{
    int refs = __atomic_load_n(&context->refs, __ATOMIC_ACQUIRE);

    while (refs > 0) {
        if (__atomic_compare_exchange_n(&context->refs, &refs, refs + 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            return true;
        }
    }
    return false;
}

static inline void __client_unref(mqtt_client_context_t *context)
{
    (void)__atomic_sub_fetch(&context->refs, 1, __ATOMIC_ACQ_REL);
}

static inline int __poll_abort_get(const mqtt_client_context_t *context)
{
    return __atomic_load_n(&context->poll_abort, __ATOMIC_ACQUIRE);
}

static inline void __poll_abort_set(mqtt_client_context_t *context, int abort)
{
    __atomic_store_n(&context->poll_abort, abort, __ATOMIC_RELEASE);
}

/* The MQTTContext_t of coreMQTT v5.0.2 has no userData field: the wrapper
 * keeps its own registry and the event callback looks up the wrapper context
 * by pContext pointer */
#define MQTT_WRAPPER_MAX_CLIENTS 4
static mqtt_client_context_t *s_ctx_registry[MQTT_WRAPPER_MAX_CLIENTS];

static void core_mqtt_registry_add(mqtt_client_context_t *context)
{
    for (int i = 0; i < MQTT_WRAPPER_MAX_CLIENTS; i++) {
        if (s_ctx_registry[i] == NULL) {
            s_ctx_registry[i] = context;
            return;
        }
    }
    log_error("mqtt wrapper registry full");
}

static void core_mqtt_registry_remove(mqtt_client_context_t *context)
{
    for (int i = 0; i < MQTT_WRAPPER_MAX_CLIENTS; i++) {
        if (s_ctx_registry[i] == context) {
            s_ctx_registry[i] = NULL;
            return;
        }
    }
}

static mqtt_client_context_t *core_mqtt_registry_find(const MQTTContext_t *mqclient)
{
    for (int i = 0; i < MQTT_WRAPPER_MAX_CLIENTS; i++) {
        if (s_ctx_registry[i] != NULL && &s_ctx_registry[i]->mqclient == mqclient) {
            return s_ctx_registry[i];
        }
    }
    return NULL;
}

static void core_mqtt_library_callback(struct MQTTContext *pContext, struct MQTTPacketInfo *pPacketInfo,
                                       struct MQTTDeserializedInfo *pDeserializedInfo)
{
    mqtt_client_context_t *context = core_mqtt_registry_find(pContext);
    if (context == NULL) {
        log_error("mqtt callback: context not found");
        return;
    }

    /* A packet was consumed: the next yield drains without a long poll. */
    __atomic_store_n(&context->rx_activity, 1, __ATOMIC_RELEASE);

    uint16_t msgid = pDeserializedInfo->packetIdentifier;

    /* Handle incoming publish. The lower 4 bits of the publish packet
     * type is used for the dup, QoS, and retain flags. Hence masking
     * out the lower bits to check if the packet is publish. */
    if ((pPacketInfo->type & 0xF0U) == MQTT_PACKET_TYPE_PUBLISH) {
        if (context->config.on_message == NULL) {
            return;
        }

        char *topic = core_malloc(pDeserializedInfo->pPublishInfo->topicNameLength + 1);
        if (topic == NULL) {
            return;
        }
        memcpy(topic, pDeserializedInfo->pPublishInfo->pTopicName, pDeserializedInfo->pPublishInfo->topicNameLength);
        topic[pDeserializedInfo->pPublishInfo->topicNameLength] = '\0';

        context->config.on_message(context, msgid,
                                   &(const mqtt_client_message_t){
                                       .topic = topic,
                                       .payload = pDeserializedInfo->pPublishInfo->pPayload,
                                       .length = pDeserializedInfo->pPublishInfo->payloadLength,
                                       .qos = pDeserializedInfo->pPublishInfo->qos,
                                   },
                                   context->config.userdata);
        core_free(topic);

    } else {
        switch (pPacketInfo->type) {
        case MQTT_PACKET_TYPE_SUBACK:
            log_debug("MQTT_PACKET_TYPE_SUBACK id:%d", msgid);
            if (context->config.on_subscribed) {
                context->config.on_subscribed(context, msgid, context->config.userdata);
            }
            break;

        case MQTT_PACKET_TYPE_UNSUBACK:
            log_debug("MQTT_PACKET_TYPE_UNSUBACK id:%d", msgid);
            if (context->config.on_unsubscribed) {
                context->config.on_unsubscribed(context, msgid, context->config.userdata);
            }
            break;

        case MQTT_PACKET_TYPE_PUBACK:
            log_debug("MQTT_PACKET_TYPE_PUBACK id:%d", msgid);
            if (context->config.on_published) {
                context->config.on_published(context, msgid, context->config.userdata);
            }
            break;

        default:
            log_debug("type:0x%02x, id:%d", pPacketInfo->type, msgid);
        }
    }
}

void *mqtt_client_new(void)
{
    mqtt_client_context_t *context = core_malloc(sizeof(mqtt_client_context_t));

    if (context != NULL) {
        memset(context, 0, sizeof(mqtt_client_context_t));
        context->refs = 1; /* owner reference, dropped by mqtt_client_free() */
    }
    return context;
}

void mqtt_client_free(void *client)
{
    mqtt_client_context_t *context = (mqtt_client_context_t *)client;

    if (context == NULL) {
        return;
    }

    /* Stop the lock-free idle poll before taking the connection lock: yield
     * holds it while polling and checks this flag between slices. */
    __poll_abort_set(context, 1);

    __conn_lock();
    __mqtt_lock();
    __link_state_set(context, MQTT_LINK_DEAD);
    __mqtt_unlock();
    __conn_unlock();

    /* Drop the owner reference, then wait for calls that were already in
     * flight. A caller that entered before DEAD holds a reference and will
     * re-check the state under the MQTT lock, so it never touches the
     * transport. */
    __client_unref(context);
    uint64_t wait_start = core_system_get_millisecond();
    while (__atomic_load_n(&context->refs, __ATOMIC_ACQUIRE) > 0) {
        if (core_system_get_millisecond() - wait_start >= 2000ULL) {
            /* An abnormal caller still holds a pointer: leaking this object is
             * strictly safer than freeing memory that caller will touch. */
            PR_ERR("mqtt client free: %d reference(s) still held, leaking context",
                   __atomic_load_n(&context->refs, __ATOMIC_ACQUIRE));
            return;
        }
        core_system_sleep(1);
    }
    core_free(context);
}

static int network_write(NetworkContext_t *pNetwork, const unsigned char *pMsg, size_t len)
{
    /* pNetworkContext points at the wrapper context (opaque to coreMQTT), so
     * the helpers can also see the one-shot read timeout. */
    mqtt_client_context_t *context = (mqtt_client_context_t *)pNetwork;
    tuya_transporter_t transporter = context->network;
    uint64_t t0 = core_system_get_millisecond();
    int result = tuya_transporter_write(transporter, (uint8_t *)pMsg, len, 0);

    /* Diagnosis: a TLS write that blocks is what makes a publish hold the
     * serialization lock for seconds (or, unlocked, blocks the caller). */
    if (core_system_get_millisecond() - t0 >= 500ULL) {
        PR_WARN("mqtt write took %ums len=%u ret=%d", (unsigned)(core_system_get_millisecond() - t0), (unsigned)len,
                result);
    }
    return result;
}

static int network_read(NetworkContext_t *pNetwork, unsigned char *pMsg, size_t len)
{
    mqtt_client_context_t *context = (mqtt_client_context_t *)pNetwork;
    tuya_transporter_t transporter = context->network;

    tuya_tls_config_t *tls_config = NULL;

    tuya_transporter_ctrl(transporter, TUYA_TRANSPORTER_GET_TLS_CONFIG, &tls_config);

    int timeout = tls_config ? tls_config->timeout : 5000;

    /* The long wait now happens in the lock-free poll of mqtt_client_yield;
     * this cap only bounds the locked read that finishes a TLS record or
     * services keepalive, so a publisher is not queued behind it for long.
     * A drain (after handled packets / TLS pending bytes) uses an even shorter
     * one-shot timeout. */
    int override = context->read_timeout_override_ms;

    context->read_timeout_override_ms = 0;
    if (override > 0) {
        timeout = override;
    } else if (timeout > (int)MQTT_READ_TIMEOUT_MS) {
        timeout = (int)MQTT_READ_TIMEOUT_MS;
    }

    uint64_t t0 = core_system_get_millisecond();
    int result = tuya_transporter_read(transporter, (uint8_t *)pMsg, len, timeout);

    /* The cloud task holds the MQTT lock across this read. If the transport
     * ignores the 200 ms request, this probe shows the real hold time. */
    if (core_system_get_millisecond() - t0 >= 500ULL) {
        PR_WARN("mqtt read took %ums (requested %d) result=%d", (unsigned)(core_system_get_millisecond() - t0), timeout,
                result);
    }

    if (result == OPRT_RESOURCE_NOT_READY) {
        return 0;
    }

    /* Defensive: if an mbedtls sentinel ever reaches this layer (a missed
     * mapping in a transporter), treat it as "retry later" rather than a
     * fatal read error. WANT_READ=-0x6900, WANT_WRITE=-0x6880. */
    if (result == -0x6900 || result == -0x6880) {
        PR_DEBUG("mqtt read wants retry: %d", result);
        return 0;
    }

    /* mbedtls: the peer sent close_notify (normal TLS close, typically the
     * broker kicking us: keepalive, policy or a duplicate client id). */
    if (result == -0x7880) {
        PR_DEBUG("mqtt peer close_notify");
        return result;
    }

    if (result <= 0) {
        log_error("network_read fail: result=%d, len=%zu, timeout=%d", result, len, timeout);
    }

    return result;
}
static uint32_t __mqtt_client_get_current_time(void)
{
    return (uint32_t)core_system_get_millisecond();
}
mqtt_client_status_t mqtt_client_init(void *client, const mqtt_client_config_t *config)
{
    mqtt_client_context_t *context = (mqtt_client_context_t *)client;
    MQTTStatus_t mqtt_status;

    /* Clean memory */
    memset(context, 0, sizeof(mqtt_client_context_t));
    /* mqtt_client_new() already set the owner reference; re-set it because of
     * the memset above (init is called once, by the owning thread). */
    context->refs = 1;
    if (s_mqtt_lock == NULL) {
        if (core_mutex_create_init(&s_mqtt_lock) != OPRT_OK) {
            log_error("mqtt lock create failed");
            return MQTT_STATUS_NETWORK_INIT_FAILED;
        }
    }
    if (s_conn_lock == NULL) {
        if (core_mutex_create_init(&s_conn_lock) != OPRT_OK) {
            log_error("mqtt conn lock create failed");
            return MQTT_STATUS_NETWORK_INIT_FAILED;
        }
    }
    __link_state_set(context, MQTT_LINK_DISCONNECTED);
    core_mqtt_registry_add(context);

    /* Setting data */
    TUYA_TRANSPORT_TYPE_E transport_type = (config->cacert == NULL) ? TRANSPORT_TYPE_TCP : TRANSPORT_TYPE_TLS;
    context->config = *config;
    context->network = tuya_transporter_create(transport_type, NULL);
    if (NULL == context->network) {
        return MQTT_STATUS_NETWORK_INIT_FAILED;
    }
    if (transport_type == TRANSPORT_TYPE_TLS) {
        tuya_tls_config_t tls_config = {
            .ca_cert = (char *)context->config.cacert,
            .ca_cert_size = context->config.cacert_len,
            .hostname = (char *)context->config.host,
            .port = context->config.port,
            .timeout = context->config.timeout_ms,
            .mode = TUYA_TLS_SERVER_CERT_MODE,
            .verify = true,
        };

        int ret = tuya_transporter_ctrl(context->network, TUYA_TRANSPORTER_SET_TLS_CONFIG, &tls_config);
        if (OPRT_OK != ret) {
            log_error("network_tls_init fail:%d", ret);
            return MQTT_STATUS_NETWORK_INIT_FAILED;
        }
    }

    /* Fill in TransportInterface send and receive function pointers.
     * For this demo, TCP sockets are used to send and receive data
     * from network. Network context is SSL context for OpenSSL.*/
    TransportInterface_t transport = {0};
    /* Opaque to coreMQTT; network_read/write cast it back to the wrapper
     * context so they can see the one-shot read timeout. */
    transport.pNetworkContext = (NetworkContext_t *)context;
    transport.send = (TransportSend_t)network_write;
    transport.recv = (TransportRecv_t)network_read;

    /* Fill the values for network buffer. */
    MQTTFixedBuffer_t network_buffer;
    network_buffer.size = CORE_MQTT_BUFFER_SIZE;
    network_buffer.pBuffer = context->mqttbuffer;

    /* Initialize MQTT library. */
    mqtt_status = MQTT_Init(&context->mqclient, &transport, (MQTTGetCurrentTimeFunc_t)__mqtt_client_get_current_time,
                            core_mqtt_library_callback, &network_buffer);

    if (mqtt_status != MQTTSuccess) {
        log_error("MQTT init failed: Status = %s.", MQTT_Status_strerror(mqtt_status));
        tuya_transporter_close(context->network);
        tuya_transporter_destroy(context->network);
        return OPRT_COM_ERROR;
    }

    /* Since coreMQTT v2.x, QoS1/QoS2 subscribe/publish require the in-flight
     * record tables to be initialized */
    mqtt_status = MQTT_InitStatefulQoS(&context->mqclient,
                                       context->outgoing_records, MQTT_WRAPPER_MAX_PENDING,
                                       context->incoming_records, MQTT_WRAPPER_MAX_PENDING);
    if (mqtt_status != MQTTSuccess) {
        log_error("MQTT_InitStatefulQoS failed: Status = %s.", MQTT_Status_strerror(mqtt_status));
        return OPRT_COM_ERROR;
    }

    return MQTT_STATUS_SUCCESS;
}

mqtt_client_status_t mqtt_client_deinit(void *client)
{
    mqtt_client_context_t *context = (mqtt_client_context_t *)client;

    if (context == NULL || !__client_ref(context)) {
        return MQTT_STATUS_INVALID_PARAM;
    }
    /* Same exclusion as disconnect: no publisher may touch the transport
     * after close/destroy. DEAD also closes the window where a connect that
     * already passed its checks would handshake on the destroyed transport. */
    __poll_abort_set(context, 1);
    __conn_lock();
    __mqtt_lock();
    __link_state_set(context, MQTT_LINK_DEAD);
    __mqtt_unlock();

    core_mqtt_registry_remove(context);
    tuya_transporter_close(context->network);
    tuya_transporter_destroy(context->network);
    context->network = NULL; /* reject a second close/destroy */
    __conn_unlock();
    __client_unref(context);
    return MQTT_STATUS_SUCCESS;
}

mqtt_client_status_t mqtt_client_connect(void *client)
{
    mqtt_client_context_t *context = (mqtt_client_context_t *)client;

    if (context == NULL || !__client_ref(context)) {
        return MQTT_STATUS_INVALID_PARAM;
    }
    MQTTStatus_t mqtt_status;
    uint64_t t0;

    __conn_lock();
    if (__link_state_get(context) == MQTT_LINK_DEAD) {
        __conn_unlock();
        __client_unref(context);
        return MQTT_STATUS_CONNECT_FAILED;
    }
    if (__link_state_get(context) == MQTT_LINK_CONNECTED) {
        __conn_unlock();
        __client_unref(context);
        return MQTT_STATUS_SUCCESS;
    }
    if (__link_state_get(context) == MQTT_LINK_CONNECTING) {
        /* Re-entered from a callback while this thread is mid-handshake (the
         * TLS certificate exception callback may call start/stop). Refuse a
         * second handshake instead of corrupting the transport. */
        log_error("mqtt connect re-entered while connecting");
        __conn_unlock();
        __client_unref(context);
        return MQTT_STATUS_CONNECT_FAILED;
    }
    /* A previous disconnect left this set to cut an idle poll short; a new
     * link must be able to wait again. */
    __poll_abort_set(context, 0);
    __link_state_set(context, MQTT_LINK_CONNECTING);

    /* TCP connect + TLS handshake outside the MQTT lock: on a congested link
     * this used to hold the lock for seconds and stalled P2P signaling. A
     * publisher now sees CONNECTING and fails fast instead of queueing. */
    t0 = core_system_get_millisecond();
    int ret = tuya_transporter_connect(context->network, context->config.host, context->config.port,
                                       context->config.timeout_ms);
    if (core_system_get_millisecond() - t0 >= 500ULL) {
        PR_WARN("mqtt transport connect took %ums", (unsigned)(core_system_get_millisecond() - t0));
    }
    if (OPRT_OK != ret) {
        tuya_transporter_close(context->network);
        __link_state_set(context, MQTT_LINK_DISCONNECTED);
        __conn_unlock();
        __client_unref(context);
        return MQTT_STATUS_NETWORK_CONNECT_FAILED;
    }

    bool pSessionPresent = false;

    __mqtt_lock();
    /* Do not let a stale drain timeout shorten the CONNACK wait. */
    context->read_timeout_override_ms = 0;
    t0 = core_system_get_millisecond();
    /* Send MQTT CONNECT packet to broker. */
    mqtt_status = MQTT_Connect(&context->mqclient,
                               &(const MQTTConnectInfo_t){.cleanSession = true,
                                                          .keepAliveSeconds = context->config.keepalive,
                                                          .pClientIdentifier = context->config.clientid,
                                                          .clientIdentifierLength = strlen(context->config.clientid),
                                                          .pUserName = context->config.username,
                                                          .userNameLength = strlen(context->config.username),
                                                          .pPassword = context->config.password,
                                                          .passwordLength = strlen(context->config.password)},
                               NULL, context->config.timeout_ms, &pSessionPresent);
    if (core_system_get_millisecond() - t0 >= 500ULL) {
        PR_WARN("mqtt CONNACK took %ums", (unsigned)(core_system_get_millisecond() - t0));
    }
    if (MQTTSuccess != mqtt_status) {
        log_error("mqtt connect err: %s(%d)", MQTT_Status_strerror(mqtt_status), mqtt_status);
        __mqtt_unlock();
        tuya_transporter_close(context->network);
        __link_state_set(context, MQTT_LINK_DISCONNECTED);
        __conn_unlock();
        /* coreMQTT v5.0.2 reports all CONNACK rejections (including reason
         * code 5, not authorized) as ServerRefused */
        __client_unref(context);
        if (MQTTServerRefused == mqtt_status) {
            return MQTT_STATUS_NOT_AUTHORIZED;
        }
        return MQTT_STATUS_CONNECT_FAILED;
    }

    /* CONNECTED before on_connected: its subscribe/publish calls check the
     * state and must not fail fast. Callbacks must not re-enter
     * connect/disconnect (they would run while conn_lock is held). */
    __link_state_set(context, MQTT_LINK_CONNECTED);
    if (context->config.on_connected) {
        context->config.on_connected(context, context->config.userdata);
    }
    __mqtt_unlock();
    __conn_unlock();
    __client_unref(context);

    return MQTT_STATUS_SUCCESS;
}

static mqtt_client_status_t __mqtt_client_disconnect(mqtt_client_context_t *context, bool send_disc_pkt)
{
    if (context == NULL || !__client_ref(context)) {
        return MQTT_STATUS_INVALID_PARAM;
    }
    MQTTStatus_t mqtt_status;

    /* Cut a lock-free idle poll short before waiting for the connection lock:
     * yield checks this flag between poll slices. */
    __poll_abort_set(context, 1);

    uint64_t t0 = core_system_get_millisecond();

    __conn_lock();
    int prev_state = __link_state_get(context);
    if (prev_state == MQTT_LINK_DEAD || prev_state == MQTT_LINK_DISCONNECTED) {
        __conn_unlock();
        __client_unref(context);
        return MQTT_STATUS_SUCCESS;
    }
    /* Only a same-thread re-entry (callback during the handshake) can observe
     * CONNECTING here: an external caller waits on conn_lock. Closing the
     * transport mid-handshake would corrupt it, so ignore the request; the
     * connect path will settle the state. */
    if (prev_state == MQTT_LINK_CONNECTING) {
        log_error("mqtt disconnect during connect ignored (re-entry)");
        __conn_unlock();
        __client_unref(context);
        return MQTT_STATUS_SUCCESS;
    }

    /* Sending DISCONNECT is a courtesy, not a requirement. Skip it when the
     * caller already detected a broken transport, or when the socket is not
     * writable right now (timeout_ms must be >= 1: core_net_select(0) waits
     * forever). */
    bool send_disconnect = false;
    if (send_disc_pkt && (prev_state == MQTT_LINK_CONNECTED) && (context->network != NULL)) {
        int writable = tuya_transporter_poll_write(context->network, 1);
        send_disconnect = (writable > 0);
        if (!send_disconnect) {
            PR_DEBUG("mqtt disconnect: socket not writable (%d), skip DISCONNECT", writable);
        }
    }

    __mqtt_lock();
    /* State first: a publisher that already holds the lock after us must see
     * DISCONNECTED and fail fast. */
    __link_state_set(context, MQTT_LINK_DISCONNECTED);
    if (send_disconnect) {
        mqtt_status = MQTT_Disconnect(&context->mqclient);
        if (MQTTSuccess != mqtt_status) {
            PR_DEBUG("mqtt disconnect pkt skipped/failed: %s(%d)", MQTT_Status_strerror(mqtt_status), mqtt_status);
        }
    }
    __mqtt_unlock();

    /* Close outside the MQTT lock: the TLS close can block. */
    tuya_transporter_close(context->network);

    if (context->config.on_disconnected) {
        context->config.on_disconnected(context, context->config.userdata);
    }
    __conn_unlock();

    if (core_system_get_millisecond() - t0 >= 500ULL) {
        PR_WARN("mqtt disconnect took %ums", (unsigned)(core_system_get_millisecond() - t0));
    }
    __client_unref(context);

    return MQTT_STATUS_SUCCESS;
}

mqtt_client_status_t mqtt_client_disconnect(void *client)
{
    return __mqtt_client_disconnect((mqtt_client_context_t *)client, true);
}

uint16_t mqtt_client_subscribe(void *client, const char *topic, uint8_t qos)
{
    mqtt_client_context_t *context = (mqtt_client_context_t *)client;

    if (context == NULL || !__client_ref(context)) {
        return 0;
    }
    MQTTStatus_t mqtt_status;

    /* Fail fast while disconnected/reconnecting: the MQTT lock may be held by
     * MQTT_Connect's CONNACK wait. No packet id is consumed on failure. */
    if (__link_state_get(context) != MQTT_LINK_CONNECTED) {
        __client_unref(context);
        return 0;
    }
    __mqtt_lock();
    if (__link_state_get(context) != MQTT_LINK_CONNECTED) {
        __mqtt_unlock();
        __client_unref(context);
        return 0;
    }
    uint16_t msgid = MQTT_GetPacketId(&context->mqclient);

    mqtt_status = MQTT_Subscribe(
        &context->mqclient,
        &(const MQTTSubscribeInfo_t){.qos = qos, .pTopicFilter = topic, .topicFilterLength = strlen(topic)}, 1, msgid);

    if (mqtt_status != MQTTSuccess) {
        log_error("Failed to send SUBSCRIBE packet to broker with error = %s.", MQTT_Status_strerror(mqtt_status));
        __mqtt_unlock();
        __client_unref(context);
        return 0;
    }
    __mqtt_unlock();
    __client_unref(context);

    return msgid;
}

uint16_t mqtt_client_unsubscribe(void *client, const char *topic, uint8_t qos)
{
    mqtt_client_context_t *context = (mqtt_client_context_t *)client;

    if (context == NULL || !__client_ref(context)) {
        return 0;
    }
    MQTTStatus_t mqtt_status;

    if (__link_state_get(context) != MQTT_LINK_CONNECTED) {
        __client_unref(context);
        return 0;
    }
    __mqtt_lock();
    if (__link_state_get(context) != MQTT_LINK_CONNECTED) {
        __mqtt_unlock();
        __client_unref(context);
        return 0;
    }
    uint16_t msgid = MQTT_GetPacketId(&context->mqclient);

    mqtt_status = MQTT_Unsubscribe(
        &context->mqclient,
        &(const MQTTSubscribeInfo_t){.qos = qos, .pTopicFilter = topic, .topicFilterLength = strlen(topic)}, 1, msgid);

    if (mqtt_status != MQTTSuccess) {
        log_error("Failed to send UNSUBSCRIBE packet to broker with error = %s.", MQTT_Status_strerror(mqtt_status));
        __mqtt_unlock();
        __client_unref(context);
        return 0;
    }
    __mqtt_unlock();
    __client_unref(context);

    return msgid;
}

uint16_t mqtt_client_publish(void *client, const char *topic, const uint8_t *payload, size_t length, uint8_t qos)
{
    mqtt_client_context_t *context = (mqtt_client_context_t *)client;

    if (context == NULL || !__client_ref(context)) {
        return 0;
    }
    MQTTStatus_t mqtt_status;
    uint64_t t0;
    uint64_t t_lock;

    /* Fail fast while disconnected/reconnecting: the MQTT lock may be held by
     * MQTT_Connect's CONNACK wait. No packet id is consumed on failure. */
    if (__link_state_get(context) != MQTT_LINK_CONNECTED) {
        __client_unref(context);
        return 0;
    }
    t0 = core_system_get_millisecond();
    __mqtt_lock();
    if (__link_state_get(context) != MQTT_LINK_CONNECTED) {
        __mqtt_unlock();
        __client_unref(context);
        return 0;
    }
    t_lock = core_system_get_millisecond();
    uint16_t msgid = MQTT_GetPacketId(&context->mqclient);

    mqtt_status = MQTT_Publish(&context->mqclient,
                               &(const MQTTPublishInfo_t){.qos = qos,
                                                          .pTopicName = topic,
                                                          .topicNameLength = strlen(topic),
                                                          .pPayload = payload,
                                                          .payloadLength = length},
                               msgid);

    __mqtt_unlock();
    /* Diagnosis: separate the lock wait from the send so a slow publish can
     * be attributed without a debugger. */
    if (core_system_get_millisecond() - t0 >= 500ULL) {
        PR_WARN("mqtt publish took %ums (lock wait %ums) len=%u ret=%d",
                (unsigned)(core_system_get_millisecond() - t0), (unsigned)(t_lock - t0), (unsigned)length,
                (int)mqtt_status);
    }
    __client_unref(context);
    if (MQTTSuccess != mqtt_status) {
        return 0;
    }
    return msgid;
}

mqtt_client_status_t mqtt_client_yield(void *client)
{
    mqtt_client_context_t *context = (mqtt_client_context_t *)client;

    if (context == NULL || !__client_ref(context)) {
        return MQTT_STATUS_INVALID_PARAM;
    }
    MQTTStatus_t mqtt_status;
    uint64_t t0;

    /* No coreMQTT call outside CONNECTED: the transport may be handshaking on
     * the connect path (conn lock), which used to make this wait seconds. */
    if (__link_state_get(context) != MQTT_LINK_CONNECTED) {
        __client_unref(context);
        return MQTT_STATUS_SUCCESS;
    }

    /* mbedtls may hold already-decrypted bytes that a socket poll cannot see,
     * and the coreMQTT callback may have consumed a packet while more are
     * buffered. In both cases skip the long wait and drain them first. The
     * query runs under the MQTT lock because only that lock serializes SSL
     * state against a concurrent publish. */
    bool skip_poll = false;

    __mqtt_lock();
    if (__link_state_get(context) != MQTT_LINK_CONNECTED) {
        __mqtt_unlock();
        __client_unref(context);
        return MQTT_STATUS_SUCCESS;
    }
    skip_poll = __atomic_exchange_n(&context->rx_activity, 0, __ATOMIC_ACQ_REL) != 0;
    if (context->network != NULL) {
        int pending = 0;

        if (tuya_transporter_ctrl(context->network, TUYA_TRANSPORTER_TLS_PENDING, &pending) == OPRT_OK && pending > 0) {
            skip_poll = true;
        }
    }
    __mqtt_unlock();

    if (!skip_poll) {
        /* Wait for socket readability without holding s_mqtt_lock: a publisher
         * is never queued behind an idle receive, and an IoT device wakes once
         * per MQTT_IDLE_POLL_MS instead of every 200 ms. The connection lock
         * keeps the transport alive under poll_read. With the default
         * MQTT_IDLE_POLL_SLICE_MS=0 this is a single select per period, so a
         * stop can wait up to one period (set a slice to bound it tighter at
         * the cost of extra wake-ups). */
        int remaining = (MQTT_IDLE_POLL_MS > 0U) ? (int)MQTT_IDLE_POLL_MS : 1000;
        int ready = OPRT_RESOURCE_NOT_READY;
        bool polled = false;

        __conn_lock();
        if (__link_state_get(context) == MQTT_LINK_CONNECTED && context->network != NULL) {
            polled = true;
            ready = 0;
            while (remaining > 0) {
                if (__poll_abort_get(context)) {
                    ready = OPRT_RESOURCE_NOT_READY;
                    break;
                }
                int slice_max = (MQTT_IDLE_POLL_SLICE_MS > 0U) ? (int)MQTT_IDLE_POLL_SLICE_MS : remaining;
                int slice = (remaining > slice_max) ? slice_max : remaining;

                ready = tuya_transporter_poll_read(context->network, slice);
                if (ready != 0) {
                    break;
                }
                remaining -= slice;
            }
        }
        __conn_unlock();

        if (__poll_abort_get(context)) {
            /* A disconnect is waiting for the connection lock; it will set the
             * state and close the transport. */
            __client_unref(context);
            return MQTT_STATUS_SUCCESS;
        }
        if (!polled) {
            /* The link changed under us (connect failure / deinit). Nothing to
             * do until the next loop iteration. */
            __client_unref(context);
            return MQTT_STATUS_SUCCESS;
        }
        if (ready < 0) {
            log_error("mqtt idle poll failed: %d", ready);
            (void)mqtt_client_disconnect(context);
            __client_unref(context);
            return MQTT_STATUS_NETWORK_TIMEOUT;
        }
        /* ready == 0 (idle timeout) still needs one ProcessLoop below so
         * keepalive can send PINGREQ; ready > 0 means data is waiting. */
    }

    /* The ProcessLoop of coreMQTT v5.0.2 has no timeout parameter: receive
     * timeout is controlled by transport recv */
    t0 = core_system_get_millisecond();
    __mqtt_lock();
    if (__link_state_get(context) != MQTT_LINK_CONNECTED) {
        __mqtt_unlock();
        __client_unref(context);
        return MQTT_STATUS_SUCCESS;
    }
    /* Use a short read for the ProcessLoop that follows the poll or drains
     * buffered data: the long wait (if any) already happened in the poll, so
     * this read only finishes a record already on the wire. It also keeps the
     * MQTT lock hold short enough that a publisher never waits 200 ms behind
     * an idle keepalive cycle. */
    context->read_timeout_override_ms = (int)MQTT_READ_AFTER_ACTIVITY_MS;
    mqtt_status = MQTT_ProcessLoop(&context->mqclient);
    __mqtt_unlock();
    if (core_system_get_millisecond() - t0 >= 500ULL) {
        PR_WARN("mqtt processloop took %ums", (unsigned)(core_system_get_millisecond() - t0));
    }
    /* A partial packet at the end of a read returns MQTTNeedMoreBytes; that is
     * not a failure. (MQTTNoDataAvailable is already mapped to success by
     * receiveSingleIteration; kept here for clarity/older cores.) */
    if (mqtt_status != MQTTSuccess && mqtt_status != MQTTNoDataAvailable && mqtt_status != MQTTNeedMoreBytes) {
        log_error("MQTT_ProcessLoop returned with status = %s.", MQTT_Status_strerror(mqtt_status));
        (void)__mqtt_client_disconnect(context, false);
        __client_unref(context);
        return MQTT_STATUS_NETWORK_TIMEOUT;
    }

    /* Fairness for publishers during a burst of back-to-back packets: after
     * the lock-free wait this is only a small extra latency per iteration. */
    core_system_sleep(MQTT_YIELD_GAP_MS);
    __client_unref(context);
    return MQTT_STATUS_SUCCESS;
}