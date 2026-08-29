/**
 * @file ec200_tcpip.c
 * @brief TCP/IP socket implementation (AT+QIOPEN / AT+QISEND / AT+QIRD / AT+QICLOSE).
 */

#include "ec200_tcpip.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char * const sock_type_str[] = {
    [EC200_SOCK_TCP]          = "TCP",
    [EC200_SOCK_UDP]          = "UDP",
    [EC200_SOCK_TCP_LISTENER] = "TCP LISTENER",
    [EC200_SOCK_UDP_SERVICE]  = "UDP SERVICE",
};

/** AT+QISTATE <socket_state> value meaning "connected". */
#define QISTATE_CONNECTED  (2)

ec200_status_t ec200_tcp_open(ec200_handle_t    *h,
                              uint8_t            ctx_id,
                              uint8_t            conn_id,
                              ec200_sock_type_t  type,
                              const char        *host,
                              uint16_t           port,
                              ec200_access_mode_t access_mode)
{
    if (!host || host[0] == '\0' || conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }
    if (ctx_id < 1U || ctx_id > 16U) {
        return EC200_ERR_PARAM;
    }
    if ((int)type > EC200_SOCK_UDP_SERVICE) {
        return EC200_ERR_PARAM;
    }

    char cmd[EC200_MAX_URL_LEN + 64];
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+QIOPEN=%u,%u,\"%s\",\"%s\",%u,0,%u",
                   (unsigned)ctx_id,
                   (unsigned)conn_id,
                   sock_type_str[(int)type],
                   host,
                   (unsigned)port,
                   (unsigned)access_mode);

    /* Asynchronous command: "OK" first, then "+QIOPEN: <conn_id>,<err>". */
    char resp[64];
    ec200_status_t st = ec200_at_send_await_urc(h, cmd, "+QIOPEN:",
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

ec200_status_t ec200_tcp_send(ec200_handle_t *h,
                              uint8_t         conn_id,
                              const uint8_t  *data,
                              uint16_t        len)
{
    if (!data || len == 0 || len > EC200_MAX_PAYLOAD_LEN ||
        conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }

    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QISEND=%u,%u",
                   (unsigned)conn_id, (unsigned)len);

    ec200_status_t st = ec200_at_send_prompt(h, cmd, EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    st = ec200_at_write_raw(h, data, len);
    if (st != EC200_OK) {
        return st;
    }

    /* Module answers "SEND OK" / "SEND FAIL". */
    char resp[32];
    st = ec200_at_wait_prefix(h, "SEND", resp, sizeof(resp),
                              EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    return (strcmp(resp, "SEND OK") == 0) ? EC200_OK : EC200_ERR_MODULE;
}

ec200_status_t ec200_tcp_recv(ec200_handle_t *h,
                              uint8_t         conn_id,
                              uint8_t        *buf,
                              uint16_t        max_len,
                              uint16_t       *bytes_read,
                              uint32_t        timeout_ms)
{
    if (!buf || !bytes_read || max_len == 0 ||
        conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }
    *bytes_read = 0;

    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QIRD=%u,%u",
                   (unsigned)conn_id, (unsigned)max_len);

    /* Header line, then raw data, then "OK" — do NOT read past the header. */
    char hdr[64];
    ec200_status_t st = ec200_at_send_expect(h, cmd, "+QIRD:",
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

    /* Consume the trailing "OK" so the stream stays synchronised. */
    return ec200_at_wait_final(h, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_tcp_close(ec200_handle_t *h, uint8_t conn_id)
{
    if (conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }
    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QICLOSE=%u", (unsigned)conn_id);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_LONG);
}

ec200_status_t ec200_tcp_get_state(ec200_handle_t *h,
                                   uint8_t         conn_id,
                                   ec200_socket_t *sock)
{
    if (!sock || conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }
    memset(sock, 0, sizeof(*sock));
    sock->conn_id = (int)conn_id;

    /* Query type 1 = "query by connection ID". */
    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QISTATE=1,%u", (unsigned)conn_id);

    char resp[256];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QISTATE:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    /*
     * +QISTATE: <connID>,"<service_type>","<IP>",<remote_port>,<local_port>,
     *           <socket_state>,<ctxID>,<serverID>,<access_mode>,"<AT_port>"
     */
    char type_str[16] = {0};
    char host[EC200_MAX_IP_ADDR_LEN] = {0};
    int  cid_val = 0;
    unsigned remote_port = 0, local_port = 0;
    int  state = -1;

    /* The prefix match guarantees the ':'. */
    const char *p = strchr(resp, ':');
    p = (p != NULL) ? (p + 1) : resp; /* GCOVR_EXCL_BR_LINE */
    int fields = sscanf(p, " %d,\"%15[^\"]\",\"%45[^\"]\",%u,%u,%d",
                        &cid_val, type_str, host,
                        &remote_port, &local_port, &state);
    if (fields < 6) {
        return EC200_ERR_PARSE;
    }

    /* sscanf caps host at 45 chars, which always fits the 256-byte field. */
    memcpy(sock->remote_host, host, strlen(host) + 1U);

    sock->remote_port = (uint16_t)remote_port;
    sock->type = (strncmp(type_str, "UDP", 3) == 0)
                    ? EC200_SOCK_UDP : EC200_SOCK_TCP;
    sock->connected = (state == QISTATE_CONNECTED);
    return EC200_OK;
}

ec200_status_t ec200_tcp_bytes_available(ec200_handle_t *h,
                                         uint8_t         conn_id,
                                         uint32_t       *bytes_avail)
{
    if (!bytes_avail || conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }
    *bytes_avail = 0;

    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QIRD=%u,0", (unsigned)conn_id);

    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QIRD:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    /* +QIRD: <total_recv>,<have_read>,<unread> */
    int unread = 0;
    if (ec200_at_parse_int_field(resp, 2U, &unread) != EC200_OK ||
        unread < 0) {
        return EC200_ERR_PARSE;
    }
    *bytes_avail = (uint32_t)unread;
    return EC200_OK;
}
