/**
 * @file http_download.c
 * @brief HTTP file download: size query, ranged requests and sequential
 * write-out through an http_session.
 *
 * http_file_download() drives a small state machine (start, connect, filesize
 * query, range request, data retrieval, complete) that opens a session for the
 * URL, learns the remote file size, requests the remaining range and appends
 * the received body to the destination file until the transfer completes or
 * fails.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */
#include "core_api.h"
#include "tuya_log.h"
#include "tuya_error_code.h"
#include "core_http_client.h"
#include "transport_interface.h"
#include "http_download.h"
#include "http_session.h"

typedef enum {
    DL_STATE_IDLE,
    DL_STATE_START,
    DL_STATE_NETWORK_CONNECT,
    DL_STATE_NETWORK_RECONNECT,
    DL_STATE_FILESIZE_GET,
    DL_STATE_RANGE_REQUEST,
    DL_STATE_DATE_GET,
    DL_STATE_COMPLETE,
} http_download_state_t;

typedef struct {
    http_download_config_t config;
    http_download_event_t event;
    TransportInterface_t transport;
    HTTPRequestHeaders_t requestHeaders;
    HTTPRequestInfo_t requestInfo;
    HTTPResponse_t response;
    char *host;
    char *path;
    uint16_t port;
    size_t file_size;
    size_t received_size;
    size_t remain_len;
    size_t offset;
    uint8_t state;
    uint8_t *buffer;
} http_download_t;

#define MAX_RETRY_TIMES (8u)
/*-----------------------------------------------------------*/
/**
 * @brief The size of the range of the file to download, with each request.
 *
 * @note This should account for the response headers that will also be stored
 * in the user buffer. We don't expect S3 to send more than 1024 bytes of
 * headers.
 */
#define RANGE_REQUEST_LENGTH_DEFAULT (8 * 1024)

/**
 * @brief The length of the HTTP GET method.
 */
#define HTTP_METHOD_GET_LENGTH (sizeof(HTTP_METHOD_GET) - 1)

/**
 * @brief Field name of the HTTP Range header to read from server response.
 */
#define HTTP_CONTENT_RANGE_HEADER_FIELD "Content-Range"

/**
 * @brief Length of the HTTP Range header field.
 */
#define HTTP_CONTENT_RANGE_HEADER_FIELD_LENGTH (sizeof(HTTP_CONTENT_RANGE_HEADER_FIELD) - 1)

/**
 * @brief HTTP status code returned for partial content.
 */
#define HTTP_STATUS_CODE_PARTIAL_CONTENT 206

//! timeout sec
#define HTTP_DOWNLOAD_TIMEOUT 180

/*-----------------------------------------------------------*/
static int http_download_filesize_get(http_download_t *ctx)
{
    int rt = 0;
    /* The location of the file size in contentRangeValStr. */
    char *pFileSizeStr = NULL;
    /* String to store the Content-Range header value. */
    char *contentRangeValStr = NULL;
    size_t contentRangeValStrLength = 0;

    PR_DEBUG("Getting file object size from host...");
    TUYA_CALL_ERR_GOTO(HTTPClient_InitializeRequestHeaders(&ctx->requestHeaders, &ctx->requestInfo), __exit);
    TUYA_CALL_ERR_GOTO(HTTPClient_AddRangeHeader(&ctx->requestHeaders, 0, 0), __exit);
    /* Range: 0-0 returns a 1-byte body, so the full response is received;
     * the download buffer serves as pBuffer */
    ctx->response.pBuffer = ctx->buffer;
    ctx->response.bufferLen = ctx->config.range_length;
    ctx->response.respOptionFlags = 0;
    TUYA_CALL_ERR_GOTO(HTTPClient_Send(&ctx->transport, &ctx->requestHeaders, NULL, 0, &ctx->response, 0), __exit);
    PR_DEBUG("Received HTTP response from %s%s...", ctx->host, ctx->path);
    PR_DEBUG("Response Headers:\n%.*s", (int32_t)ctx->response.headersLen, ctx->response.pHeaders);
    if (ctx->response.statusCode != HTTP_STATUS_CODE_PARTIAL_CONTENT) {
        PR_ERR("Received an invalid response from the server "
               "(Status Code: %u).",
               ctx->response.statusCode);
        rt = OPRT_NOT_SUPPORTED;
        goto __exit;
    }
    TUYA_CALL_ERR_GOTO(HTTPClient_ReadHeader(&ctx->response, (char *)HTTP_CONTENT_RANGE_HEADER_FIELD,
                                             (size_t)HTTP_CONTENT_RANGE_HEADER_FIELD_LENGTH,
                                             (const char **)&contentRangeValStr, &contentRangeValStrLength),
                       __exit);
    pFileSizeStr = strstr(contentRangeValStr, "/");
    if (pFileSizeStr == NULL) {
        PR_ERR("'/' not present in Content-Range header value: %s.", contentRangeValStr);
        rt = OPRT_NOT_FOUND;
        goto __exit;
    }
    pFileSizeStr += sizeof(char);
    ctx->file_size = (size_t)strtoul(pFileSizeStr, NULL, 10);
    PR_INFO("The file is %d bytes long.", (int32_t)ctx->file_size);
    /* pBuffer is ctx->buffer (do not free); only reset the response state
     * for the next request */
    memset(&ctx->response, 0, sizeof(ctx->response));
__exit:
    return rt;
}

