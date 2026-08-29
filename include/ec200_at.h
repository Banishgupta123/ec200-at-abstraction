/**
 * @file ec200_at.h
 * @brief Low-level AT command transport layer.
 *
 * Provides the core send/receive mechanics used by all higher-level modules.
 * User code should not normally call these functions directly; instead use the
 * domain-specific APIs (ec200_sim.h, ec200_network.h, …).
 *
 * ## Transaction shapes
 *
 * The engine models the four response shapes the EC200 actually produces:
 *
 * 1. `cmd → [lines…] → OK`                    : ec200_at_send()
 * 2. `cmd → +PREFIX line → OK`                : ec200_at_send_wait()
 * 3. `cmd → +PREFIX line → raw data …`        : ec200_at_send_expect() +
 *                                              ec200_at_read_exact() +
 *                                              ec200_at_wait_final()
 * 4. `cmd → OK → … later → +PREFIX urc`       : ec200_at_send_await_urc()
 *
 * Plus data-mode entry (`cmd → ">" prompt`)   : ec200_at_send_prompt()
 * and receive-only waits (after Ctrl-Z etc.)  : ec200_at_wait_prefix() /
 *                                              ec200_at_wait_final()
 *
 * Lines matching a prefix registered with ec200_at_register_urc() are
 * dispatched to their handler even when they arrive in the middle of a
 * command transaction, so asynchronous URCs are never mis-filed as response
 * data.
 *
 * ## Timeout model
 *
 * @p timeout_ms is a total deadline budget for the call.  Time is charged
 * whenever the read callback reports a timeout; in addition every transaction
 * is bounded by ::EC200_AT_MAX_LINES lines so a URC storm or a byte-trickling
 * UART cannot block the caller indefinitely.
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

/** Maximum response/URC lines processed per transaction (storm guard). */
#define EC200_AT_MAX_LINES          (64U)

/** Granularity of a single blocking read while waiting for data (ms). */
#define EC200_AT_POLL_SLICE_MS      (100U)

/** Total budget for retrying short writes before giving up (ms). */
#define EC200_AT_WRITE_TIMEOUT_MS   (1000U)

/* -------------------------------------------------------------------------
 * Command transactions
 * ------------------------------------------------------------------------- */

/**
 * @brief Send an AT command and wait for the final result code.
 *
 * Transmits @p cmd followed by `\r`, then reads response lines until a
 * terminal result ("OK", "ERROR", "+CME ERROR:", "+CMS ERROR:") arrives.
 * Non-terminal lines are accumulated into @p resp_buf separated by `\n`
 * (lines that do not fit are dropped, the transaction still completes).
 *
 * @param h           Library handle (must be initialised).
 * @param cmd         NUL-terminated AT command string (without termination).
 * @param resp_buf    Optional buffer for the response body (NULL = discard).
 * @param resp_buf_sz Size of @p resp_buf in bytes (ignored when NULL).
 * @param timeout_ms  Total deadline for the transaction.
 *
 * @return EC200_OK, EC200_ERR_TIMEOUT, EC200_ERR_CME, EC200_ERR_CMS,
 *         EC200_ERR_IO, EC200_ERR_OVERFLOW, or EC200_ERR_NOT_READY.
 */
ec200_status_t ec200_at_send(ec200_handle_t *h,
                             const char     *cmd,
                             char           *resp_buf,
                             size_t          resp_buf_sz,
                             uint32_t        timeout_ms);

/**
 * @brief Send an AT command, capture one prefixed line, and read through the
 *        terminal "OK".
 *
 * The line starting with @p expected_prefix is copied to @p resp_buf; the
 * engine then keeps reading until the terminal result code so the stream
 * stays synchronised (nothing is left queued for the next command).
 *
 * Use ec200_at_send_expect() instead when raw data follows the prefixed line.
 *
 * @return EC200_OK on prefix match followed by OK; EC200_ERR_PARSE if the
 *         terminal OK arrived without the prefix; or other error codes.
 */
ec200_status_t ec200_at_send_wait(ec200_handle_t *h,
                                  const char     *cmd,
                                  const char     *expected_prefix,
                                  char           *resp_buf,
                                  size_t          resp_buf_sz,
                                  uint32_t        timeout_ms);

