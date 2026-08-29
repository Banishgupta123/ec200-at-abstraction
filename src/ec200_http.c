/**
 * @file ec200_http.c
 * @brief HTTP client implementation (AT+QHTTP*).
 *
 * AT+QHTTPGET / AT+QHTTPPOST are asynchronous: the module replies "OK"
 * immediately and reports `+QHTTPGET:`/`+QHTTPPOST:` as a later URC.
 * AT+QHTTPREAD streams the body between a "CONNECT" line and a trailing
 * "OK", followed by a `+QHTTPREAD: <err>` URC.
 */

#include "ec200_http.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/** Upper bound on body bytes discarded after the caller's buffer is full. */
#define HTTP_MAX_DISCARD  (65536U)

/** Convert a millisecond timeout to the AT command's seconds parameter. */
static unsigned timeout_secs(uint32_t timeout_ms)
{
    uint32_t s = timeout_ms / 1000U;
    if (s < 1U) {
        s = 1U;
    }
    if (s > 65535U) {
        s = 65535U;
    }
    return (unsigned)s;
}

/** Parse "+QHTTPGET/POST: <err>,<status>,<len>" into @p resp. */
static ec200_status_t parse_http_result(const char           *line,
                                        ec200_http_response_t *resp)
{
    int err = 0, status_code = 0, clen = 0;
    if (ec200_at_parse_int_field(line, 0U, &err) != EC200_OK) {
        return EC200_ERR_PARSE;
    }
    if (err != 0) {
        return EC200_ERR_MODULE;
    }
    /* <status>/<len> are optional on some firmware error paths. */
    (void)ec200_at_parse_int_field(line, 1U, &status_code);
    (void)ec200_at_parse_int_field(line, 2U, &clen);

    resp->status_code    = (uint16_t)status_code;
    resp->content_length = (uint32_t)((clen > 0) ? clen : 0);
    return EC200_OK;
}

