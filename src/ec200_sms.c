/**
 * @file ec200_sms.c
 * @brief SMS send/receive implementation.
 */

#include "ec200_sms.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SMS_CTRL_Z  (0x1AU)

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/* Map stat string (from AT+CMGL / AT+CMGR) to ec200_sms_stat_t */
static ec200_sms_stat_t stat_from_string(const char *s)
{
    if (strncmp(s, "REC UNREAD", 10) == 0) return EC200_SMS_STAT_REC_UNREAD;
    if (strncmp(s, "REC READ",    8) == 0) return EC200_SMS_STAT_REC_READ;
    if (strncmp(s, "STO UNSENT", 10) == 0) return EC200_SMS_STAT_STO_UNSENT;
    if (strncmp(s, "STO SENT",    8) == 0) return EC200_SMS_STAT_STO_SENT;
    return EC200_SMS_STAT_ALL;
}

/**
 * @brief Extract the next double-quoted string starting at *cursor.
 *
 * On success *cursor points just past the closing quote.
 *
 * @return true when a quoted string was found and copied (truncated to fit).
 */
static bool extract_quoted(const char **cursor, char *out, size_t out_sz)
{
    const char *p = strchr(*cursor, '"');
    if (p == NULL) {
        return false;
    }
    const char *q = strchr(p + 1, '"');
    if (q == NULL) {
        return false;
    }
    size_t len = (size_t)(q - p - 1);
    if (len >= out_sz) {
        len = out_sz - 1U;
    }
    memcpy(out, p + 1, len);
    out[len] = '\0';
    *cursor = q + 1;
    return true;
}