/**
 * @brief Send an AT command and stop immediately after the prefixed line.
 *
 * Unlike ec200_at_send_wait() no further bytes are consumed once the prefix
 * line has been read, so raw data that follows the line (AT+QIRD payload,
 * AT+QHTTPREAD body after "CONNECT") is left untouched for the caller.
 * The caller must consume the data and then call ec200_at_wait_final() to
 * re-synchronise on the trailing "OK".
 *
 * @return EC200_OK on prefix match; error codes as ec200_at_send_wait().
 */
ec200_status_t ec200_at_send_expect(ec200_handle_t *h,
                                    const char     *cmd,
                                    const char     *expected_prefix,
                                    char           *resp_buf,
                                    size_t          resp_buf_sz,
                                    uint32_t        timeout_ms);

/**
 * @brief Send an asynchronous command: expect "OK" first, then a result URC.
 *
 * Quectel's asynchronous commands (AT+QMTOPEN, AT+QIOPEN, AT+QHTTPGET, …)
 * acknowledge with an immediate "OK" and report the actual outcome later as
 * an unsolicited `+PREFIX: …` line.  This primitive handles both phases.
 *
 * @param h            Library handle.
 * @param cmd          Command to send.
 * @param urc_prefix   Result line prefix (e.g. "+QMTOPEN:").
 * @param urc_buf      Buffer receiving the result line.
 * @param urc_buf_sz   Size of @p urc_buf.
 * @param ok_timeout   Deadline for the initial "OK".
 * @param urc_timeout  Deadline for the result URC after the "OK".
 *
 * @return EC200_OK once the result line arrived; EC200_ERR_TIMEOUT / CME /
 *         CMS / IO otherwise.
 */
ec200_status_t ec200_at_send_await_urc(ec200_handle_t *h,
                                       const char     *cmd,
                                       const char     *urc_prefix,
                                       char           *urc_buf,
                                       size_t          urc_buf_sz,
                                       uint32_t        ok_timeout,
                                       uint32_t        urc_timeout);

/**
 * @brief Send a command and wait for the ">" data prompt.
 *
 * Used by AT+CMGS / AT+QISEND / AT+QMTPUBEX style commands.  While waiting
 * the engine also assembles complete lines so an early "ERROR" /
 * "+CME ERROR" aborts immediately instead of timing out.
 *
 * @return EC200_OK when the prompt was seen; EC200_ERR_CME / CMS / PARSE on
 *         an error line; EC200_ERR_TIMEOUT / IO otherwise.
 */
ec200_status_t ec200_at_send_prompt(ec200_handle_t *h,
                                    const char     *cmd,
                                    uint32_t        timeout_ms);

/* -------------------------------------------------------------------------
 * Receive-only waits (no transmission — nothing extra hits the wire)
 * ------------------------------------------------------------------------- */

/**
 * @brief Wait for a line starting with @p prefix without sending anything.
 *
 * Returns as soon as the line is matched (it is copied to @p resp_buf).
 * Terminal error lines abort the wait; registered URCs are dispatched.
 *
 * @return EC200_OK, EC200_ERR_TIMEOUT, EC200_ERR_CME/CMS/PARSE, EC200_ERR_IO.
 */
ec200_status_t ec200_at_wait_prefix(ec200_handle_t *h,
                                    const char     *prefix,
                                    char           *resp_buf,
                                    size_t          resp_buf_sz,
                                    uint32_t        timeout_ms);

/**
 * @brief Read lines until the terminal result code ("OK"/"ERROR"/…).
 *
 * Used to re-synchronise the stream after raw-data phases.
 *
 * @return EC200_OK, EC200_ERR_TIMEOUT, EC200_ERR_CME/CMS/PARSE, EC200_ERR_IO.
 */
ec200_status_t ec200_at_wait_final(ec200_handle_t *h, uint32_t timeout_ms);

/* -------------------------------------------------------------------------
 * Raw I/O
 * ------------------------------------------------------------------------- */

/**
 * @brief Write raw bytes to the UART, retrying short writes.
 *
 * @return EC200_OK, EC200_ERR_IO on a fatal write error, or
 *         EC200_ERR_TIMEOUT if the transport kept refusing bytes for
 *         ::EC200_AT_WRITE_TIMEOUT_MS.
 */
ec200_status_t ec200_at_write_raw(ec200_handle_t *h,
                                  const uint8_t  *data,
                                  uint16_t        len);

