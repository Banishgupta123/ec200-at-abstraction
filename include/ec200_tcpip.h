/**
 * @file ec200_tcpip.h
 * @brief TCP/IP socket API (Quectel AT+QIOPEN / AT+QISEND / AT+QICLOSE).
 */

#ifndef EC200_TCPIP_H
#define EC200_TCPIP_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_TCPIP TCP/IP Sockets
 *  @brief Open, send, receive, and close TCP/UDP connections (AT+QI*).
 *  @{
 */

/**
 * @brief Open a TCP or UDP socket (AT+QIOPEN).
 *
 * @param h            Initialised library handle.
 * @param ctx_id       PDP context ID associated with this connection (0-16).
 * @param conn_id      Connection ID to use (0 to EC200_MAX_CONNECTIONS-1).
 * @param type         EC200_SOCK_TCP or EC200_SOCK_UDP.
 * @param host         Remote hostname or IP address (NUL-terminated).
 * @param port         Remote port number.
 * @param access_mode  Buffer / direct / transparent access mode.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_tcp_open(ec200_handle_t    *h,
                              uint8_t            ctx_id,
                              uint8_t            conn_id,
                              ec200_sock_type_t  type,
                              const char        *host,
                              uint16_t           port,
                              ec200_access_mode_t access_mode);

/**
 * @brief Send data over an open TCP/UDP connection (AT+QISEND).
 *
 * @param h        Initialised library handle.
 * @param conn_id  Connection ID (must be open).
 * @param data     Pointer to data buffer.
 * @param len      Number of bytes to send.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_tcp_send(ec200_handle_t *h,
                              uint8_t         conn_id,
                              const uint8_t  *data,
                              uint16_t        len);

/**
 * @brief Receive data from an open TCP/UDP connection (AT+QIRD).
 *
 * @param h           Initialised library handle.
 * @param conn_id     Connection ID.
 * @param buf         Caller-allocated receive buffer.
 * @param max_len     Size of @p buf in bytes.
 * @param bytes_read  Output: number of bytes received.
 * @param timeout_ms  Maximum wait time in milliseconds.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_tcp_recv(ec200_handle_t *h,
                              uint8_t         conn_id,
                              uint8_t        *buf,
                              uint16_t        max_len,
                              uint16_t       *bytes_read,
                              uint32_t        timeout_ms);

/**
 * @brief Close a TCP/UDP connection (AT+QICLOSE).
 *
 * @param h        Initialised library handle.
 * @param conn_id  Connection ID to close.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_tcp_close(ec200_handle_t *h, uint8_t conn_id);

/**
 * @brief Query the state of a connection (AT+QISTATE).
 *
 * @param h       Initialised library handle.
 * @param conn_id Connection ID to query.
 * @param sock    Output: socket state structure.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_tcp_get_state(ec200_handle_t *h,
                                   uint8_t         conn_id,
                                   ec200_socket_t *sock);

/**
 * @brief Query the number of bytes available to read on a connection (AT+QIRD=\<conn_id\>,0).
 *
 * @param h            Initialised library handle.
 * @param conn_id      Connection ID.
 * @param bytes_avail  Output: number of bytes waiting in the receive buffer.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_tcp_bytes_available(ec200_handle_t *h,
                                         uint8_t         conn_id,
                                         uint32_t       *bytes_avail);

/** @} */ /* EC200_TCPIP */

#ifdef __cplusplus
}
#endif

#endif /* EC200_TCPIP_H */
