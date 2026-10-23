/**
 * @file http_manager.h
 * @brief HTTP session manager: pooled sessions, URL parsing, and
 *        request/response-header exchange over TCP or TLS.
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 * get_http_manager_instance() lazily creates a singleton that exposes
 * create/send/receive/destroy callbacks over a fixed pool of
 * MAX_HTTP_SESSION_NUM sessions, serialized by one mutex. Each session keeps
 * its own context: URL, parsed host/path/port/TLS flag and response info.
 *
 * http_session_send() parses the request URL, creates a transporter (TLS when
 * the scheme is https, otherwise TCP), fetches the server certificate through
 * iotdns for TLS, connects with the default 60 s timeout, and hands the
 * request to coreHTTP (HTTPClient_InitializeRequestHeaders / HTTPClient_Send)
 * with a 512-byte header buffer. The body is not parsed (DO_NOT_PARSE_BODY
 * flag): http_session_receive() returns only the status line, content length
 * and keep-alive flag, and this layer never reads the body.
 *
 * Session memory uses core_malloc/core_free.
 */

#ifndef __HTTP_MANAGER_H__
#define __HTTP_MANAGER_H__

#include "tuya_cloud_types.h"

#include "core_api.h"

#include "tuya_ai_types.h"
#include "http_inf.h"
#include "httpc.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#ifndef MAX_HTTP_SESSION_NUM
#define MAX_HTTP_SESSION_NUM 16 // max number of active session
#endif
#define INVALID_HTTP_SESSION_ID 0xFFFFffff // invalid HTTP session handle

/***********************************************************
***********************typedef define***********************
***********************************************************/

typedef enum {
    HTTP_VER_1_0,
    HTTP_VER_1_1,
} http_ver_t;

typedef enum {
    HTTP_DELETE = 0,
    HTTP_GET,
    HTTP_HEAD,
    HTTP_POST,
    HTTP_PUT,
    HTTP_CONNECT,
    HTTP_OPTIONS,
    HTTP_TRACE,
} http_method_t;

typedef struct {
    /** The Type of HTTP Request */
    http_method_t type;
    /** The target resource for the HTTP Request. A complete URL is also
     * accepted.
     */
    const char *resource;
    /** The HTTP Protocol Version */
    http_ver_t version;
    /** Pointer to data buffer. NULL if GET request */
    const char *content;
    /** The length of the data pointed to by \a content above. This is
     * don't-care if the content is set to NULL
     */
    int content_len;
} http_req_t;

/**
 * Structure used to give back http header response fields to the caller.
 */
typedef struct {
    /** The value of the protocol field in the first line of the HTTP
        header response. e.g. "HTTP". */
    const char *protocol;
    /** HTTP version */
    http_ver_t version;
    /** The status code returned as a part of the first line of the
        HTTP response e.g. 200 if success
    */
    int status_code;
    /** HTTP "Location" header field value */
    const char *location;
    /** HTTP "Server" header field value */
    const char *server;
    /** If "Keep-Alive" field is present or if the value of
        "Connection" field is "Keep-Alive" then this member is set to
        'true'. It is set to 'false' in other cases
    */
    bool_t keep_alive_ack;
    /** This will be 'true' if "Transfer-Encoding" field is set to
        "chunked".
    */
    bool_t chunked;
    /** Value of the "Content-Length" field. If "Transfer-Encoding" is
        set to chunked then this value will be zero.
    */
    unsigned int content_length;
} http_resp_t;

/**
 * @brief Definition of HTTP session state
 */
typedef enum {
    // HTTP_FREE = 0,   // session is free
    HTTP_DISCONNECT = 1, // session is disconnected
    HTTP_CONNECTING,     // session is connecting
    HTTP_CONNECTED,      // session is connected and ready to send/recv
    HTTP_UPLOADING,      // session has sent data and is ready to recv
} E_HTTP_SESSION_STATE;

typedef enum {
    HDR_ADD_DEFAULT_USER_AGENT = 0x0001,
} http_hdr_field_sel_t;

/**
 * @brief The HTTP session's Request structure
 */
typedef struct {
    /** internal HTTP session */
    http_session_t s;

    /** http request */
    http_req_t req;

    /** session state, please refer to enum E_HTTP_SESSION_STATE */
    E_HTTP_SESSION_STATE state;
} S_HTTP_SESSION;

typedef S_HTTP_SESSION *SESSION_ID;

/**
 * @brief This API is used to create HTTP session
 *
 * @param[in] url URL of HTTP session, max len of URL is 256
 * @param[in] is_persistent Session is persistent or not
 *
 * @return SESSION_ID on success, NULL on error
 */
typedef SESSION_ID (*FUNC_HTTP_SESSION_CREATE)(IN CONST char *url, BOOL_T is_persistent);

/**
 * @brief This API is used to send HTTP session request
 *
 * @param[in] session Session returned from the call to FUNC_HTTP_SESSION_CREATE
 * @param[in] req The http_req_t structure filled up with appropriate parameters
 * @param[in] field_flags The http_hdr_field_sel_t, OR of zero or more flags
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
typedef OPERATE_RET (*FUNC_HTTP_SESSION_SEND)(IN CONST SESSION_ID session, IN CONST http_req_t *req,
                                              http_hdr_field_sel_t field_flags);

/**
 * @brief This API is used to recv response header from HTTP session
 *
 * @param[in] session Session returned from the call to FUNC_HTTP_SESSION_CREATE
 * @param[in,out] resp Pointer to a pointer of type http_resp_t .The
 * structure will be allocated by the callee. Note that the caller does
 * not need to free the structure allocated and returned from this
 * API. The allocation and free is done by the callee automatically
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
typedef OPERATE_RET (*FUNC_HTTP_SESSION_RECEIVE)(SESSION_ID session, http_resp_t **resp);

/**
 * @brief This API is used to destroy HTTP session
 *
 * @param[in] session Session returned from the call to FUNC_HTTP_SESSION_CREATE
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
typedef OPERATE_RET (*FUNC_HTTP_SESSION_DESTORY)(SESSION_ID session);

/**
 * @brief The HTTP session manager structure.
 */
typedef struct {
    /** session resources */
    S_HTTP_SESSION *session[MAX_HTTP_SESSION_NUM];
    /** exclusive access to session resources*/
    MUTEX_HANDLE mutex;

    /** session manager inited or not */
    bool inited;

    /** handler to create HTTP session */
    FUNC_HTTP_SESSION_CREATE create_http_session;
    /** handler to send HTTP session request */
    FUNC_HTTP_SESSION_SEND send_http_request;
    /** handler to recv response header from HTTP session */
    FUNC_HTTP_SESSION_RECEIVE receive_http_response;
    /** handler to destroy HTTP session */
    FUNC_HTTP_SESSION_DESTORY destory_http_session;
} S_HTTP_MANAGER;

/**
 * @brief Retrieve instance of HTTP session manager
 *
 * @note HTTP manager is singleton, multiple call to this API will return the same instance
 *
 * @return NULL on error. Instance on success, please refer to S_HTTP_MANAGER
 */
S_HTTP_MANAGER *get_http_manager_instance(VOID_T);

/***********************************************************
********************function declaration********************
***********************************************************/

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_MANAGER_H__ */
