/**
 * @file ec200_at.h
 * @brief Low-level AT command transport layer.
 *
 * Provides the core send/receive mechanics used by all higher-level modules.
 * User code should not normally call these functions directly; instead use the
 * domain-specific APIs (ec200_sim.h, ec200_network.h, …).
 */

#ifndef EC200_AT_H
#define EC200_AT_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_AT AT Transport Layer
 *  @brief Low-level AT command send/receive and raw UART access.
 *  @{
 */

/* -------------------------------------------------------------------------
 * Default timeouts (milliseconds)
 * ------------------------------------------------------------------------- */
#define EC200_AT_TIMEOUT_DEFAULT    (1000U)   /**< Generic AT command timeout   */
#define EC200_AT_TIMEOUT_SHORT      (300U)    /**< Fast query timeout           */
#define EC200_AT_TIMEOUT_LONG       (10000U)  /**< Network attach / open socket */
#define EC200_AT_TIMEOUT_HTTP       (30000U)  /**< HTTP GET/POST                */
#define EC200_AT_TIMEOUT_COPS       (120000U) /**< Operator search timeout      */

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * @brief Send a raw AT command string and wait for the final result code.
 *
 * The function transmits @p cmd followed by "\\r\\n", then waits up to
 * @p timeout_ms for a line containing one of the terminal strings "OK",
 * "ERROR", "+CME ERROR:", or "+CMS ERROR:".
 *
 * @param h           Library handle (must be initialised).
 * @param cmd         NUL-terminated AT command string (without \\r\\n).
 * @param resp_buf    Optional buffer to receive the raw module response.
 *                    Pass NULL if the response is not needed.
 * @param resp_buf_sz Size of @p resp_buf in bytes (ignored when NULL).
 * @param timeout_ms  Maximum wait time in milliseconds.
 *
 * @return EC200_OK, EC200_ERR_TIMEOUT, EC200_ERR_CME, EC200_ERR_CMS,
 *         EC200_ERR_IO, or EC200_ERR_OVERFLOW.
 */
ec200_status_t ec200_at_send(ec200_handle_t *h,
                             const char     *cmd,
                             char           *resp_buf,
                             size_t          resp_buf_sz,
                             uint32_t        timeout_ms);

/**
 * @brief Send a raw AT command and wait for a specific expected prefix.
 *
 * Like ec200_at_send() but the function returns EC200_OK as soon as a line
 * starting with @p expected_prefix is received, storing that line in
 * @p resp_buf.  It still terminates on "ERROR" lines.
 *
 * @param h               Library handle.
 * @param cmd             NUL-terminated AT command string.
 * @param expected_prefix Response line prefix to look for (e.g. "+CPIN:").
 * @param resp_buf        Buffer to store the matched response line.
 * @param resp_buf_sz     Size of @p resp_buf.
 * @param timeout_ms      Maximum wait time in milliseconds.
 *
 * @return EC200_OK on prefix match, EC200_ERR_PARSE if the terminal result
 *         code arrived before the expected prefix, or other error codes.
 */
ec200_status_t ec200_at_send_wait(ec200_handle_t *h,
                                  const char     *cmd,
                                  const char     *expected_prefix,
                                  char           *resp_buf,
                                  size_t          resp_buf_sz,
                                  uint32_t        timeout_ms);

/**
 * @brief Write raw bytes to the UART (bypass AT framing).
 *
 * Used when the module is in data / transparent mode.
 *
 * @param h    Library handle.
 * @param data Pointer to bytes to send.
 * @param len  Number of bytes to send.
 *
 * @return EC200_OK or EC200_ERR_IO.
 */
ec200_status_t ec200_at_write_raw(ec200_handle_t *h,
                                  const uint8_t  *data,
                                  uint16_t        len);

/**
 * @brief Read raw bytes from the UART (bypass AT framing).
 *
 * @param h          Library handle.
 * @param buf        Buffer to receive bytes.
 * @param len        Maximum number of bytes to read.
 * @param timeout_ms Maximum wait time in milliseconds.
 * @param bytes_read Number of bytes actually read (output).
 *
 * @return EC200_OK or EC200_ERR_IO / EC200_ERR_TIMEOUT.
 */
ec200_status_t ec200_at_read_raw(ec200_handle_t *h,
                                 uint8_t        *buf,
                                 uint16_t        len,
                                 uint32_t        timeout_ms,
                                 uint16_t       *bytes_read);

/**
 * @brief Poll for and dispatch any pending URC lines.
 *
 * Call this function periodically (from a main loop or RTOS task) to ensure
 * that unsolicited result codes are delivered to the URC callback set in
 * @p h->urc_handler.
 *
 * @param h           Library handle.
 * @param timeout_ms  Time to wait for incoming data (0 = non-blocking poll).
 *
 * @return EC200_OK or EC200_ERR_IO.
 */
ec200_status_t ec200_at_poll_urc(ec200_handle_t *h, uint32_t timeout_ms);

/**
 * @brief Retrieve the last +CME ERROR code received.
 *
 * @param h  Library handle.
 * @return   Integer error code, or -1 if the last error was not CME.
 */
int ec200_at_last_cme_error(const ec200_handle_t *h);

/**
 * @brief Retrieve the last +CMS ERROR code received.
 *
 * @param h  Library handle.
 * @return   Integer error code, or -1 if the last error was not CMS.
 */
int ec200_at_last_cms_error(const ec200_handle_t *h);

/** @} */ /* EC200_AT */

#ifdef __cplusplus
}
#endif

#endif /* EC200_AT_H */
