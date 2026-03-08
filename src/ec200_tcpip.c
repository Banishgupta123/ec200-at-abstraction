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

ec200_status_t ec200_tcp_open(ec200_handle_t    *h,
                              uint8_t            ctx_id,
                              uint8_t            conn_id,
                              ec200_sock_type_t  type,
                              const char        *host,
                              uint16_t           port,
                              ec200_access_mode_t access_mode)
{
    if (!host || conn_id >= EC200_MAX_CONNECTIONS) return EC200_ERR_PARAM;
    if ((int)type > EC200_SOCK_UDP_SERVICE)         return EC200_ERR_PARAM;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "AT+QIOPEN=%u,%u,\"%s\",\"%s\",%u,0,%u",
             (unsigned)ctx_id,
             (unsigned)conn_id,
             sock_type_str[(int)type],
             host,
             (unsigned)port,
             (unsigned)access_mode);

    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QIOPEN:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) return st;

    /* +QIOPEN: <conn_id>,<err>   (0 = success) */
    const char *comma = strchr(resp, ',');
    if (!comma) return EC200_ERR_PARSE;
    int err = atoi(comma + 1);
    return (err == 0) ? EC200_OK : EC200_ERR_UNKNOWN;
}

ec200_status_t ec200_tcp_send(ec200_handle_t *h,
                              uint8_t         conn_id,
                              const uint8_t  *data,
                              uint16_t        len)
{
    if (!data || len == 0 || conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QISEND=%u,%u", (unsigned)conn_id, (unsigned)len);

    /* Write command; expect ">" prompt */
    int cmdlen = snprintf(h->_tx_buf, sizeof(h->_tx_buf), "%s\r\n", cmd);
    if (cmdlen < 0 || (size_t)cmdlen >= sizeof(h->_tx_buf)) {
        return EC200_ERR_OVERFLOW;
    }
    int n = h->write((const uint8_t *)h->_tx_buf, (uint16_t)cmdlen, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    /* Wait for ">" */
    uint8_t ch;
    bool got_prompt = false;
    for (int i = 0; i < 300; i++) {
        int r = h->read(&ch, 1, 100U, h->user_ctx);
        if (r > 0 && ch == '>') {
            got_prompt = true;
            break;
        }
    }
    if (!got_prompt) return EC200_ERR_TIMEOUT;

    /* Send the data payload */
    n = h->write(data, len, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    /* Wait for "SEND OK" */
    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "", "SEND OK",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    return st;
}

ec200_status_t ec200_tcp_recv(ec200_handle_t *h,
                              uint8_t         conn_id,
                              uint8_t        *buf,
                              uint16_t        max_len,
                              uint16_t       *bytes_read,
                              uint32_t        timeout_ms)
{
    if (!buf || !bytes_read || conn_id >= EC200_MAX_CONNECTIONS) {
        return EC200_ERR_PARAM;
    }
    *bytes_read = 0;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QIRD=%u,%u",
             (unsigned)conn_id, (unsigned)max_len);

    char resp[EC200_RX_BUFFER_SIZE];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QIRD:",
                                           resp, sizeof(resp),
                                           timeout_ms);
    if (st != EC200_OK) return st;

    /* +QIRD: <read_actual_length>\r\n<data> */
    const char *colon = strchr(resp, ':');
    if (!colon) return EC200_ERR_PARSE;
    uint16_t actual = (uint16_t)atoi(colon + 1);
    if (actual == 0) return EC200_OK;

    /* Read 'actual' raw bytes */
    int r = h->read(buf, actual, timeout_ms, h->user_ctx);
    if (r < 0) return EC200_ERR_IO;
    *bytes_read = (uint16_t)r;
    return EC200_OK;
}

ec200_status_t ec200_tcp_close(ec200_handle_t *h, uint8_t conn_id)
{
    if (conn_id >= EC200_MAX_CONNECTIONS) return EC200_ERR_PARAM;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QICLOSE=%u", (unsigned)conn_id);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_LONG);
}

ec200_status_t ec200_tcp_get_state(ec200_handle_t *h,
                                   uint8_t         conn_id,
                                   ec200_socket_t *sock)
{
    if (!sock || conn_id >= EC200_MAX_CONNECTIONS) return EC200_ERR_PARAM;
    memset(sock, 0, sizeof(*sock));
    sock->conn_id = (int)conn_id;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QISTATE=%u", (unsigned)conn_id);

    char resp[256];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QISTATE:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /* +QISTATE: <conn_id>,"TCP","<ip>",<port>,<state>,... */
    char type_str[16] = {0};
    char state_str[16] = {0};
    int  cid_val = 0;
    unsigned port = 0;

    const char *p = strchr(resp, ':');
    if (!p) return EC200_ERR_PARSE;
    sscanf(p + 1, " %d,\"%15[^\"]\",\"%45[^\"]\",%u,\"%15[^\"]\"",
           &cid_val, type_str, sock->remote_host, &port, state_str);

    sock->remote_port = (uint16_t)port;
    sock->type = (strncmp(type_str, "UDP", 3) == 0)
                    ? EC200_SOCK_UDP : EC200_SOCK_TCP;
    sock->connected = (strncmp(state_str, "CONNECTED", 9) == 0);
    return EC200_OK;
}

ec200_status_t ec200_tcp_bytes_available(ec200_handle_t *h,
                                         uint8_t         conn_id,
                                         uint32_t       *bytes_avail)
{
    if (!bytes_avail || conn_id >= EC200_MAX_CONNECTIONS) return EC200_ERR_PARAM;
    *bytes_avail = 0;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QIRD=%u,0", (unsigned)conn_id);

    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QIRD:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /* +QIRD: <recv_buf>,<have_recv>,<unread_len> */
    const char *p = strchr(resp, ':');
    if (!p) return EC200_ERR_PARSE;

    unsigned recv_buf = 0, have_recv = 0, unread = 0;
    sscanf(p + 1, " %u,%u,%u", &recv_buf, &have_recv, &unread);
    *bytes_avail = (uint32_t)unread;
    return EC200_OK;
}
