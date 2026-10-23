/**
 * @file http_client_interface.h
 * @brief HTTP client interface: request/response types and the request/free
 * entry points implemented by http_client_wrapper.c.
 *
 * http_client_request_t carries the method, URL, headers, optional body and
 * transport; http_client_response_t returns the status code, headers and body
 * buffer. http_client_request() performs one exchange and http_client_free()
 * releases the response.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */
#ifndef HTTP_CLIENT_INTERFACE_H
#define HTTP_CLIENT_INTERFACE_H

#include "tuya_cloud_types.h"
#include "core_memory.h"

/* Response buffer size for one-shot requests (shared by headers and body;
 * coreHTTP v3.1.3 requires the application to provide it) */
#define HTTP_CLIENT_RESPONSE_BUFFER_SIZE (8 * 1024)

/**
 * @ingroup http_enum_types
 * @brief The HTTP interface return status.
 */
typedef enum http_client_status {
    HTTP_CLIENT_SUCCESS = 0,
    HTTP_CLIENT_SERIALIZE_FAULT,
    HTTP_CLIENT_SEND_FAULT,
    HTTP_CLIENT_MALLOC_FAULT
} http_client_status_t;

typedef struct http_client_header {
    const char *key;
    const char *value;
} http_client_header_t;

typedef struct http_client_request {
    const char *host;
    uint16_t port;
    const char *path;
    const uint8_t *cacert;
    size_t cacert_len;
    bool tls_no_verify;
    const char *method;
    http_client_header_t *headers;
    uint8_t headers_count;
    const uint8_t *body;
    size_t body_length;
    uint32_t timeout_ms;
} http_client_request_t;

typedef struct http_client_response {
    /**
     * @brief Buffer for both the raw HTTP header and body.
     *
     * This buffer is supplied by the application.
     */
    uint8_t *buffer;
    size_t buffer_length; /**< The length of the response buffer in bytes. */

    /**
     * @brief The starting location of the response headers in buffer.
     */
    const uint8_t *headers;
    size_t headers_length;

    /**
     * @brief The starting location of the response body in buffer.
     */
    const uint8_t *body;
    size_t body_length;

    /**
     * @brief The HTTP response Status-Code.
     */
    uint16_t status_code;
} http_client_response_t;

http_client_status_t http_client_request(const http_client_request_t *request, http_client_response_t *response);

int http_client_free(http_client_response_t *response);

#endif /* ifndef HTTP_CLIENT_INTERFACE_H */
