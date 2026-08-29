/**
 * @file ec200_at.c
 * @brief Low-level AT command transport layer implementation.
 *
 * Design notes
 * ------------
 * - Lines are assembled byte-by-byte into the handle's persistent _rx_buf.
 *   A partial line interrupted by a timeout stays buffered and is completed
 *   on the next call — a partial line is never surfaced to parsers, so every
 *   line seen by the library is complete and NUL-terminated.
 * - Reads request one byte at a time on purpose: the read callback contract
 *   only promises "up to len bytes within timeout", and a bulk request
 *   against a fully-blocking implementation (e.g. HAL_UART_Receive) would
 *   stall for the whole timeout even when a line is already complete.
 *   Raw payload phases (ec200_at_read_exact) do use bulk reads, where
 *   waiting for all bytes is exactly what is wanted.
 * - Every wait is bounded twice: by the caller's timeout budget (charged
 *   whenever the transport reports a timeout) and by EC200_AT_MAX_LINES per
 *   transaction, so neither a URC storm nor a byte-trickling UART can block
 *   the caller indefinitely.
 * - +CME/+CMS error state is reset at the start of every transmission, so a
 *   stale code can never misclassify a later failure.
 */

#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/** Result values for rx_getc()/rx_line(). */
#define RX_OK        (1)
#define RX_TIMEOUT   (0)
#define RX_IO_ERROR  (-1)

static bool handle_ready(const ec200_handle_t *h)
{
    return (h != NULL) && h->_initialised;
}

/**
 * @brief Blocking single-byte read honouring the caller's deadline budget.
 *
 * @return RX_OK (byte in *out), RX_TIMEOUT (budget exhausted), RX_IO_ERROR.
 */
static int rx_getc(ec200_handle_t *h, uint32_t *budget, uint8_t *out)
{
    for (;;) {
        uint32_t slice = (*budget < EC200_AT_POLL_SLICE_MS)
                           ? *budget : EC200_AT_POLL_SLICE_MS;
        int n = h->read(out, 1U, slice, h->user_ctx);
        if (n < 0) {
            return RX_IO_ERROR;
        }
        if (n > 0) {
            return RX_OK;
        }
        if (*budget == 0U) {
            return RX_TIMEOUT;
        }
        *budget -= slice;
    }
}

/**
 * @brief Feed one received byte into the persistent line assembler.
 *
 * CR bytes are dropped; LF completes the line.  A line longer than the
 * buffer is truncated (excess bytes dropped) but still terminates normally.
 *
 * @return true when a complete NUL-terminated line is ready in h->_rx_buf.
 */
static bool rx_feed(ec200_handle_t *h, uint8_t ch)
{
    if (ch == (uint8_t)'\r') {
        return false;
    }
    if (ch == (uint8_t)'\n') {
        h->_rx_buf[h->_rx_len] = '\0';
        h->_rx_len      = 0U;
        h->_rx_overlong = false;
        return true;
    }
    if (h->_rx_len < (uint16_t)(sizeof(h->_rx_buf) - 1U)) {
        h->_rx_buf[h->_rx_len++] = (char)ch;
    } else {
        h->_rx_overlong = true; /* drop excess bytes, keep line boundary */
    }
    return false;
}

/**
 * @brief Assemble one complete line within the deadline budget.
 *
 * On RX_TIMEOUT any partial line stays buffered for the next call.
 *
 * @return RX_OK (line in h->_rx_buf), RX_TIMEOUT, RX_IO_ERROR.
 */
static int rx_line(ec200_handle_t *h, uint32_t *budget)
{
    for (;;) {
        uint8_t ch = 0U;
        int rc = rx_getc(h, budget, &ch);
        if (rc != RX_OK) {
            return rc;
        }
        if (rx_feed(h, ch)) {
            return RX_OK;
        }
    }
}

/**
 * @brief Classify a complete response line.
 *
 * @return 1 = success terminal ("OK"), 2 = error terminal, 0 = neither.
 */
