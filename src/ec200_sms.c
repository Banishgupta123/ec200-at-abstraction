/**
 * @file ec200_sms.c
 * @brief SMS send/receive implementation.
 */

#include "ec200_sms.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/* Map stat integer (from AT+CMGL / AT+CMGR) to ec200_sms_stat_t */
static ec200_sms_stat_t stat_from_string(const char *s)
{
    if (strncmp(s, "REC UNREAD", 10) == 0) return EC200_SMS_STAT_REC_UNREAD;
    if (strncmp(s, "REC READ",    8) == 0) return EC200_SMS_STAT_REC_READ;
    if (strncmp(s, "STO UNSENT", 10) == 0) return EC200_SMS_STAT_STO_UNSENT;
    if (strncmp(s, "STO SENT",    8) == 0) return EC200_SMS_STAT_STO_SENT;
    return EC200_SMS_STAT_ALL;
}

/* Parse a +CMGR / +CMGL header line into an ec200_sms_message_t */
static void parse_cmgr_header(const char *line, ec200_sms_message_t *msg)
{
    /* +CMGR: "REC READ","+1234567890",,"21/01/01,12:00:00+00" */
    /* +CMGL: <index>,"REC READ","+1234567890",,"21/01/01,12:00:00+00" */
    const char *p = strchr(line, '"');
    if (!p) return;

    /* Check if we have an index at the start (CMGL format) */
    const char *colon = strchr(line, ':');
    if (colon) {
        char *endptr = NULL;
        long idx = strtol(colon + 1, &endptr, 10);
        if (endptr && endptr != colon + 1) {
            msg->index = (int)idx;
            p = strchr(endptr, '"');
            if (!p) return;
        }
    }

    /* stat */
    char stat_str[16] = {0};
    const char *q = strchr(p + 1, '"');
    if (q) {
        size_t slen = (size_t)(q - p - 1);
        if (slen < sizeof(stat_str)) {
            memcpy(stat_str, p + 1, slen);
            stat_str[slen] = '\0';
        }
        msg->stat = stat_from_string(stat_str);
        p = q + 1;
    }

    /* sender */
    p = strchr(p, '"');
    if (!p) return;
    q = strchr(p + 1, '"');
    if (q) {
        size_t slen = (size_t)(q - p - 1);
        if (slen >= sizeof(msg->sender)) slen = sizeof(msg->sender) - 1U;
        memcpy(msg->sender, p + 1, slen);
        msg->sender[slen] = '\0';
        p = q + 1;
    }

    /* skip alpha field (empty usually) */
    p = strchr(p, '"');
    if (!p) return;
    q = strchr(p + 1, '"');
    if (!q) return;
    p = q + 1;

    /* timestamp */
    p = strchr(p, '"');
    if (!p) return;
    q = strchr(p + 1, '"');
    if (q) {
        size_t tlen = (size_t)(q - p - 1);
        if (tlen >= sizeof(msg->timestamp)) tlen = sizeof(msg->timestamp) - 1U;
        memcpy(msg->timestamp, p + 1, tlen);
        msg->timestamp[tlen] = '\0';
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_sms_set_format(ec200_handle_t     *h,
                                    ec200_sms_format_t  format)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+CMGF=%d", (int)format);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_sms_send(ec200_handle_t *h,
                              const char     *number,
                              const char     *text)
{
    if (!number || !text) return EC200_ERR_PARAM;

    /* Set destination */
    char cmd[EC200_MAX_PHONE_NUM_LEN + 16];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);

    /* Send the CMGS command and wait for the ">" prompt */
    int cmdlen = snprintf(h->_tx_buf, sizeof(h->_tx_buf), "%s\r", cmd);
    if (cmdlen < 0 || (size_t)cmdlen >= sizeof(h->_tx_buf)) {
        return EC200_ERR_OVERFLOW;
    }
    int n = h->write((const uint8_t *)h->_tx_buf, (uint16_t)cmdlen, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    /* Wait for ">" prompt (the module may echo blank lines first) */
    uint8_t ch;
    bool got_prompt = false;
    for (int i = 0; i < 200; i++) {
        int r = h->read(&ch, 1, 100U, h->user_ctx);
        if (r > 0 && ch == '>') {
            got_prompt = true;
            break;
        }
    }
    if (!got_prompt) return EC200_ERR_TIMEOUT;

    /* Send text followed by Ctrl-Z (0x1A) to submit */
    size_t text_len = strlen(text);
    if (text_len > EC200_MAX_SMS_TEXT_LEN) text_len = EC200_MAX_SMS_TEXT_LEN;

    n = h->write((const uint8_t *)text, (uint16_t)text_len, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    uint8_t ctrlz = 0x1A;
    n = h->write(&ctrlz, 1, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    /* Wait for +CMGS: <mr> then OK */
    char resp[64];
    return ec200_at_send_wait(h, "", "+CMGS:",
                              resp, sizeof(resp),
                              EC200_AT_TIMEOUT_LONG);
}

ec200_status_t ec200_sms_read(ec200_handle_t      *h,
                              int                  index,
                              ec200_sms_message_t *msg)
{
    if (!msg) return EC200_ERR_PARAM;
    memset(msg, 0, sizeof(*msg));
    msg->index = index;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", index);

    char resp[EC200_RX_BUFFER_SIZE];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+CMGR:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    parse_cmgr_header(resp, msg);

    /* The next line (body) is not captured by ec200_at_send_wait().
     * Read it separately with a short timeout. */
    char body_line[EC200_MAX_SMS_TEXT_LEN + 1];
    int blen = h->read((uint8_t *)body_line,
                       (uint16_t)(sizeof(body_line) - 1U),
                       500U, h->user_ctx);
    if (blen > 0) {
        body_line[blen] = '\0';
        strncpy(msg->text, body_line, EC200_MAX_SMS_TEXT_LEN);
        msg->text[EC200_MAX_SMS_TEXT_LEN] = '\0';
    }

    return EC200_OK;
}

ec200_status_t ec200_sms_list(ec200_handle_t      *h,
                              ec200_sms_stat_t      stat,
                              ec200_sms_message_t  *msgs,
                              uint8_t               max_msgs,
                              uint8_t              *count_out)
{
    if (!msgs || !count_out) return EC200_ERR_PARAM;
    *count_out = 0;

    static const char * const stat_strings[] = {
        "REC UNREAD", "REC READ", "STO UNSENT", "STO SENT", "ALL"
    };
    if ((int)stat > 4) return EC200_ERR_PARAM;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGL=\"%s\"", stat_strings[(int)stat]);

    char resp[EC200_RX_BUFFER_SIZE];
    ec200_status_t st = ec200_at_send(h, cmd,
                                      resp, sizeof(resp),
                                      EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) return st;

    /* Parse lines: +CMGL: <idx>,...\n<text>\n... */
    char *line = resp;
    while (*line && *count_out < max_msgs) {
        if (strncmp(line, "+CMGL:", 6) == 0) {
            ec200_sms_message_t *m = &msgs[*count_out];
            memset(m, 0, sizeof(*m));
            parse_cmgr_header(line, m);

            /* advance to body line */
            char *nl = strchr(line, '\n');
            if (nl) {
                line = nl + 1;
                char *body_end = strchr(line, '\n');
                size_t blen;
                if (body_end) {
                    blen = (size_t)(body_end - line);
                    if (blen > 0 && line[blen - 1] == '\r') blen--;
                } else {
                    blen = strlen(line);
                }
                if (blen > EC200_MAX_SMS_TEXT_LEN) blen = EC200_MAX_SMS_TEXT_LEN;
                memcpy(m->text, line, blen);
                m->text[blen] = '\0';
                if (body_end) line = body_end + 1;
                else break;
            } else {
                break;
            }
            (*count_out)++;
        } else {
            char *nl = strchr(line, '\n');
            if (!nl) break;
            line = nl + 1;
        }
    }

    return EC200_OK;
}

ec200_status_t ec200_sms_delete(ec200_handle_t *h, int index)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", index);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_sms_delete_all(ec200_handle_t *h, uint8_t flag)
{
    if (flag < 1 || flag > 4) return EC200_ERR_PARAM;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGD=1,%u", (unsigned)flag);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_LONG);
}