static int http_download_range_request(http_download_t *ctx, uint32_t range_start, uint32_t range_end)
{
    int rt = OPRT_OK;

    PR_DEBUG("Downloading bytes %d-%d, from %s...: ", range_start, range_end, ctx->host);
    TUYA_CALL_ERR_GOTO(HTTPClient_InitializeRequestHeaders(&ctx->requestHeaders, &ctx->requestInfo), __exit);
    TUYA_CALL_ERR_GOTO(HTTPClient_AddRangeHeader(&ctx->requestHeaders, range_start, range_end), __exit);
    PR_TRACE("Request Headers:\n%.*s", (int32_t)ctx->requestHeaders.headersLen, (char *)ctx->requestHeaders.pBuffer);
    ctx->response.pBuffer = ctx->buffer;
    ctx->response.bufferLen = ctx->config.range_length;
    ctx->response.respOptionFlags = HTTP_RESPONSE_DO_NOT_PARSE_BODY_FLAG;
    TUYA_CALL_ERR_GOTO(HTTPClient_Send(&ctx->transport, &ctx->requestHeaders, NULL, 0, &ctx->response, 0),
                       __exit);
    PR_TRACE("Received HTTP response from %s%s...", ctx->host, ctx->path);
    PR_TRACE("Response Headers:\n%.*s", (int32_t)ctx->response.headersLen, ctx->response.pHeaders);
__exit:
    return rt;
}

