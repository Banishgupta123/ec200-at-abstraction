/**
 * @file ec200_at.c
 * @brief Low-level AT command transport layer implementation.
 */

#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/**
 * @brief Read one CR/LF-delimited line from the UART into @p line_buf.
 *
 * @return Number of characters in the line (excluding \\r\\n), or <0 on error.
 */
static int read_line(ec200_handle_t *h,
                     char           *line_buf,
                     size_t          line_buf_sz,
                     uint32_t        timeout_ms)
{
    size_t  pos = 0;
    uint8_t ch;

    while (pos < line_buf_sz - 1U) {
        int n = h->read(&ch, 1, timeout_ms, h->user_ctx);
        if (n <= 0) {
            /* Timeout or I/O error */
            return (pos > 0) ? (int)pos : -1;
        }
        if (ch == '\r') {
            continue; /* skip CR */
        }
        if (ch == '\n') {
            break; /* end of line */
        }
        line_buf[pos++] = (char)ch;
    }

    line_buf[pos] = '\0';
    return (int)pos;
}

/**
 * @brief Classify a response line as terminal or not.
 *
 * @return 1 if "OK", 2 if "ERROR" / "+CME" / "+CMS", 0 otherwise.
 */
static int is_terminal(ec200_handle_t *h, const char *line)
{
    if (strcmp(line, "OK") == 0) {
        return 1;
    }
    if (strcmp(line, "ERROR") == 0) {
        h->_last_cme_error = -1;
        h->_last_cms_error = -1;
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

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_at_send(ec200_handle_t *h,
                             const char     *cmd,
                             char           *resp_buf,
                             size_t          resp_buf_sz,
                             uint32_t        timeout_ms)
{
    if (!h || !h->_initialised || !cmd) {
        return EC200_ERR_NOT_READY;
    }

    /* Build and transmit the command line */
    int cmdlen = snprintf(h->_tx_buf, sizeof(h->_tx_buf), "%s\r\n", cmd);
    if (cmdlen < 0 || (size_t)cmdlen >= sizeof(h->_tx_buf)) {
        return EC200_ERR_OVERFLOW;
    }

    int n = h->write((const uint8_t *)h->_tx_buf, (uint16_t)cmdlen, h->user_ctx);
    if (n < 0) {
        return EC200_ERR_IO;
    }

    /* Accumulate response lines until terminal result code */
    if (resp_buf && resp_buf_sz > 0) {
        resp_buf[0] = '\0';
    }
    size_t resp_pos = 0;

    while (1) {
        char line[EC200_RX_BUFFER_SIZE];
        int  len = read_line(h, line, sizeof(line), timeout_ms);
        if (len < 0) {
            return EC200_ERR_TIMEOUT;
        }
        if (len == 0) {
            continue; /* empty line, skip */
        }

        int term = is_terminal(h, line);

        /* Accumulate non-terminal lines into caller's buffer */
        if (term == 0 && resp_buf && resp_pos + (size_t)len + 2U < resp_buf_sz) {
            if (resp_pos > 0) {
                resp_buf[resp_pos++] = '\n';
            }
            memcpy(resp_buf + resp_pos, line, (size_t)len);
            resp_pos += (size_t)len;
            resp_buf[resp_pos] = '\0';
        }

        if (term == 1) {
            return EC200_OK;
        }
        if (term == 2) {
            if (h->_last_cme_error >= 0) return EC200_ERR_CME;
            if (h->_last_cms_error >= 0) return EC200_ERR_CMS;
            return EC200_ERR_PARSE;
        }
    }
}

ec200_status_t ec200_at_send_wait(ec200_handle_t *h,
                                  const char     *cmd,
                                  const char     *expected_prefix,
                                  char           *resp_buf,
                                  size_t          resp_buf_sz,
                                  uint32_t        timeout_ms)
{
    if (!h || !h->_initialised || !cmd || !expected_prefix) {
        return EC200_ERR_NOT_READY;
    }

    /* Build and transmit the command */
    int cmdlen = snprintf(h->_tx_buf, sizeof(h->_tx_buf), "%s\r\n", cmd);
    if (cmdlen < 0 || (size_t)cmdlen >= sizeof(h->_tx_buf)) {
        return EC200_ERR_OVERFLOW;
    }

    int n = h->write((const uint8_t *)h->_tx_buf, (uint16_t)cmdlen, h->user_ctx);
    if (n < 0) {
        return EC200_ERR_IO;
    }

    size_t prefix_len = strlen(expected_prefix);

    while (1) {
        char line[EC200_RX_BUFFER_SIZE];
        int  len = read_line(h, line, sizeof(line), timeout_ms);
        if (len < 0) {
            return EC200_ERR_TIMEOUT;
        }
        if (len == 0) {
            continue;
        }

        /* Check for the expected prefix first */
        if (strncmp(line, expected_prefix, prefix_len) == 0) {
            if (resp_buf && resp_buf_sz > 0) {
                strncpy(resp_buf, line, resp_buf_sz - 1U);
                resp_buf[resp_buf_sz - 1U] = '\0';
            }
            /* Drain the remaining "OK" line but do not fail if it's missing */
            char drain[32];
            read_line(h, drain, sizeof(drain), 200U);
            return EC200_OK;
        }

        int term = is_terminal(h, line);
        if (term == 1) {
            return EC200_ERR_PARSE; /* OK arrived before expected prefix */
        }
        if (term == 2) {
            if (h->_last_cme_error >= 0) return EC200_ERR_CME;
            if (h->_last_cms_error >= 0) return EC200_ERR_CMS;
            return EC200_ERR_PARSE;
        }
    }
}

ec200_status_t ec200_at_write_raw(ec200_handle_t *h,
                                  const uint8_t  *data,
                                  uint16_t        len)
{
    if (!h || !h->_initialised || !data) {
        return EC200_ERR_NOT_READY;
    }
    int n = h->write(data, len, h->user_ctx);
    return (n >= 0) ? EC200_OK : EC200_ERR_IO;
}

ec200_status_t ec200_at_read_raw(ec200_handle_t *h,
                                 uint8_t        *buf,
                                 uint16_t        len,
                                 uint32_t        timeout_ms,
                                 uint16_t       *bytes_read)
{
    if (!h || !h->_initialised || !buf || !bytes_read) {
        return EC200_ERR_NOT_READY;
    }
    int n = h->read(buf, len, timeout_ms, h->user_ctx);
    if (n < 0) {
        *bytes_read = 0;
        return EC200_ERR_IO;
    }
    *bytes_read = (uint16_t)n;
    return EC200_OK;
}

ec200_status_t ec200_at_poll_urc(ec200_handle_t *h, uint32_t timeout_ms)
{
    if (!h || !h->_initialised) {
        return EC200_ERR_NOT_READY;
    }

    char line[EC200_RX_BUFFER_SIZE];
    int  len = read_line(h, line, sizeof(line), timeout_ms);
    if (len < 0) {
        return EC200_ERR_TIMEOUT;
    }
    if (len == 0) {
        return EC200_OK;
    }

    /* Dispatch to MQTT message handler if it is a +QMTRECV URC */
    if (h->mqtt_msg_cb && strncmp(line, "+QMTRECV:", 9) == 0) {
        ec200_mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));

        /* Parse: +QMTRECV: <client_idx>,<msg_id>,"<topic>","<payload>" */
        char *p = line + 9;
        /* skip client_idx, msg_id */
        p = strchr(p, '"');
        if (p) {
            char *q = strchr(p + 1, '"');
            if (q) {
                size_t tlen = (size_t)(q - p - 1);
                if (tlen >= sizeof(msg.topic)) tlen = sizeof(msg.topic) - 1U;
                memcpy(msg.topic, p + 1, tlen);
                msg.topic[tlen] = '\0';

                /* payload */
                p = strchr(q + 1, '"');
                if (p) {
                    q = strrchr(p + 1, '"');
                    if (q) {
                        size_t plen = (size_t)(q - p - 1);
                        if (plen > sizeof(msg.payload)) plen = sizeof(msg.payload);
                        memcpy(msg.payload, p + 1, plen);
                        msg.payload_len = (uint32_t)plen;
                    }
                }
            }
        }
        h->mqtt_msg_cb(&msg, h->user_ctx);
        return EC200_OK;
    }

    /* Dispatch to generic URC handler */
    if (h->urc_handler) {
        h->urc_handler(line, h->user_ctx);
    }

    return EC200_OK;
}

int ec200_at_last_cme_error(const ec200_handle_t *h)
{
    return h ? h->_last_cme_error : -1;
}

int ec200_at_last_cms_error(const ec200_handle_t *h)
{
    return h ? h->_last_cms_error : -1;
}