static int is_terminal(ec200_handle_t *h, const char *line)
{
    if (strcmp(line, "OK") == 0) {
        return 1;
    }
    if (strcmp(line, "ERROR") == 0) {
        return 2;
    }
    if (strncmp(line, "+CME ERROR:", 11) == 0) {
        h->_last_cme_error = atoi(line + 11);
        return 2;
    }
    if (strncmp(line, "+CMS ERROR:", 11) == 0) {
        h->_last_cms_error = atoi(line + 11);
        return 2;
    }
    return 0;
}

/** Map an error-terminal line to the corresponding status code. */
static ec200_status_t map_error_terminal(const ec200_handle_t *h)
{
    if (h->_last_cme_error >= 0) {
        return EC200_ERR_CME;
    }
    if (h->_last_cms_error >= 0) {
        return EC200_ERR_CMS;
    }
    return EC200_ERR_MODULE;
}

/**
 * @brief Dispatch @p line to a registered URC handler if its prefix matches.
 * @return true when the line was consumed by a handler.
 */
static bool dispatch_registered_urc(ec200_handle_t *h, const char *line)
{
    for (size_t i = 0; i < EC200_MAX_URC_HANDLERS; i++) {
        const ec200_urc_entry_t *e = &h->_urc_table[i];
        if (e->prefix != NULL &&
            strncmp(line, e->prefix, strlen(e->prefix)) == 0) {
            /* register() rejects NULL handlers, so this always holds. */
            if (e->handler != NULL) { /* GCOVR_EXCL_BR_LINE */
                e->handler(line, e->ctx);
            }
            return true;
        }
    }
    return false;
}

/** Copy a matched line into the caller's buffer (no zero-fill tail). */
static void copy_line(const char *line, char *dst, size_t dst_sz)
{
    if (dst == NULL || dst_sz == 0U) {
        return;
    }
    size_t len = strlen(line);
    if (len >= dst_sz) {
        len = dst_sz - 1U;
    }
    memcpy(dst, line, len);
    dst[len] = '\0';
}

/**
 * @brief Reset per-transaction error state, frame @p cmd, and transmit it.
 */
static ec200_status_t at_transmit(ec200_handle_t *h, const char *cmd)
{
    h->_last_cme_error = -1;
    h->_last_cms_error = -1;

    int cmdlen = snprintf(h->_tx_buf, sizeof(h->_tx_buf), "%s\r", cmd);
    /* cmdlen < 0 cannot happen for this format string. */
    if (cmdlen < 0 || (size_t)cmdlen >= sizeof(h->_tx_buf)) { /* GCOVR_EXCL_BR_LINE */
        return EC200_ERR_OVERFLOW;
    }
    return ec200_at_write_raw(h, (const uint8_t *)h->_tx_buf,
                              (uint16_t)cmdlen);
}

/**
 * @brief Shared receive loop for command transactions.
 *
 * Reads lines until a terminal result code.  When @p prefix is non-NULL the
 * first matching line is copied to @p resp_buf; with @p stop_on_prefix the
 * function returns immediately after the match (raw-data commands).
 * Without a prefix, non-terminal lines are accumulated into @p resp_buf.
 */
static ec200_status_t receive_transaction(ec200_handle_t *h,
                                          const char     *prefix,
                                          bool            stop_on_prefix,
                                          char           *resp_buf,
                                          size_t          resp_buf_sz,
                                          uint32_t        timeout_ms)
{
    uint32_t budget     = timeout_ms;
    size_t   prefix_len = (prefix != NULL) ? strlen(prefix) : 0U;
    size_t   resp_pos   = 0U;
    bool     matched    = false;

    if (resp_buf != NULL && resp_buf_sz > 0U) {
        resp_buf[0] = '\0';
    }

    for (unsigned lines = 0U; lines < EC200_AT_MAX_LINES; lines++) {
        int rc = rx_line(h, &budget);
        if (rc == RX_IO_ERROR) {
            return EC200_ERR_IO;
        }
        if (rc == RX_TIMEOUT) {
            return EC200_ERR_TIMEOUT;
        }

        const char *line = h->_rx_buf;
        if (line[0] == '\0') {
            continue; /* blank separator line */
        }

        if (prefix != NULL && !matched &&
            strncmp(line, prefix, prefix_len) == 0) {
            matched = true;
            copy_line(line, resp_buf, resp_buf_sz);
            if (stop_on_prefix) {
                return EC200_OK; /* raw data follows — leave it unread */
            }
            continue;
        }

        int term = is_terminal(h, line);
        if (term == 1) {
            if (prefix != NULL && !matched) {
                return EC200_ERR_PARSE; /* OK arrived without the prefix */
            }
            return EC200_OK;
        }
        if (term == 2) {
            return map_error_terminal(h);
        }

        if (dispatch_registered_urc(h, line)) {
            continue; /* async URC mid-command — not response data */
        }

        /* Accumulate body lines (prefix-less transactions only). */
        if (prefix == NULL && resp_buf != NULL &&
            resp_pos + strlen(line) + 2U < resp_buf_sz) {
            if (resp_pos > 0U) {
                resp_buf[resp_pos++] = '\n';
            }
            size_t llen = strlen(line);
            memcpy(&resp_buf[resp_pos], line, llen);
            resp_pos += llen;
            resp_buf[resp_pos] = '\0';
        }
    }

    return EC200_ERR_OVERFLOW; /* line-storm guard tripped */
}