ec200_status_t ec200_http_set_context(ec200_handle_t *h, uint8_t ctx_id)
{
    if (ctx_id < 1 || ctx_id > 16) {
        return EC200_ERR_PARAM;
    }
    char cmd[48];
    (void)snprintf(cmd, sizeof(cmd), "AT+QHTTPCFG=\"contextid\",%u",
                   (unsigned)ctx_id);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_http_set_url(ec200_handle_t *h, const char *url)
{
    if (!url) {
        return EC200_ERR_PARAM;
    }

    size_t url_len = strlen(url);
    if (url_len == 0 || url_len > EC200_MAX_URL_LEN) {
        return EC200_ERR_PARAM;
    }

    /* AT+QHTTPURL=<url_len>,<timeout> → CONNECT → <url> → OK */
    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QHTTPURL=%u,80", (unsigned)url_len);

    char connect_buf[32];
    ec200_status_t st = ec200_at_send_expect(h, cmd, "CONNECT",
                                             connect_buf, sizeof(connect_buf),
                                             EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    st = ec200_at_write_raw(h, (const uint8_t *)url, (uint16_t)url_len);
    if (st != EC200_OK) {
        return st;
    }

    return ec200_at_wait_final(h, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_http_get(ec200_handle_t       *h,
                              uint32_t              timeout_ms,
                              ec200_http_response_t *resp)
{
    if (!resp) {
        return EC200_ERR_PARAM;
    }

    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QHTTPGET=%u",
                   timeout_secs(timeout_ms));

    char raw[128];
    ec200_status_t st = ec200_at_send_await_urc(h, cmd, "+QHTTPGET:",
                                                raw, sizeof(raw),
                                                EC200_AT_TIMEOUT_DEFAULT,
                                                timeout_ms);
    if (st != EC200_OK) {
        return st;
    }
    return parse_http_result(raw, resp);
}

ec200_status_t ec200_http_post(ec200_handle_t       *h,
                               const uint8_t        *body,
                               uint32_t              body_len,
                               const char           *content_type,
                               uint32_t              timeout_ms,
                               ec200_http_response_t *resp)
{
    if (!resp || !body || body_len == 0U || body_len > 65535U) {
        return EC200_ERR_PARAM;
    }

    /* Set content-type if provided. */
    if (content_type) {
        char cfg_cmd[128];
        (void)snprintf(cfg_cmd, sizeof(cfg_cmd),
                       "AT+QHTTPCFG=\"contenttype\",\"%s\"", content_type);
        ec200_status_t cst = ec200_at_send(h, cfg_cmd, NULL, 0,
                                           EC200_AT_TIMEOUT_DEFAULT);
        if (cst != EC200_OK) {
            return cst;
        }
    }

    char cmd[64];
    (void)snprintf(cmd, sizeof(cmd), "AT+QHTTPPOST=%u,%u,%u",
                   (unsigned)body_len,
                   timeout_secs(timeout_ms),
                   timeout_secs(timeout_ms));

    /* CONNECT prompt → body → OK → +QHTTPPOST URC. */
    char connect_buf[32];
    ec200_status_t st = ec200_at_send_expect(h, cmd, "CONNECT",
                                             connect_buf, sizeof(connect_buf),
                                             EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    st = ec200_at_write_raw(h, body, (uint16_t)body_len);
    if (st != EC200_OK) {
        return st;
    }

    st = ec200_at_wait_final(h, EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    char raw[128];
    st = ec200_at_wait_prefix(h, "+QHTTPPOST:", raw, sizeof(raw), timeout_ms);
    if (st != EC200_OK) {
        return st;
    }
    return parse_http_result(raw, resp);
}

/**
 * @brief Stream body bytes until the "\r\nOK\r\n" trailer, filling @p buf.
 *
 * Bytes beyond @p buf_sz are discarded (bounded by ::HTTP_MAX_DISCARD) so
 * the stream still re-synchronises; truncation is reported as
 * EC200_ERR_OVERFLOW with @p bytes_read == @p buf_sz.
 */
static ec200_status_t read_body_until_ok(ec200_handle_t *h,
                                         uint8_t        *buf,
                                         size_t          buf_sz,
                                         uint32_t       *bytes_read,
                                         uint32_t        timeout_ms)
{
    static const char trailer[] = "\r\nOK\r\n";
    const size_t trailer_len = sizeof(trailer) - 1U;

    size_t   out_pos   = 0;
    size_t   match     = 0;
    uint32_t discarded = 0;
    uint32_t budget    = timeout_ms;
    bool     overflow  = false;

    for (;;) {
        uint8_t  ch  = 0;
        uint16_t got = 0;
        uint32_t slice = (budget < EC200_AT_POLL_SLICE_MS)
                           ? budget : EC200_AT_POLL_SLICE_MS;

        ec200_status_t st = ec200_at_read_raw(h, &ch, 1U, slice, &got);
        if (st == EC200_ERR_IO) {
            return EC200_ERR_IO;
        }
        if (st == EC200_ERR_TIMEOUT) { /* EC200_OK implies got == 1 */
            if (budget == 0U) {
                *bytes_read = (uint32_t)out_pos;
                return EC200_ERR_TIMEOUT;
            }
            budget -= slice;
            continue;
        }

        if ((char)ch == trailer[match]) {
            match++;
            if (match == trailer_len) {
                *bytes_read = (uint32_t)out_pos;
                return overflow ? EC200_ERR_OVERFLOW : EC200_OK;
            }
            continue;
        }

        /* Flush the partially-matched trailer prefix into the body. */
        for (size_t i = 0; i < match; i++) {
            if (out_pos < buf_sz) {
                buf[out_pos++] = (uint8_t)trailer[i];
            } else {
                overflow = true;
                discarded++;
            }
        }
        match = 0;

        if ((char)ch == trailer[0]) {
            match = 1;
        } else if (out_pos < buf_sz) {
            buf[out_pos++] = ch;
        } else {
            overflow = true;
            discarded++;
        }

        if (discarded > HTTP_MAX_DISCARD) {
            *bytes_read = (uint32_t)out_pos;
            return EC200_ERR_OVERFLOW;
        }
    }
}

ec200_status_t ec200_http_read(ec200_handle_t *h,
                               uint8_t        *buf,
                               size_t          buf_sz,
                               uint32_t       *bytes_read,
                               uint32_t        timeout_ms)
{
    if (!buf || !bytes_read || buf_sz == 0U) {
        return EC200_ERR_PARAM;
    }
    *bytes_read = 0;

    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QHTTPREAD=%u",
                   timeout_secs(timeout_ms));

    /* CONNECT → body bytes → "OK" → +QHTTPREAD: <err> URC. */
    char connect_buf[32];
    ec200_status_t st = ec200_at_send_expect(h, cmd, "CONNECT",
                                             connect_buf, sizeof(connect_buf),
                                             EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    st = read_body_until_ok(h, buf, buf_sz, bytes_read, timeout_ms);
    if (st != EC200_OK && st != EC200_ERR_OVERFLOW) {
        return st;
    }

    /* Final +QHTTPREAD: <err> URC confirms the transfer. */
    char raw[64];
    ec200_status_t urc = ec200_at_wait_prefix(h, "+QHTTPREAD:", raw,
                                              sizeof(raw),
                                              EC200_AT_TIMEOUT_DEFAULT);
    if (urc == EC200_OK) {
        int err = 0;
        if (ec200_at_parse_int_field(raw, 0U, &err) == EC200_OK && err != 0) {
            return EC200_ERR_MODULE;
        }
    }
    return st; /* EC200_OK, or EC200_ERR_OVERFLOW when truncated */
}

ec200_status_t ec200_http_stop(ec200_handle_t *h)
{
    return ec200_at_send(h, "AT+QHTTPSTOP", NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}
