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

/* Wire mnemonic for each ec200_sms_mem_t, indexed by the enum value. */
static const char * const mem_strings[] = { "SM", "ME", "MT" };

#define SMS_MEM_COUNT  (sizeof(mem_strings) / sizeof(mem_strings[0]))

/* Map a storage mnemonic back to its enum; false when unrecognised. */
static bool mem_from_string(const char *s, ec200_sms_mem_t *out)
{
    for (size_t i = 0; i < SMS_MEM_COUNT; i++) {
        if (strcmp(s, mem_strings[i]) == 0) {
            *out = (ec200_sms_mem_t)i;
            return true;
        }
    }
    return false;
}

/*
 * Read six used/total integers out of a +CPMS line.
 *
 * The set- and query-forms differ: the reply to AT+CPMS=… is six bare
 * integers, while AT+CPMS? interleaves the storage names
 * ("ME",1,23,"ME",1,23,…).  @p stride/@p first describe which fields carry
 * the numbers, so one parser serves both.
 */
static ec200_status_t parse_cpms_usage(const char          *line,
                                       unsigned             first,
                                       unsigned             stride,
                                       ec200_sms_storage_t *out)
{
    ec200_sms_mem_usage_t *slots[3] = {
        &out->read_delete, &out->write_send, &out->receive
    };

    for (unsigned i = 0; i < 3U; i++) {
        int used  = 0;
        int total = 0;
        unsigned base = first + (i * stride);
        if (ec200_at_parse_int_field(line, base, &used) != EC200_OK ||
            ec200_at_parse_int_field(line, base + 1U, &total) != EC200_OK) {
            return EC200_ERR_PARSE;
        }
        if (used < 0 || total < 0) {
            return EC200_ERR_PARSE;
        }
        slots[i]->used  = (uint16_t)used;
        slots[i]->total = (uint16_t)total;
    }
    return EC200_OK;
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

/*
 * Shared body of AT+CMGS and AT+CMGW: both take a destination number, answer
 * with a ">" prompt, swallow the text up to a Ctrl-Z, and reply with a single
 * integer (message reference for CMGS, storage index for CMGW) before "OK".
 *
 * @param op          "CMGS" or "CMGW".
 * @param value_out   Receives the reported integer; NULL to ignore it.
 */
static ec200_status_t sms_submit(ec200_handle_t *h,
                                 const char     *op,
                                 const char     *number,
                                 const char     *text,
                                 int            *value_out)
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
    /* Scanning a byte range for Ctrl-Z, not copying a string: the checker's
     * null-terminator assumption does not apply.
     * NOLINTNEXTLINE(bugprone-not-null-terminated-result) */
    if (memchr(text, SMS_CTRL_Z, text_len) != NULL) {
        return EC200_ERR_PARAM; /* Ctrl-Z would submit the message early */
    }

    char cmd[EC200_MAX_PHONE_NUM_LEN + 16];
    (void)snprintf(cmd, sizeof(cmd), "AT+%s=\"%s\"", op, number);

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

    /* "+CMGS: <mr>" / "+CMGW: <index>" followed by "OK". */
    char prefix[12];
    (void)snprintf(prefix, sizeof(prefix), "+%s:", op);

    char resp[64];
    st = ec200_at_wait_prefix(h, prefix, resp, sizeof(resp),
                              EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }

    if (value_out != NULL) {
        /* The value is informational: a module that omits it still leaves the
         * message sent/stored, so a parse miss is not a failure. */
        if (ec200_at_parse_int_field(resp, 0, value_out) != EC200_OK) {
            *value_out = -1;
        }
    }
    return ec200_at_wait_final(h, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_sms_send(ec200_handle_t *h,
                              const char     *number,
                              const char     *text)
{
    return sms_submit(h, "CMGS", number, text, NULL);
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
    char resp[EC200_SMS_READ_BUF_LEN];
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

    char resp[EC200_SMS_LIST_BUF_LEN];
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

/* -------------------------------------------------------------------------
 * Storage selection (AT+CPMS)
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_sms_set_storage(ec200_handle_t      *h,
                                     ec200_sms_mem_t      read_delete,
                                     ec200_sms_mem_t      write_send,
                                     ec200_sms_mem_t      receive,
                                     ec200_sms_storage_t *usage_out)
{
    if ((unsigned)read_delete >= SMS_MEM_COUNT ||
        (unsigned)write_send  >= SMS_MEM_COUNT ||
        (unsigned)receive     >= SMS_MEM_COUNT) {
        return EC200_ERR_PARAM;
    }
    /* "MT" is the union of ME and SM: readable, but not a place to put a
     * message, so it cannot be the write/send store. */
    if (write_send == EC200_SMS_MEM_MT) {
        return EC200_ERR_PARAM;
    }

    char cmd[48];
    (void)snprintf(cmd, sizeof(cmd), "AT+CPMS=\"%s\",\"%s\",\"%s\"",
                   mem_strings[(int)read_delete],
                   mem_strings[(int)write_send],
                   mem_strings[(int)receive]);

    char resp[96];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+CPMS:", resp,
                                           sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }
    if (usage_out == NULL) {
        return EC200_OK;
    }
    /* Set-form reply: six bare integers, used/total per area. */
    return parse_cpms_usage(resp, 0U, 2U, usage_out);
}

ec200_status_t ec200_sms_get_storage(ec200_handle_t      *h,
                                     ec200_sms_storage_t *usage_out)
{
    if (usage_out == NULL) {
        return EC200_ERR_PARAM;
    }

    char resp[96];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CPMS?", "+CPMS:", resp,
                                           sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }
    /* Query-form reply: "ME",used,total repeated — numbers start at field 1
     * and every area occupies three fields. */
    return parse_cpms_usage(resp, 1U, 3U, usage_out);
}