/* -------------------------------------------------------------------------
 * Command transactions
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_at_send(ec200_handle_t *h,
                             const char     *cmd,
                             char           *resp_buf,
                             size_t          resp_buf_sz,
                             uint32_t        timeout_ms)
{
    if (!handle_ready(h) || cmd == NULL) {
        return EC200_ERR_NOT_READY;
    }
    ec200_status_t st = at_transmit(h, cmd);
    if (st != EC200_OK) {
        return st;
    }
    return receive_transaction(h, NULL, false, resp_buf, resp_buf_sz,
                               timeout_ms);
}

ec200_status_t ec200_at_send_wait(ec200_handle_t *h,
                                  const char     *cmd,
                                  const char     *expected_prefix,
                                  char           *resp_buf,
                                  size_t          resp_buf_sz,
                                  uint32_t        timeout_ms)
{
    if (!handle_ready(h) || cmd == NULL || expected_prefix == NULL) {
        return EC200_ERR_NOT_READY;
    }
    ec200_status_t st = at_transmit(h, cmd);
    if (st != EC200_OK) {
        return st;
    }
    return receive_transaction(h, expected_prefix, false, resp_buf,
                               resp_buf_sz, timeout_ms);
}

ec200_status_t ec200_at_send_expect(ec200_handle_t *h,
                                    const char     *cmd,
                                    const char     *expected_prefix,
                                    char           *resp_buf,
                                    size_t          resp_buf_sz,
                                    uint32_t        timeout_ms)
{
    if (!handle_ready(h) || cmd == NULL || expected_prefix == NULL) {
        return EC200_ERR_NOT_READY;
    }
    ec200_status_t st = at_transmit(h, cmd);
    if (st != EC200_OK) {
        return st;
    }
    return receive_transaction(h, expected_prefix, true, resp_buf,
                               resp_buf_sz, timeout_ms);
}

ec200_status_t ec200_at_send_await_urc(ec200_handle_t *h,
                                       const char     *cmd,
                                       const char     *urc_prefix,
                                       char           *urc_buf,
                                       size_t          urc_buf_sz,
                                       uint32_t        ok_timeout,
                                       uint32_t        urc_timeout)
{
    if (!handle_ready(h) || cmd == NULL || urc_prefix == NULL) {
        return EC200_ERR_NOT_READY;
    }
    ec200_status_t st = at_transmit(h, cmd);
    if (st != EC200_OK) {
        return st;
    }

    /*
     * Phase 1: wait for the immediate "OK".  Some firmware emits the result
     * line before the OK, so a prefix match here already completes the call.
     */
    st = receive_transaction(h, urc_prefix, true, urc_buf, urc_buf_sz,
                             ok_timeout);
    if (st == EC200_OK) {
        /* Result line arrived before the OK (non-standard order): consume
         * the trailing OK, if any, so the stream stays synchronised. */
        (void)ec200_at_wait_final(h, EC200_AT_TIMEOUT_SHORT);
        return EC200_OK;
    }
    if (st != EC200_ERR_PARSE) {
        return st; /* real failure (timeout / IO / CME / CMS / MODULE) */
    }

    /* Phase 2 (normal path): OK consumed, now wait for the result URC. */
    return ec200_at_wait_prefix(h, urc_prefix, urc_buf, urc_buf_sz,
                                urc_timeout);
}

