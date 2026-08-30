/**
 * @file ec200_mqtt.c
 * @brief MQTT client implementation (AT+QMT*).
 *
 * All QMT commands are asynchronous on the EC200: the module acknowledges
 * with "OK" and reports the outcome later as a `+QMTxxx:` URC, which is
 * handled via ec200_at_send_await_urc().
 *
 * Publishing uses AT+QMTPUBEX (length-parameterised) rather than the
 * Ctrl-Z-terminated AT+QMTPUB, so binary payloads containing 0x1A are safe.
 */

#include "ec200_mqtt.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/**
 * @brief Check the `<result>`/`<err>` field of a +QMTxxx result line.
 *
 * @param line   Result line, e.g. "+QMTOPEN: 0,0".
 * @param field  Zero-based index of the result field.
 */
static ec200_status_t qmt_result(const char *line, unsigned field)
{
    int err = 0;
    if (ec200_at_parse_int_field(line, field, &err) != EC200_OK) {
        return EC200_ERR_PARSE;
    }
    return (err == 0) ? EC200_OK : EC200_ERR_MODULE;
}

/**
 * @brief URC handler for "+QMTRECV:" — parses and forwards inbound messages.
 *
 * Registered with the AT layer by ec200_mqtt_set_message_cb(), so received
 * publications are dispatched even when they arrive mid-command.
 */
static void mqtt_recv_urc(const char *line, void *ctx)
{
    ec200_handle_t *h = (ec200_handle_t *)ctx;
    /* ctx is always the registering handle; the cb check is the live one. */
    if (h == NULL || h->mqtt_msg_cb == NULL) { /* GCOVR_EXCL_BR_LINE */
        return;
    }

    /*
     * +QMTRECV: <client_idx>,<msg_id>,"<topic>","<payload>"
     * The message struct is static (not stack) because it is ~1.6 KB and
     * this handler can run from small polling tasks.  The library is
     * single-threaded per handle by contract, so this is safe.
     */
    static ec200_mqtt_message_t msg;
    msg.topic[0]    = '\0';
    msg.payload_len = 0;
    msg.qos         = EC200_MQTT_QOS0;

    const char *p = strchr(line, '"');
    if (p == NULL) {
        return;
    }
    const char *q = strchr(p + 1, '"');
    if (q == NULL) {
        return;
    }
    size_t tlen = (size_t)(q - p - 1);
    if (tlen >= sizeof(msg.topic)) {
        tlen = sizeof(msg.topic) - 1U;
    }
    memcpy(msg.topic, p + 1, tlen);
    msg.topic[tlen] = '\0';

    p = strchr(q + 1, '"');
    if (p != NULL) {
        q = strrchr(p + 1, '"'); /* found => q >= p + 1 by construction */
        if (q != NULL) {
            size_t plen = (size_t)(q - p - 1);
            if (plen > sizeof(msg.payload)) {
                plen = sizeof(msg.payload);
            }
            memcpy(msg.payload, p + 1, plen);
            msg.payload_len = (uint32_t)plen;
        }
    }

    h->mqtt_msg_cb(&msg, h->user_ctx);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_mqtt_open(ec200_handle_t          *h,
                               const ec200_mqtt_config_t *cfg)
{
    if (!cfg || cfg->host[0] == '\0') {
        return EC200_ERR_PARAM;
    }
    if (cfg->use_tls && cfg->ssl_ctx_id > 5U) {
        return EC200_ERR_PARAM;
    }

    char cmd[EC200_MAX_URL_LEN + 32];

    /*
     * Always state the TLS setting explicitly
     * (AT+QMTCFG="ssl",<client_idx>,<enable>,<ctx_id>).  The module keeps
     * this setting between sessions, so a previous MQTTS connection would
     * otherwise leave TLS enabled and break a later plaintext open.
     */
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+QMTCFG=\"ssl\",%u,%u,%u",
                   (unsigned)cfg->tcp_connect_id,
                   cfg->use_tls ? 1U : 0U,
                   (unsigned)cfg->ssl_ctx_id);
    {
        ec200_status_t sst = ec200_at_send(h, cmd, NULL, 0,
                                           EC200_AT_TIMEOUT_DEFAULT);
        if (sst != EC200_OK) {
            return sst;
        }
    }
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+QMTOPEN=%u,\"%s\",%u",
                   (unsigned)cfg->tcp_connect_id,
                   cfg->host,
                   (unsigned)cfg->port);

    char resp[64];
    ec200_status_t st = ec200_at_send_await_urc(h, cmd, "+QMTOPEN:",
                                                resp, sizeof(resp),
                                                EC200_AT_TIMEOUT_DEFAULT,
                                                EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    /* +QMTOPEN: <tcp_connectID>,<err>  (0 = success) */
    return qmt_result(resp, 1U);
}