/* Parse a +CMGR / +CMGL header line into an ec200_sms_message_t */
static void parse_cmgr_header(const char *line, ec200_sms_message_t *msg)
{
    /* +CMGR: "REC READ","+1234567890",,"21/01/01,12:00:00+00" */
    /* +CMGL: <index>,"REC READ","+1234567890",,"21/01/01,12:00:00+00" */
    const char *cursor = line;

    /* Optional index at the start (CMGL format).  Callers only pass lines
     * that matched "+CMGR:"/"+CMGL:", so the ':' is always present, and
     * strtol always sets endptr when given a non-NULL pointer. */
    const char *colon = strchr(line, ':');
    if (colon != NULL) { /* GCOVR_EXCL_BR_LINE */
        char *endptr = NULL;
        long idx = strtol(colon + 1, &endptr, 10);
        if (endptr != NULL && endptr != colon + 1) { /* GCOVR_EXCL_BR_LINE */
            msg->index = (int)idx;
            cursor = endptr;
        } else {
            cursor = colon + 1;
        }
    }

    char stat_str[16] = {0};
    if (extract_quoted(&cursor, stat_str, sizeof(stat_str))) {
        msg->stat = stat_from_string(stat_str);
    }
    (void)extract_quoted(&cursor, msg->sender, sizeof(msg->sender));

    /* Skip the (usually empty) alpha field, then take the timestamp.  The
     * alpha field may be entirely absent (",,"), in which case the next
     * quoted string IS the timestamp — detect via a digit-led string. */
    char field[sizeof(msg->timestamp)] = {0};
    if (extract_quoted(&cursor, field, sizeof(field))) {
        if (field[0] >= '0' && field[0] <= '9') {
            memcpy(msg->timestamp, field, sizeof(field));
        } else {
            (void)extract_quoted(&cursor, msg->timestamp,
                                 sizeof(msg->timestamp));
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_sms_set_format(ec200_handle_t     *h,
                                    ec200_sms_format_t  format)
{
    if (format != EC200_SMS_FORMAT_PDU && format != EC200_SMS_FORMAT_TEXT) {
        return EC200_ERR_PARAM;
    }
    char cmd[16];
    (void)snprintf(cmd, sizeof(cmd), "AT+CMGF=%d", (int)format);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_sms_send(ec200_handle_t *h,
                              const char     *number,
                              const char     *text)
{
    if (!number || number[0] == '\0' || !text) {
        return EC200_ERR_PARAM;
    }
    if (strlen(number) >= EC200_MAX_PHONE_NUM_LEN) {
        return EC200_ERR_PARAM;
    }

    size_t text_len = strlen(text);
    if (text_len > EC200_MAX_SMS_TEXT_LEN) {
        return EC200_ERR_PARAM; /* refuse to silently truncate */
    }
    if (memchr(text, SMS_CTRL_Z, text_len) != NULL) {
        return EC200_ERR_PARAM; /* Ctrl-Z would submit the message early */
    }

    char cmd[EC200_MAX_PHONE_NUM_LEN + 16];
    (void)snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);

    ec200_status_t st = ec200_at_send_prompt(h, cmd, EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    st = ec200_at_write_raw(h, (const uint8_t *)text, (uint16_t)text_len);
    if (st != EC200_OK) {
        return st;
    }

    uint8_t ctrlz = SMS_CTRL_Z;
    st = ec200_at_write_raw(h, &ctrlz, 1);
    if (st != EC200_OK) {
        return st;
    }

    /* "+CMGS: <mr>" followed by "OK". */
    char resp[64];
    st = ec200_at_wait_prefix(h, "+CMGS:", resp, sizeof(resp),
                              EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    return ec200_at_wait_final(h, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_sms_read(ec200_handle_t      *h,
                              int                  index,
                              ec200_sms_message_t *msg)
{
    if (!msg || index < 0) {
        return EC200_ERR_PARAM;
    }
    memset(msg, 0, sizeof(*msg));
    msg->index = index;

    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", index);

    /*
     * Collect the full response ("+CMGR: <header>\n<body lines>") in one
     * transaction so the body cannot be lost between reads.
     */
    char resp[EC200_RX_BUFFER_SIZE];
    ec200_status_t st = ec200_at_send(h, cmd, resp, sizeof(resp),
                                      EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    char *hdr = strstr(resp, "+CMGR:");
    if (hdr == NULL) {
        return EC200_ERR_PARSE; /* empty slot or unexpected format */
    }

    char *body = strchr(hdr, '\n');
    if (body != NULL) {
        *body = '\0';
        body++;
    }

    parse_cmgr_header(hdr, msg);
    msg->index = index; /* header of +CMGR carries no index */

    if (body != NULL) {
        size_t blen = strlen(body);
        if (blen > EC200_MAX_SMS_TEXT_LEN) {
            blen = EC200_MAX_SMS_TEXT_LEN;
        }
        memcpy(msg->text, body, blen);
        msg->text[blen] = '\0';
    }

    return EC200_OK;
}

ec200_status_t ec200_sms_list(ec200_handle_t      *h,
                              ec200_sms_stat_t      stat,
                              ec200_sms_message_t  *msgs,
                              uint8_t               max_msgs,
                              uint8_t              *count_out)
{
    if (!msgs || !count_out || max_msgs == 0) {
        return EC200_ERR_PARAM;
    }
    *count_out = 0;

    static const char * const stat_strings[] = {
        "REC UNREAD", "REC READ", "STO UNSENT", "STO SENT", "ALL"
    };
    if ((int)stat > 4) {
        return EC200_ERR_PARAM;
    }

    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+CMGL=\"%s\"",
                   stat_strings[(int)stat]);

    char resp[EC200_RX_BUFFER_SIZE];
    ec200_status_t st = ec200_at_send(h, cmd, resp, sizeof(resp),
                                      EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }

    /* Parse lines: +CMGL: <idx>,...\n<text>\n... */
    char *line = resp;
    while (*line && *count_out < max_msgs) {
        if (strncmp(line, "+CMGL:", 6) == 0) {
            ec200_sms_message_t *m = &msgs[*count_out];
            memset(m, 0, sizeof(*m));

            char *nl = strchr(line, '\n');
            if (nl) {
                *nl = '\0';
            }
            parse_cmgr_header(line, m);

            if (!nl) {
                break; /* header without body — response ended */
            }

            /* Body is the next line. */
            line = nl + 1;
            char *body_end = strchr(line, '\n');
            size_t blen = body_end ? (size_t)(body_end - line)
                                   : strlen(line);
            if (blen > EC200_MAX_SMS_TEXT_LEN) {
                blen = EC200_MAX_SMS_TEXT_LEN;
            }
            memcpy(m->text, line, blen);
            m->text[blen] = '\0';
            (*count_out)++;

            if (!body_end) {
                break;
            }
            line = body_end + 1;
        } else {
            char *nl = strchr(line, '\n');
            if (!nl) {
                break;
            }
            line = nl + 1;
        }
    }

    return EC200_OK;
}

ec200_status_t ec200_sms_delete(ec200_handle_t *h, int index)
{
    if (index < 0) {
        return EC200_ERR_PARAM;
    }
    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", index);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_sms_delete_all(ec200_handle_t *h, uint8_t flag)
{
    if (flag < 1 || flag > 4) {
        return EC200_ERR_PARAM;
    }
    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+CMGD=1,%u", (unsigned)flag);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_LONG);
}