ec200_status_t ec200_at_send_prompt(ec200_handle_t *h,
                                    const char     *cmd,
                                    uint32_t        timeout_ms)
{
    if (!handle_ready(h) || cmd == NULL) {
        return EC200_ERR_NOT_READY;
    }
    ec200_status_t st = at_transmit(h, cmd);
    if (st != EC200_OK) {
        return st;
    }

    uint32_t budget = timeout_ms;
    for (;;) {
        uint8_t ch = 0U;
        int rc = rx_getc(h, &budget, &ch);
        if (rc == RX_IO_ERROR) {
            return EC200_ERR_IO;
        }
        if (rc == RX_TIMEOUT) {
            return EC200_ERR_TIMEOUT;
        }

        if (ch == (uint8_t)'>' && h->_rx_len == 0U) {
            /* Prompt found at start-of-line; swallow the trailing space
             * ("> ") if it is already available. */
            uint8_t sp     = 0U;
            uint32_t peek  = 0U;
            if (rx_getc(h, &peek, &sp) == RX_OK && sp != (uint8_t)' ') {
                (void)rx_feed(h, sp); /* keep unexpected byte for later */
            }
            return EC200_OK;
        }

        if (rx_feed(h, ch)) {
            const char *line = h->_rx_buf;
            if (line[0] == '\0') {
                continue;
            }
            int term = is_terminal(h, line);
            if (term == 2) {
                return map_error_terminal(h);
            }
            if (term == 1) {
                return EC200_ERR_PARSE; /* OK instead of prompt */
            }
            (void)dispatch_registered_urc(h, line);
        }
    }
}

/* -------------------------------------------------------------------------
 * Receive-only waits
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_at_wait_prefix(ec200_handle_t *h,
                                    const char     *prefix,
                                    char           *resp_buf,
                                    size_t          resp_buf_sz,
                                    uint32_t        timeout_ms)
{
    if (!handle_ready(h) || prefix == NULL) {
        return EC200_ERR_NOT_READY;
    }

    uint32_t budget     = timeout_ms;
    size_t   prefix_len = strlen(prefix);

    for (unsigned lines = 0U; lines < EC200_AT_MAX_LINES; lines++) {
        int rc = rx_line(h, &budget);
        if (rc == RX_IO_ERROR) {
            return EC200_ERR_IO;
        }
        if (rc == RX_TIMEOUT) {
            return EC200_ERR_TIMEOUT;
        }

        const char *line = h->_rx_buf;
        if (line[0] == '\0') {
            continue;
        }
        if (strncmp(line, prefix, prefix_len) == 0) {
            copy_line(line, resp_buf, resp_buf_sz);
            return EC200_OK;
        }
        int term = is_terminal(h, line);
        if (term == 2) {
            return map_error_terminal(h);
        }
        if (term == 1) {
            continue; /* stray OK while waiting for a URC — ignore */
        }
        (void)dispatch_registered_urc(h, line);
    }
    return EC200_ERR_OVERFLOW;
}

ec200_status_t ec200_at_wait_final(ec200_handle_t *h, uint32_t timeout_ms)
{
    if (!handle_ready(h)) {
        return EC200_ERR_NOT_READY;
    }

    uint32_t budget = timeout_ms;
    for (unsigned lines = 0U; lines < EC200_AT_MAX_LINES; lines++) {
        int rc = rx_line(h, &budget);
        if (rc == RX_IO_ERROR) {
            return EC200_ERR_IO;
        }
        if (rc == RX_TIMEOUT) {
            return EC200_ERR_TIMEOUT;
        }

        const char *line = h->_rx_buf;
        if (line[0] == '\0') {
            continue;
        }
        int term = is_terminal(h, line);
        if (term == 1) {
            return EC200_OK;
        }
        if (term == 2) {
            return map_error_terminal(h);
        }
        (void)dispatch_registered_urc(h, line);
    }
    return EC200_ERR_OVERFLOW;
}

