/**
 * @file ec200_ssl_socket.h
 * @brief TLS client sockets (AT+QSSL* — QSSLOPEN/SEND/RECV/CLOSE).
 *
 * The secure counterpart of ec200_tcpip.h.  Configure an SSL context with
 * ec200_ssl_configure() first, then open a TLS connection that references
 * both a PDP context and that SSL context.
 */

#ifndef EC200_SSL_SOCKET_H
#define EC200_SSL_SOCKET_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_SSLSock TLS Sockets
 *  @brief TLS client sockets over the module's stack.
 *  @{
 */

/**
 * @brief Open a TLS connection (AT+QSSLOPEN).
 *
 * @param h        Initialised handle.
 * @param pdp_ctx  PDP context id (1-16), already activated.
 * @param ssl_ctx  SSL context id (0-5), already configured.
 * @param conn_id  Socket id (0-11).
 * @param host     Remote hostname or IP.
 * @param port     Remote port (e.g. 443, 8883).
 *
 * @return EC200_OK on successful handshake; EC200_ERR_MODULE on a nonzero
 *         open/handshake result; EC200_ERR_PARAM / TIMEOUT / IO otherwise.
 */
ec200_status_t ec200_ssl_socket_open(ec200_handle_t *h,
                                     uint8_t         pdp_ctx,
                                     uint8_t         ssl_ctx,
                                     uint8_t         conn_id,
                                     const char     *host,
                                     uint16_t        port);

/**
 * @brief Send bytes over a TLS socket (AT+QSSLSEND).  Binary-safe.
 *
 * @param h        Initialised handle.
 * @param conn_id  Socket id (0-11).
 * @param data     Bytes to send.
 * @param len      Number of bytes (1..EC200_MAX_PAYLOAD_LEN).
 * @return EC200_OK, EC200_ERR_MODULE (SEND FAIL), or an error code.
 */
ec200_status_t ec200_ssl_socket_send(ec200_handle_t *h,
                                     uint8_t         conn_id,
                                     const uint8_t  *data,
                                     uint16_t        len);

/**
 * @brief Receive up to @p max_len bytes from a TLS socket (AT+QSSLRECV).
 *
 * @param h          Initialised handle.
 * @param conn_id    Socket id (0-11).
 * @param buf        Output buffer.
 * @param max_len    Buffer capacity / max bytes to read.
 * @param bytes_read Output: number of bytes actually read (0 = none pending).
 * @param timeout_ms Read timeout in milliseconds.
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_ssl_socket_recv(ec200_handle_t *h,
                                     uint8_t         conn_id,
                                     uint8_t        *buf,
                                     uint16_t        max_len,
                                     uint16_t       *bytes_read,
                                     uint32_t        timeout_ms);

/**
 * @brief Close a TLS socket (AT+QSSLCLOSE).
 *
 * @param h        Initialised handle.
 * @param conn_id  Socket id (0-11).
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_ssl_socket_close(ec200_handle_t *h, uint8_t conn_id);

/** @} */ /* EC200_SSLSock */

#ifdef __cplusplus
}
#endif

#endif /* EC200_SSL_SOCKET_H */
