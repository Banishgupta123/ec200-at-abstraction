/**
 * @file ec200_http.h
 * @brief HTTP client API (Quectel AT+QHTTP*).
 */

#ifndef EC200_HTTP_H
#define EC200_HTTP_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_HTTP HTTP Client
 *  @brief Configure context, set URL, perform GET/POST, read response body (AT+QHTTP*).
 *  @{
 */

/**
 * @brief Configure the HTTP context (AT+QHTTPCFG).
 *
 * Sets the PDP context ID to use for HTTP requests.
 *
 * @param h       Initialised library handle.
 * @param ctx_id  PDP context ID (1-16).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_http_set_context(ec200_handle_t *h, uint8_t ctx_id);

/**
 * @brief Set the HTTP request URL (AT+QHTTPURL).
 *
 * @param h    Initialised library handle.
 * @param url  Full URL string (NUL-terminated, max EC200_MAX_URL_LEN).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_http_set_url(ec200_handle_t *h, const char *url);

/**
 * @brief Perform an HTTP GET request (AT+QHTTPGET).
 *
 * The URL must have been set previously with ec200_http_set_url().
 *
 * @param h          Initialised library handle.
 * @param timeout_ms Response wait timeout in milliseconds.
 * @param resp       Output: HTTP status code and content length.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_http_get(ec200_handle_t       *h,
                              uint32_t              timeout_ms,
                              ec200_http_response_t *resp);

/**
 * @brief Perform an HTTP POST request (AT+QHTTPPOST).
 *
 * The URL must have been set previously with ec200_http_set_url().
 *
 * @param h            Initialised library handle.
 * @param body         Pointer to the POST body data.
 * @param body_len     Length of @p body in bytes.
 * @param content_type Body content type (the EC200U takes a numeric index,
 *                     not a MIME string — see ::ec200_http_content_type_t).
 * @param timeout_ms   Response wait timeout in milliseconds.
 * @param resp         Output: HTTP status code and content length.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_http_post(ec200_handle_t          *h,
                               const uint8_t           *body,
                               uint32_t                 body_len,
                               ec200_http_content_type_t content_type,
                               uint32_t                 timeout_ms,
                               ec200_http_response_t   *resp);

/**
 * @brief Read the HTTP response body (AT+QHTTPREAD).
 *
 * Must be called after a successful ec200_http_get() or ec200_http_post().
 *
 * @param h          Initialised library handle.
 * @param buf        Caller-allocated buffer for the response body.
 * @param buf_sz     Size of @p buf in bytes.
 * @param bytes_read Output: number of bytes written into @p buf.
 * @param timeout_ms Read timeout in milliseconds.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_http_read(ec200_handle_t *h,
                               uint8_t        *buf,
                               size_t          buf_sz,
                               uint32_t       *bytes_read,
                               uint32_t        timeout_ms);

/**
 * @brief Stop and release the HTTP session (AT+QHTTPSTOP).
 *
 * @param h  Initialised library handle.
 * @return   EC200_OK or an error code.
 */
ec200_status_t ec200_http_stop(ec200_handle_t *h);

/** @} */ /* EC200_HTTP */

#ifdef __cplusplus
}
#endif

#endif /* EC200_HTTP_H */
