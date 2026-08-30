/**
 * @file ec200_ssl_socket.c
 * @brief TLS client socket implementation (AT+QSSL*).
 */

#include "ec200_ssl_socket.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

ec200_status_t ec200_ssl_socket_open(ec200_handle_t *h,
                                     uint8_t         pdp_ctx,
                                     uint8_t         ssl_ctx,
                                     uint8_t         conn_id,
                                     const char     *host,
                                     uint16_t        port)
{
    if (!host || host[0] == '\0' || conn_id >= EC200_MAX_CONNECTIONS ||
        pdp_ctx < 1U || pdp_ctx > 16U || ssl_ctx > 5U) {
        return EC200_ERR_PARAM;
    }

    /* Reject over-long names: they would silently truncate into a
     * malformed AT command. */
    if (strlen(host) >= EC200_MAX_URL_LEN) {
        return EC200_ERR_PARAM;
    }

    char cmd[EC200_MAX_URL_LEN + 64];
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+QSSLOPEN=%u,%u,%u,\"%s\",%u,0",
                   (unsigned)pdp_ctx, (unsigned)ssl_ctx,
                   (unsigned)conn_id, host, (unsigned)port);

    /* Async: "OK" then "+QSSLOPEN: <conn_id>,<err>". */
    char resp[64];
    ec200_status_t st = ec200_at_send_await_urc(h, cmd, "+QSSLOPEN:",
                                                resp, sizeof(resp),
                                                EC200_AT_TIMEOUT_DEFAULT,
                                                EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    int err = 0;
    if (ec200_at_parse_int_field(resp, 1U, &err) != EC200_OK) {
        return EC200_ERR_PARSE;
    }
    return (err == 0) ? EC200_OK : EC200_ERR_MODULE;
}

ec200_status_t ec200_ssl_socket_send(ec200_handle_t *h,
                                     uint8_t         conn_id,
                                     const uint8_t  *data,
                                     uint16_t        len)
{
    if (!data || len == 0U || len > EC200_MAX_PAYLOAD_LEN ||
        conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }

    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QSSLSEND=%u,%u",
                   (unsigned)conn_id, (unsigned)len);

    ec200_status_t st = ec200_at_send_prompt(h, cmd, EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }
    st = ec200_at_write_raw(h, data, len);
    if (st != EC200_OK) {
        return st;
    }

    char resp[32];
    st = ec200_at_wait_prefix(h, "SEND", resp, sizeof(resp),
                              EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    return (strcmp(resp, "SEND OK") == 0) ? EC200_OK : EC200_ERR_MODULE;
}

ec200_status_t ec200_ssl_socket_recv(ec200_handle_t *h,
                                     uint8_t         conn_id,
                                     uint8_t        *buf,
                                     uint16_t        max_len,
                                     uint16_t       *bytes_read,
                                     uint32_t        timeout_ms)
{
    if (!buf || !bytes_read || max_len == 0U ||
        conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }
    *bytes_read = 0;

    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QSSLRECV=%u,%u",
                   (unsigned)conn_id, (unsigned)max_len);

    /* Header line, then raw data, then "OK". */
    char hdr[48];
    ec200_status_t st = ec200_at_send_expect(h, cmd, "+QSSLRECV:",
                                             hdr, sizeof(hdr), timeout_ms);
    if (st != EC200_OK) {
        return st;
    }

    int actual = 0;
    if (ec200_at_parse_int_field(hdr, 0U, &actual) != EC200_OK ||
        actual < 0 || actual > (int)max_len) {
        return EC200_ERR_PARSE;
    }

    if (actual > 0) {
        uint16_t got = 0;
        st = ec200_at_read_exact(h, buf, (uint16_t)actual, timeout_ms, &got);
        *bytes_read = got;
        if (st != EC200_OK) {
            return st;
        }
    }
    return ec200_at_wait_final(h, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_ssl_socket_close(ec200_handle_t *h, uint8_t conn_id)
{
    if (conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }
    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QSSLCLOSE=%u", (unsigned)conn_id);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_LONG);
}
