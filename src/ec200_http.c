/**
 * @file ec200_http.c
 * @brief HTTP client implementation (AT+QHTTP*).
 */

#include "ec200_http.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

ec200_status_t ec200_http_set_context(ec200_handle_t *h, uint8_t ctx_id)
{
    if (ctx_id < 1 || ctx_id > 16) return EC200_ERR_PARAM;
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+QHTTPCFG=\"contextid\",%u", (unsigned)ctx_id);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_http_set_url(ec200_handle_t *h, const char *url)
{
    if (!url) return EC200_ERR_PARAM;

    size_t url_len = strlen(url);
    if (url_len == 0 || url_len > EC200_MAX_URL_LEN) return EC200_ERR_PARAM;

    /* AT+QHTTPURL=<url_len>,<timeout> */
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QHTTPURL=%u,80", (unsigned)url_len);

    /* Send command and wait for "CONNECT" (the prompt to enter the URL) */
    int cmdlen = snprintf(h->_tx_buf, sizeof(h->_tx_buf), "%s\r\n", cmd);
    if (cmdlen < 0 || (size_t)cmdlen >= sizeof(h->_tx_buf)) {
        return EC200_ERR_OVERFLOW;
    }
    int n = h->write((const uint8_t *)h->_tx_buf, (uint16_t)cmdlen, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    /* Wait for CONNECT */
    char resp[32];
    ec200_status_t st = ec200_at_send_wait(h, "", "CONNECT",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /* Send the URL */
    n = h->write((const uint8_t *)url, (uint16_t)url_len, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    /* Wait for OK */
    char ok_buf[32];
    return ec200_at_send_wait(h, "", "OK",
                              ok_buf, sizeof(ok_buf),
                              EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_http_get(ec200_handle_t       *h,
                              uint32_t              timeout_ms,
                              ec200_http_response_t *resp)
{
    if (!resp) return EC200_ERR_PARAM;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QHTTPGET=%u", (unsigned)(timeout_ms / 1000U));

    char raw[128];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QHTTPGET:",
                                           raw, sizeof(raw),
                                           timeout_ms);
    if (st != EC200_OK) return st;

    /* +QHTTPGET: <err>,<http_status>,<content_length> */
    const char *p = strchr(raw, ':');
    if (!p) return EC200_ERR_PARSE;
    int err = 0;
    unsigned status_code = 0, clen = 0;
    sscanf(p + 1, " %d,%u,%u", &err, &status_code, &clen);
    if (err != 0) return EC200_ERR_UNKNOWN;

    resp->status_code    = (uint16_t)status_code;
    resp->content_length = (uint32_t)clen;
    return EC200_OK;
}

ec200_status_t ec200_http_post(ec200_handle_t       *h,
                               const uint8_t        *body,
                               uint32_t              body_len,
                               const char           *content_type,
                               uint32_t              timeout_ms,
                               ec200_http_response_t *resp)
{
    if (!resp || !body) return EC200_ERR_PARAM;

    /* Set content-type if provided */
    if (content_type) {
        char cfg_cmd[128];
        snprintf(cfg_cmd, sizeof(cfg_cmd),
                 "AT+QHTTPCFG=\"contenttype\",\"%s\"", content_type);
        ec200_at_send(h, cfg_cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QHTTPPOST=%u,%u,%u",
             (unsigned)body_len,
             (unsigned)(timeout_ms / 1000U),
             (unsigned)(timeout_ms / 1000U));

    /* Send command and wait for CONNECT prompt */
    int cmdlen = snprintf(h->_tx_buf, sizeof(h->_tx_buf), "%s\r\n", cmd);
    if (cmdlen < 0 || (size_t)cmdlen >= sizeof(h->_tx_buf)) {
        return EC200_ERR_OVERFLOW;
    }
    int n = h->write((const uint8_t *)h->_tx_buf, (uint16_t)cmdlen, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    char prompt[32];
    ec200_status_t st = ec200_at_send_wait(h, "", "CONNECT",
                                           prompt, sizeof(prompt),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /* Write body */
    n = h->write(body, (uint16_t)body_len, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    /* Wait for +QHTTPPOST response */
    char raw[128];
    st = ec200_at_send_wait(h, "", "+QHTTPPOST:",
                            raw, sizeof(raw), timeout_ms);
    if (st != EC200_OK) return st;

    const char *p = strchr(raw, ':');
    if (!p) return EC200_ERR_PARSE;
    int err = 0;
    unsigned status_code = 0, clen = 0;
    sscanf(p + 1, " %d,%u,%u", &err, &status_code, &clen);
    if (err != 0) return EC200_ERR_UNKNOWN;

    resp->status_code    = (uint16_t)status_code;
    resp->content_length = (uint32_t)clen;
    return EC200_OK;
}

ec200_status_t ec200_http_read(ec200_handle_t *h,
                               uint8_t        *buf,
                               size_t          buf_sz,
                               uint32_t       *bytes_read,
                               uint32_t        timeout_ms)
{
    if (!buf || !bytes_read) return EC200_ERR_PARAM;
    *bytes_read = 0;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QHTTPREAD=%u", (unsigned)(timeout_ms / 1000U));

    /* Expect CONNECT then data then OK */
    int cmdlen = snprintf(h->_tx_buf, sizeof(h->_tx_buf), "%s\r\n", cmd);
    if (cmdlen < 0 || (size_t)cmdlen >= sizeof(h->_tx_buf)) {
        return EC200_ERR_OVERFLOW;
    }
    int n = h->write((const uint8_t *)h->_tx_buf, (uint16_t)cmdlen, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    char connect_buf[32];
    ec200_status_t st = ec200_at_send_wait(h, "", "CONNECT",
                                           connect_buf, sizeof(connect_buf),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /* Read raw body bytes */
    int r = h->read(buf, (uint16_t)(buf_sz > 0xFFFFU ? 0xFFFFU : buf_sz),
                    timeout_ms, h->user_ctx);
    if (r < 0) return EC200_ERR_IO;
    *bytes_read = (uint32_t)r;
    return EC200_OK;
}

ec200_status_t ec200_http_stop(ec200_handle_t *h)
{
    return ec200_at_send(h, "AT+QHTTPSTOP", NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}