/* -------------------------------------------------------------------------
 * Raw I/O
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_at_write_raw(ec200_handle_t *h,
                                  const uint8_t  *data,
                                  uint16_t        len)
{
    if (!handle_ready(h) || data == NULL) {
        return EC200_ERR_NOT_READY;
    }

    uint16_t sent   = 0U;
    uint32_t budget = EC200_AT_WRITE_TIMEOUT_MS;

    while (sent < len) {
        int n = h->write(&data[sent], (uint16_t)(len - sent), h->user_ctx);
        if (n < 0) {
            return EC200_ERR_IO;
        }
        if (n == 0) {
            /* Transport backpressure: wait briefly, bounded overall. */
            if (budget == 0U) {
                return EC200_ERR_TIMEOUT;
            }
            h->delay_ms(5U, h->user_ctx);
            budget = (budget >= 5U) ? (budget - 5U) : 0U;
            continue;
        }
        if ((uint16_t)n > (uint16_t)(len - sent)) {
            return EC200_ERR_IO; /* defensive: nonsensical callback result */
        }
        sent = (uint16_t)(sent + (uint16_t)n);
    }
    return EC200_OK;
}

/**
 * @brief Hand out bytes already sitting in the line-assembly buffer.
 *
 * Only relevant when a raw-data phase begins while a partial (desynchronised)
 * line was buffered; keeps raw reads from silently skipping those bytes.
 */
static uint16_t rx_take_buffered(ec200_handle_t *h,
                                 uint8_t        *buf,
                                 uint16_t        len)
{
    uint16_t n = (h->_rx_len < len) ? h->_rx_len : len;
    if (n > 0U) {
        memcpy(buf, h->_rx_buf, n);
        memmove(h->_rx_buf, &h->_rx_buf[n], (size_t)(h->_rx_len - n));
        h->_rx_len = (uint16_t)(h->_rx_len - n);
    }
    return n;
}

ec200_status_t ec200_at_read_raw(ec200_handle_t *h,
                                 uint8_t        *buf,
                                 uint16_t        len,
                                 uint32_t        timeout_ms,
                                 uint16_t       *bytes_read)
{
    if (!handle_ready(h) || buf == NULL || bytes_read == NULL || len == 0U) {
        return EC200_ERR_NOT_READY;
    }

    uint16_t got = rx_take_buffered(h, buf, len);
    if (got > 0U) {
        *bytes_read = got;
        return EC200_OK;
    }

    uint32_t budget = timeout_ms;
    for (;;) {
        uint32_t slice = (budget < EC200_AT_POLL_SLICE_MS)
                           ? budget : EC200_AT_POLL_SLICE_MS;
        int n = h->read(buf, len, slice, h->user_ctx);
        if (n < 0) {
            *bytes_read = 0U;
            return EC200_ERR_IO;
        }
        if (n > 0) {
            *bytes_read = (uint16_t)n;
            return EC200_OK;
        }
        if (budget == 0U) {
            *bytes_read = 0U;
            return EC200_ERR_TIMEOUT;
        }
        budget -= slice;
    }
}

ec200_status_t ec200_at_read_exact(ec200_handle_t *h,
                                   uint8_t        *buf,
                                   uint16_t        len,
                                   uint32_t        timeout_ms,
                                   uint16_t       *bytes_read)
{
    if (!handle_ready(h) || buf == NULL || bytes_read == NULL) {
        return EC200_ERR_NOT_READY;
    }

    uint16_t got    = rx_take_buffered(h, buf, len);
    uint32_t budget = timeout_ms;

    while (got < len) {
        uint32_t slice = (budget < EC200_AT_POLL_SLICE_MS)
                           ? budget : EC200_AT_POLL_SLICE_MS;
        int n = h->read(&buf[got], (uint16_t)(len - got), slice, h->user_ctx);
        if (n < 0) {
            *bytes_read = got;
            return EC200_ERR_IO;
        }
        if (n > 0) {
            if ((uint16_t)n > (uint16_t)(len - got)) {
                *bytes_read = got;
                return EC200_ERR_IO; /* defensive */
            }
            got = (uint16_t)(got + (uint16_t)n);
            continue;
        }
        if (budget == 0U) {
            *bytes_read = got;
            return EC200_ERR_TIMEOUT;
        }
        budget -= slice;
    }

    *bytes_read = got;
    return EC200_OK;
}