/* -------------------------------------------------------------------------
 * Service centre address (AT+CSCA)
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_sms_set_smsc(ec200_handle_t *h, const char *number)
{
    if (!number || number[0] == '\0' ||
        strlen(number) >= EC200_MAX_PHONE_NUM_LEN) {
        return EC200_ERR_PARAM;
    }
    char cmd[EC200_MAX_PHONE_NUM_LEN + 16];
    (void)snprintf(cmd, sizeof(cmd), "AT+CSCA=\"%s\"", number);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_sms_get_smsc(ec200_handle_t *h, char *out, size_t out_sz)
{
    if (!out || out_sz == 0U) {
        return EC200_ERR_PARAM;
    }
    out[0] = '\0';

    char resp[96];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CSCA?", "+CSCA:", resp,
                                           sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    /* Not extract_quoted(): that truncates to fit, and a half-copied service
     * centre number is worse than no answer — a caller that read it and wrote
     * it back with ec200_sms_set_smsc() would misconfigure the module. */
    const char *open  = strchr(resp, '"');
    const char *close = (open != NULL) ? strchr(open + 1, '"') : NULL;
    if (close == NULL) {
        return EC200_ERR_PARSE;
    }
    size_t len = (size_t)(close - open - 1);
    if (len >= out_sz) {
        return EC200_ERR_OVERFLOW;
    }
    memcpy(out, open + 1, len);
    out[len] = '\0';
    return EC200_OK;
}

/* -------------------------------------------------------------------------
 * Store and send-from-storage (AT+CMGW / AT+CMSS)
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_sms_write(ec200_handle_t *h,
                               const char     *number,
                               const char     *text,
                               int            *index_out)
{
    return sms_submit(h, "CMGW", number, text, index_out);
}

ec200_status_t ec200_sms_send_stored(ec200_handle_t *h,
                                     int             index,
                                     int            *mr_out)
{
    if (index < 0) {
        return EC200_ERR_PARAM;
    }
    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+CMSS=%d", index);

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+CMSS:", resp,
                                           sizeof(resp),
                                           EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    if (mr_out != NULL &&
        ec200_at_parse_int_field(resp, 0, mr_out) != EC200_OK) {
        *mr_out = -1; /* informational only — the message still went out */
    }
    return EC200_OK;
}

/* -------------------------------------------------------------------------
 * New-message indication (AT+CNMI)
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_sms_set_indication(ec200_handle_t         *h,
                                        const ec200_sms_cnmi_t *cfg)
{
    if (cfg == NULL) {
        return EC200_ERR_PARAM;
    }
    if (cfg->mode > 2U || cfg->mt > 3U || cfg->bm > 3U ||
        cfg->ds > 2U || cfg->bfr > 1U) {
        return EC200_ERR_PARAM;
    }

    char cmd[40];
    (void)snprintf(cmd, sizeof(cmd), "AT+CNMI=%u,%u,%u,%u,%u",
                   (unsigned)cfg->mode, (unsigned)cfg->mt, (unsigned)cfg->bm,
                   (unsigned)cfg->ds, (unsigned)cfg->bfr);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_sms_get_indication(ec200_handle_t   *h,
                                        ec200_sms_cnmi_t *cfg)
{
    if (cfg == NULL) {
        return EC200_ERR_PARAM;
    }

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CNMI?", "+CNMI:", resp,
                                           sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    /* Parse all five fields before storing any, so a malformed reply leaves
     * the caller's struct untouched rather than half-updated. */
    int v[5];
    for (unsigned i = 0; i < 5U; i++) {
        if (ec200_at_parse_int_field(resp, i, &v[i]) != EC200_OK) {
            return EC200_ERR_PARSE;
        }
        if (v[i] < 0 || v[i] > 255) {
            return EC200_ERR_PARSE;
        }
    }

    cfg->mode = (uint8_t)v[0];
    cfg->mt   = (uint8_t)v[1];
    cfg->bm   = (uint8_t)v[2];
    cfg->ds   = (uint8_t)v[3];
    cfg->bfr  = (uint8_t)v[4];
    return EC200_OK;
}

ec200_status_t ec200_sms_parse_notification(const char               *line,
                                            ec200_sms_notification_t *out)
{
    if (!line || !out) {
        return EC200_ERR_PARAM;
    }
    if (strncmp(line, "+CMTI:", 6) != 0) {
        return EC200_ERR_PARSE;
    }

    const char *cursor = line;
    char mem_str[8] = {0};
    if (!extract_quoted(&cursor, mem_str, sizeof(mem_str))) {
        return EC200_ERR_PARSE;
    }
    if (!mem_from_string(mem_str, &out->mem)) {
        return EC200_ERR_PARSE;
    }

    int index = 0;
    if (ec200_at_parse_int_field(line, 1, &index) != EC200_OK || index < 0) {
        return EC200_ERR_PARSE;
    }
    out->index = index;
    return EC200_OK;
}