/**
 * @brief Read up to @p len raw bytes (single attempt, bypass AT framing).
 *
 * Bytes already buffered by the line assembler are returned first.
 *
 * @return EC200_OK with @p bytes_read set (0 is valid), EC200_ERR_TIMEOUT if
 *         the timeout expired with no data, or EC200_ERR_IO on a fatal error.
 */
ec200_status_t ec200_at_read_raw(ec200_handle_t *h,
                                 uint8_t        *buf,
                                 uint16_t        len,
                                 uint32_t        timeout_ms,
                                 uint16_t       *bytes_read);

/**
 * @brief Read exactly @p len raw bytes, looping until done or deadline.
 *
 * @param h          Library handle.
 * @param buf        Buffer receiving the bytes.
 * @param len        Exact number of bytes to read.
 * @param timeout_ms Total deadline for the full transfer.
 * @param bytes_read Actual byte count on return (== @p len on EC200_OK).
 *
 * @return EC200_OK, EC200_ERR_TIMEOUT (short read — @p bytes_read tells how
 *         far it got), or EC200_ERR_IO.
 */
ec200_status_t ec200_at_read_exact(ec200_handle_t *h,
                                   uint8_t        *buf,
                                   uint16_t        len,
                                   uint32_t        timeout_ms,
                                   uint16_t       *bytes_read);

/* -------------------------------------------------------------------------
 * URC handling
 * ------------------------------------------------------------------------- */

/**
 * @brief Register a handler for URC lines starting with @p prefix.
 *
 * Registered URCs are dispatched from ec200_at_poll_urc() *and* from within
 * command transactions, so they are never mis-filed as response data.
 * @p prefix must remain valid while registered (use a string literal).
 *
 * @return EC200_OK, EC200_ERR_PARAM, or EC200_ERR_OVERFLOW if the table is
 *         full (::EC200_MAX_URC_HANDLERS entries).
 */
ec200_status_t ec200_at_register_urc(ec200_handle_t       *h,
                                     const char           *prefix,
                                     ec200_urc_handler_fn  handler,
                                     void                 *ctx);

/**
 * @brief Remove a previously registered URC handler.
 *
 * @return EC200_OK or EC200_ERR_PARAM if the prefix was not registered.
 */
ec200_status_t ec200_at_unregister_urc(ec200_handle_t *h, const char *prefix);

/**
 * @brief Poll for and dispatch any pending URC lines.
 *
 * Call periodically (main loop / RTOS task).  Complete lines are matched
 * against the registered URC table first; unmatched lines go to the fallback
 * handler set with ec200_set_urc_handler().
 *
 * @param h           Library handle.
 * @param timeout_ms  Time to wait for incoming data (0 = non-blocking poll).
 *
 * @return EC200_OK (also when no URC was pending) or EC200_ERR_IO.
 */
ec200_status_t ec200_at_poll_urc(ec200_handle_t *h, uint32_t timeout_ms);

/* -------------------------------------------------------------------------
 * Response parsing utilities
 * ------------------------------------------------------------------------- */

/**
 * @brief Extract the @p index -th comma-separated integer field of a
 *        response line.
 *
 * Field 0 starts after the "+PREFIX:" header (or at the line start when no
 * colon is present).  Commas inside double-quoted strings do not count as
 * separators.
 *
 * @param line   Response line (e.g. `+QMTOPEN: 0,3`).
 * @param index  Zero-based field index.
 * @param value  Parsed integer on success.
 *
 * @return EC200_OK, or EC200_ERR_PARSE if the field is missing / not numeric.
 */
ec200_status_t ec200_at_parse_int_field(const char *line,
                                        unsigned    index,
                                        int        *value);

/* -------------------------------------------------------------------------
 * Error introspection
 * ------------------------------------------------------------------------- */

/**
 * @brief Retrieve the +CME ERROR code from the current/last transaction.
 * @return Integer error code, or -1 if the last failure was not CME.
 */
int ec200_at_last_cme_error(const ec200_handle_t *h);

/**
 * @brief Retrieve the +CMS ERROR code from the current/last transaction.
 * @return Integer error code, or -1 if the last failure was not CMS.
 */
int ec200_at_last_cms_error(const ec200_handle_t *h);

/** @} */ /* EC200_AT */

#ifdef __cplusplus
}
#endif

#endif /* EC200_AT_H */