ec200_status_t ec200_mqtt_connect(ec200_handle_t          *h,
                                  const ec200_mqtt_config_t *cfg)
{
    if (!cfg || cfg->client_id[0] == '\0') {
        return EC200_ERR_PARAM;
    }

    char cmd[256];
    if (cfg->username[0] != '\0') {
        (void)snprintf(cmd, sizeof(cmd),
                       "AT+QMTCONN=%u,\"%s\",\"%s\",\"%s\"",
                       (unsigned)cfg->client_idx,
                       cfg->client_id,
                       cfg->username,
                       cfg->password);
    } else {
        (void)snprintf(cmd, sizeof(cmd),
                       "AT+QMTCONN=%u,\"%s\"",
                       (unsigned)cfg->client_idx,
                       cfg->client_id);
    }

    char resp[64];
    ec200_status_t st = ec200_at_send_await_urc(h, cmd, "+QMTCONN:",
                                                resp, sizeof(resp),
                                                EC200_AT_TIMEOUT_DEFAULT,
                                                EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    /* +QMTCONN: <client_idx>,<result>[,<ret_code>]  (result 0 = accepted) */
    return qmt_result(resp, 1U);
}

ec200_status_t ec200_mqtt_disconnect(ec200_handle_t *h, uint8_t client_idx)
{
    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QMTDISC=%u", (unsigned)client_idx);

    char resp[64];
    ec200_status_t st = ec200_at_send_await_urc(h, cmd, "+QMTDISC:",
                                                resp, sizeof(resp),
                                                EC200_AT_TIMEOUT_DEFAULT,
                                                EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    return qmt_result(resp, 1U);
}

ec200_status_t ec200_mqtt_close(ec200_handle_t *h, uint8_t tcp_connect_id)
{
    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QMTCLOSE=%u",
                   (unsigned)tcp_connect_id);

    char resp[64];
    ec200_status_t st = ec200_at_send_await_urc(h, cmd, "+QMTCLOSE:",
                                                resp, sizeof(resp),
                                                EC200_AT_TIMEOUT_DEFAULT,
                                                EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    return qmt_result(resp, 1U);
}

ec200_status_t ec200_mqtt_subscribe(ec200_handle_t  *h,
                                    uint8_t          client_idx,
                                    uint16_t         msg_id,
                                    const char      *topic,
                                    ec200_mqtt_qos_t qos)
{
    if (!topic || topic[0] == '\0' ||
        strlen(topic) >= EC200_MAX_TOPIC_LEN || msg_id == 0U) {
        return EC200_ERR_PARAM;
    }

    char cmd[EC200_MAX_TOPIC_LEN + 48];
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+QMTSUB=%u,%u,\"%s\",%u",
                   (unsigned)client_idx,
                   (unsigned)msg_id,
                   topic,
                   (unsigned)qos);

    char resp[128];
    ec200_status_t st = ec200_at_send_await_urc(h, cmd, "+QMTSUB:",
                                                resp, sizeof(resp),
                                                EC200_AT_TIMEOUT_DEFAULT,
                                                EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    /* +QMTSUB: <client_idx>,<msg_id>,<result>[,<value>]  (result 0 = ok) */
    return qmt_result(resp, 2U);
}

ec200_status_t ec200_mqtt_unsubscribe(ec200_handle_t *h,
                                      uint8_t         client_idx,
                                      uint16_t        msg_id,
                                      const char     *topic)
{
    if (!topic || topic[0] == '\0' ||
        strlen(topic) >= EC200_MAX_TOPIC_LEN || msg_id == 0U) {
        return EC200_ERR_PARAM;
    }

    char cmd[EC200_MAX_TOPIC_LEN + 48];
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+QMTUNS=%u,%u,\"%s\"",
                   (unsigned)client_idx,
                   (unsigned)msg_id,
                   topic);

    char resp[128];
    ec200_status_t st = ec200_at_send_await_urc(h, cmd, "+QMTUNS:",
                                                resp, sizeof(resp),
                                                EC200_AT_TIMEOUT_DEFAULT,
                                                EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    return qmt_result(resp, 2U);
}

ec200_status_t ec200_mqtt_publish(ec200_handle_t  *h,
                                  uint8_t          client_idx,
                                  uint16_t         msg_id,
                                  ec200_mqtt_qos_t qos,
                                  bool             retain,
                                  const char      *topic,
                                  const uint8_t   *payload,
                                  uint32_t         payload_len)
{
    if (!topic || topic[0] == '\0' ||
        strlen(topic) >= EC200_MAX_TOPIC_LEN) {
        return EC200_ERR_PARAM;
    }
    if (!payload || payload_len == 0U ||
        payload_len > EC200_MAX_PAYLOAD_LEN) {
        return EC200_ERR_PARAM; /* refuse to silently truncate */
    }
    if (qos != EC200_MQTT_QOS0 && msg_id == 0U) {
        return EC200_ERR_PARAM; /* QoS > 0 requires a message ID */
    }

    /*
     * AT+QMTPUBEX takes the payload length up front and reads exactly that
     * many raw bytes after the ">" prompt — no Ctrl-Z terminator, so 0x1A
     * bytes inside binary payloads are transmitted verbatim.
     */
    char cmd[EC200_MAX_TOPIC_LEN + 64];
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+QMTPUBEX=%u,%u,%u,%u,\"%s\",%u",
                   (unsigned)client_idx,
                   (unsigned)msg_id,
                   (unsigned)qos,
                   retain ? 1U : 0U,
                   topic,
                   (unsigned)payload_len);

    ec200_status_t st = ec200_at_send_prompt(h, cmd, EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    st = ec200_at_write_raw(h, payload, (uint16_t)payload_len);
    if (st != EC200_OK) {
        return st;
    }

    /* "OK" is emitted first, then "+QMTPUBEX: <idx>,<msg_id>,<result>". */
    char resp[128];
    st = ec200_at_wait_prefix(h, "+QMTPUBEX:", resp, sizeof(resp),
                              EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }
    return qmt_result(resp, 2U);
}

void ec200_mqtt_set_message_cb(ec200_handle_t   *h,
                               ec200_mqtt_msg_fn callback)
{
    if (h == NULL) {
        return;
    }
    h->mqtt_msg_cb = callback;
    if (callback != NULL) {
        (void)ec200_at_register_urc(h, "+QMTRECV:", mqtt_recv_urc, h);
    } else {
        (void)ec200_at_unregister_urc(h, "+QMTRECV:");
    }
}