/*-----------------------------------------------------------*/
static int http_file_download_init(http_download_t *ctx, http_download_config_t *config)
{
    int rt = OPRT_OK;

    if (NULL == ctx || NULL == config || NULL == config->url) {
        return OPRT_INVALID_PARM;
    }
    memset(ctx, 0, sizeof(http_download_t));
    memcpy(&ctx->config, config, sizeof(http_download_config_t));
    ctx->file_size = ctx->config.file_size;
    ctx->config.range_length = config->range_length;
    if (config->range_length == 0) {
        ctx->config.range_length = RANGE_REQUEST_LENGTH_DEFAULT;
    }
    ctx->event.user_data = ctx->config.user_data;

    /* url parse to host port path (http(s)://host[:port]/path) */
    http_url_parts_t parts;
    TUYA_CALL_ERR_RETURN(http_url_parse(config->url, &parts));

    PR_DEBUG("UF_SCHEMA: %.*s", parts.schema_len, parts.schema);
    PR_DEBUG("UF_HOST: %.*s", parts.host_len, parts.host);
    PR_DEBUG("UF_PATH: %s", parts.path);
    PR_DEBUG("PORT: %d", parts.port);
    ctx->port = parts.port;
    ctx->host = core_malloc(parts.host_len + 1);
    TUYA_CHECK_NULL_RETURN(ctx->host, OPRT_MALLOC_FAILED);
    memcpy(ctx->host, parts.host, parts.host_len);
    ctx->host[parts.host_len] = 0;

    ctx->path = core_malloc(parts.path_len + 1);
    TUYA_CHECK_NULL_RETURN(ctx->path, OPRT_MALLOC_FAILED);
    strcpy(ctx->path, parts.path);

    ctx->buffer = core_malloc(ctx->config.range_length + 1);
    TUYA_CHECK_NULL_RETURN(ctx->buffer, OPRT_MALLOC_FAILED);

    HTTPRequestInfo_t *requestInfo = &ctx->requestInfo;
    requestInfo->pHost = ctx->host;
    requestInfo->hostLen = strlen(ctx->host);
    requestInfo->pMethod = HTTP_METHOD_GET;
    requestInfo->methodLen = sizeof(HTTP_METHOD_GET) - 1;
    requestInfo->pPath = ctx->path;
    requestInfo->pathLen = strlen(ctx->path);
    requestInfo->reqFlags = HTTP_REQUEST_KEEP_ALIVE_FLAG;
    /* Set the buffer used for storing request headers. */

    HTTPRequestHeaders_t *requestHeaders = &ctx->requestHeaders;
    requestHeaders->bufferLen = 512;
    requestHeaders->pBuffer = core_malloc(requestHeaders->bufferLen);
    TUYA_CHECK_NULL_RETURN(requestHeaders->pBuffer, OPRT_MALLOC_FAILED);

    return rt;
}