/* -------------------------------------------------------------------------
 * URC handling
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_at_register_urc(ec200_handle_t       *h,
                                     const char           *prefix,
                                     ec200_urc_handler_fn  handler,
                                     void                 *ctx)
{
    if (h == NULL || prefix == NULL || prefix[0] == '\0' || handler == NULL) {
        return EC200_ERR_PARAM;
    }
    /* Replace an existing registration for the same prefix. */
    for (size_t i = 0; i < EC200_MAX_URC_HANDLERS; i++) {
        if (h->_urc_table[i].prefix != NULL &&
            strcmp(h->_urc_table[i].prefix, prefix) == 0) {
            h->_urc_table[i].handler = handler;
            h->_urc_table[i].ctx     = ctx;
            return EC200_OK;
        }
    }
    for (size_t i = 0; i < EC200_MAX_URC_HANDLERS; i++) {
        if (h->_urc_table[i].prefix == NULL) {
            h->_urc_table[i].prefix  = prefix;
            h->_urc_table[i].handler = handler;
            h->_urc_table[i].ctx     = ctx;
            return EC200_OK;
        }
    }
    return EC200_ERR_OVERFLOW;
}

ec200_status_t ec200_at_unregister_urc(ec200_handle_t *h, const char *prefix)
{
    if (h == NULL || prefix == NULL) {
        return EC200_ERR_PARAM;
    }
    for (size_t i = 0; i < EC200_MAX_URC_HANDLERS; i++) {
        if (h->_urc_table[i].prefix != NULL &&
            strcmp(h->_urc_table[i].prefix, prefix) == 0) {
            h->_urc_table[i].prefix  = NULL;
            h->_urc_table[i].handler = NULL;
            h->_urc_table[i].ctx     = NULL;
            return EC200_OK;
        }
    }
    return EC200_ERR_PARAM;
}

ec200_status_t ec200_at_poll_urc(ec200_handle_t *h, uint32_t timeout_ms)
{
    if (!handle_ready(h)) {
        return EC200_ERR_NOT_READY;
    }

    uint32_t budget = timeout_ms;
    for (unsigned lines = 0U; lines < EC200_AT_MAX_LINES; lines++) {
        int rc = rx_line(h, &budget);
        if (rc == RX_IO_ERROR) {
            return EC200_ERR_IO;
        }
        if (rc == RX_TIMEOUT) {
            return EC200_OK; /* nothing pending is not an error */
        }

        const char *line = h->_rx_buf;
        if (line[0] == '\0') {
            continue;
        }
        if (!dispatch_registered_urc(h, line) && h->urc_handler != NULL) {
            h->urc_handler(line, h->user_ctx);
        }
    }
    return EC200_OK;
}

/* -------------------------------------------------------------------------
 * Response parsing utilities
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_at_parse_int_field(const char *line,
                                        unsigned    index,
                                        int        *value)
{
    if (line == NULL || value == NULL) {
        return EC200_ERR_PARSE;
    }

    const char *p     = strchr(line, ':');
    p                 = (p != NULL) ? (p + 1) : line; /* GCOVR_EXCL_BR_LINE */
    unsigned    field = 0U;
    bool        quoted = false;

    while (*p == ' ') {
        p++;
    }
    while (field < index) {
        if (*p == '\0') {
            return EC200_ERR_PARSE;
        }
        if (*p == '"') {
            quoted = !quoted;
        } else if (*p == ',' && !quoted) {
            field++;
        }
        p++;
    }
    while (*p == ' ') {
        p++;
    }
    if (*p != '-' && (*p < '0' || *p > '9')) {
        return EC200_ERR_PARSE;
    }
    *value = atoi(p);
    return EC200_OK;
}

/* -------------------------------------------------------------------------
 * Error introspection
 * ------------------------------------------------------------------------- */

int ec200_at_last_cme_error(const ec200_handle_t *h)
{
    return (h != NULL) ? h->_last_cme_error : -1;
}

int ec200_at_last_cms_error(const ec200_handle_t *h)
{
    return (h != NULL) ? h->_last_cms_error : -1;
}