int http_file_download(http_download_config_t *config)
{
    int rt = OPRT_OK;

    http_download_t *ctx = core_calloc(1, sizeof(http_download_t));
    TUYA_CHECK_NULL_GOTO(ctx, __exit);
    TUYA_CALL_ERR_GOTO(http_file_download_init(ctx, config), __exit);
    /* TLS pre init */
    TUYA_TRANSPORT_TYPE_E transport_type = (config->cacert == NULL) ? TRANSPORT_TYPE_TCP : TRANSPORT_TYPE_TLS;
    NetworkContext_t network;
    TUYA_CHECK_NULL_GOTO(network = tuya_transporter_create(transport_type, NULL), __exit);
    if (transport_type == TRANSPORT_TYPE_TLS) {
        tuya_tls_config_t tls_config = {
            .ca_cert = (char *)config->cacert,
            .ca_cert_size = config->cacert_len,
            .hostname = (char *)ctx->host,
            .port = ctx->port,
            .mode = TUYA_TLS_SERVER_CERT_MODE,
            .verify = true,
        };

        TUYA_CALL_ERR_GOTO(tuya_transporter_ctrl(network, TUYA_TRANSPORTER_SET_TLS_CONFIG, &tls_config), __exit);
    }
    /* http client TransportInterface */
    ctx->transport.pNetworkContext = (NetworkContext_t *)&network;
    ctx->transport.send = (TransportSend_t)NetworkTransportSend;
    ctx->transport.recv = (TransportRecv_t)NetworkTransportRecv;

    ctx->state = DL_STATE_NETWORK_CONNECT;
    TIME_T download_time = core_time_get_posix();

    bool is_completed = false;

    int32_t read_size = 0;

    if (ctx->config.event_handler) {
        ctx->config.event_handler(DL_EVENT_START, &ctx->event);
    }

    do {

        switch (ctx->state) {

        case DL_STATE_NETWORK_CONNECT:
            rt = tuya_transporter_connect(network, ctx->host, ctx->port, config->timeout_ms);
            if (OPRT_OK == rt) {
                ctx->state = DL_STATE_FILESIZE_GET;
            } else {
                ctx->state = DL_STATE_NETWORK_RECONNECT;
            }
            break;

        case DL_STATE_FILESIZE_GET:
            if (0 == ctx->file_size) {
                rt = http_download_filesize_get(ctx);
            }
            if (OPRT_OK != rt) {
                ctx->state = DL_STATE_NETWORK_RECONNECT;
                break;
            }
            if (ctx->config.event_handler) {
                ctx->event.file_size = ctx->file_size;
                ctx->config.event_handler(DL_EVENT_ON_FILESIZE, &ctx->event);
            }
            ctx->state = DL_STATE_RANGE_REQUEST;
            break;

        case DL_STATE_RANGE_REQUEST:
            rt = http_download_range_request(ctx, ctx->received_size, ctx->file_size);
            if (OPRT_OK != rt) {
                ctx->state = DL_STATE_NETWORK_RECONNECT;
                break;
            }
            ctx->state = DL_STATE_DATE_GET;
            /* fall through */
            /* Intent: after the range request is sent, go straight to reading
             * data in this iteration instead of looping through the while
             * again (DATE_GET is the only successor of RANGE_REQUEST, so no
             * state is skipped).
             * The previous line is the English keyword comment recognized by
             * GCC; without it the esp32 build emits a warning. */

        case DL_STATE_DATE_GET: {
            /* On the first round, consume the body fragment pre-received into
             * pBuffer by Send(DO_NOT_PARSE_BODY) (pBody points into the body
             * area of pBuffer); later data is read raw through the transport. */
            if (ctx->response.bodyLen > 0 && ctx->response.pBody != NULL) {
                memmove(ctx->buffer, ctx->response.pBody, ctx->response.bodyLen);
                read_size = (int32_t)ctx->response.bodyLen;
                ctx->response.bodyLen = 0;
            } else {
                read_size = tuya_transporter_read(network, ctx->buffer + ctx->remain_len,
                                                  ctx->config.range_length - ctx->remain_len, config->timeout_ms);
            }

            if (read_size <= 0) {
                PR_WARN("file download range get error:%d, goto retry", rt);
                ctx->state = DL_STATE_NETWORK_RECONNECT;
                break;
            }
            if (ctx->config.event_handler) {
                ctx->event.data = (uint8_t *)ctx->buffer;
                ctx->event.data_len = read_size + ctx->remain_len;
                ctx->event.offset = ctx->received_size - ctx->remain_len;
                ctx->event.remain_len = ctx->remain_len;
                ctx->config.event_handler(DL_EVENT_ON_DATA, &ctx->event);
                if (ctx->event.remain_len) {
                    memmove(ctx->buffer, ctx->buffer + (ctx->event.data_len - ctx->event.remain_len),
                            ctx->event.remain_len);
                }
                ctx->remain_len = ctx->event.remain_len;
                ctx->received_size += read_size;
            }
            //! reset time
            download_time = core_time_get_posix();
            /* File download complete? */
            if (ctx->received_size >= ctx->file_size) {
                ctx->state = DL_STATE_COMPLETE;
            }
            break;
        }

        case DL_STATE_NETWORK_RECONNECT:
            tuya_transporter_close(network);
            core_system_sleep(3000);
            ctx->state = DL_STATE_NETWORK_CONNECT;
            break;

        case DL_STATE_COMPLETE:
            PR_INFO("Download Complete!");
            is_completed = true;
            if (ctx->config.event_handler) {
                ctx->config.event_handler(DL_EVENT_FINISH, &ctx->event);
            }
            break;
        }
    } while (((core_time_get_posix() - download_time) < HTTP_DOWNLOAD_TIMEOUT) && !is_completed);

    tuya_transporter_close(network);
    tuya_transporter_destroy(network);

    if (!is_completed) {
        if (ctx->config.event_handler) {
            ctx->config.event_handler(DL_EVENT_FAULT, &ctx->event);
        }
    }

__exit:
    if (ctx) {
        if (ctx->host) {
            core_free(ctx->host);
        }
        if (ctx->path) {
            core_free(ctx->path);
        }
        if (ctx->requestHeaders.pBuffer) {
            core_free(ctx->requestHeaders.pBuffer);
        }
        if (ctx->response.pBuffer) {
            core_free(ctx->response.pBuffer);
        }

        core_free(ctx);
    }

    return rt;
}
