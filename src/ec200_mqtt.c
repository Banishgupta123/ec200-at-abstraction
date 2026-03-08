/**
 * @file ec200_mqtt.c
 * @brief MQTT client implementation (AT+QMT*).
 */

#include "ec200_mqtt.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

ec200_status_t ec200_mqtt_open(ec200_handle_t          *h,
                               const ec200_mqtt_config_t *cfg)
{
    if (!cfg) return EC200_ERR_PARAM;

    char cmd[EC200_MAX_URL_LEN + 32];
    snprintf(cmd, sizeof(cmd),
             "AT+QMTOPEN=%u,\"%s\",%u",
             (unsigned)cfg->tcp_connect_id,
             cfg->host,
             (unsigned)cfg->port);

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QMTOPEN:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) return st;

    /* +QMTOPEN: <tcp_connectID>,<err>  (0 = success) */
    const char *comma = strchr(resp, ',');
    if (!comma) return EC200_ERR_PARSE;
    int err = atoi(comma + 1);
    return (err == 0) ? EC200_OK : EC200_ERR_UNKNOWN;
}

ec200_status_t ec200_mqtt_connect(ec200_handle_t          *h,
                                  const ec200_mqtt_config_t *cfg)
{
    if (!cfg) return EC200_ERR_PARAM;

    char cmd[256];
    if (cfg->username[0] != '\0') {
        snprintf(cmd, sizeof(cmd),
                 "AT+QMTCONN=%u,\"%s\",\"%s\",\"%s\"",
                 (unsigned)cfg->client_idx,
                 cfg->client_id,
                 cfg->username,
                 cfg->password);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "AT+QMTCONN=%u,\"%s\"",
                 (unsigned)cfg->client_idx,
                 cfg->client_id);
    }

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QMTCONN:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) return st;

    /* +QMTCONN: <client_idx>,<result>[,<ret_code>]  (result 0 = accepted) */
    const char *comma = strchr(resp, ',');
    if (!comma) return EC200_ERR_PARSE;
    int result = atoi(comma + 1);
    return (result == 0) ? EC200_OK : EC200_ERR_UNKNOWN;
}

ec200_status_t ec200_mqtt_disconnect(ec200_handle_t *h, uint8_t client_idx)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QMTDISC=%u", (unsigned)client_idx);

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QMTDISC:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) return st;

    const char *comma = strchr(resp, ',');
    if (!comma) return EC200_ERR_PARSE;
    return (atoi(comma + 1) == 0) ? EC200_OK : EC200_ERR_UNKNOWN;
}

ec200_status_t ec200_mqtt_close(ec200_handle_t *h, uint8_t tcp_connect_id)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QMTCLOSE=%u", (unsigned)tcp_connect_id);

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QMTCLOSE:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) return st;

    const char *comma = strchr(resp, ',');
    if (!comma) return EC200_ERR_PARSE;
    return (atoi(comma + 1) == 0) ? EC200_OK : EC200_ERR_UNKNOWN;
}

ec200_status_t ec200_mqtt_subscribe(ec200_handle_t  *h,
                                    uint8_t          client_idx,
                                    uint16_t         msg_id,
                                    const char      *topic,
                                    ec200_mqtt_qos_t qos)
{
    if (!topic) return EC200_ERR_PARAM;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "AT+QMTSUB=%u,%u,\"%s\",%u",
             (unsigned)client_idx,
             (unsigned)msg_id,
             topic,
             (unsigned)qos);

    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QMTSUB:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) return st;

    /* +QMTSUB: <client_idx>,<msg_id>,<result>[,<value>]  (result 0 = ok) */
    /* Skip two commas to get result */
    const char *p = strchr(resp, ',');
    if (!p) return EC200_ERR_PARSE;
    p = strchr(p + 1, ',');
    if (!p) return EC200_ERR_PARSE;
    return (atoi(p + 1) == 0) ? EC200_OK : EC200_ERR_UNKNOWN;
}

ec200_status_t ec200_mqtt_unsubscribe(ec200_handle_t *h,
                                      uint8_t         client_idx,
                                      uint16_t        msg_id,
                                      const char     *topic)
{
    if (!topic) return EC200_ERR_PARAM;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "AT+QMTUNS=%u,%u,\"%s\"",
             (unsigned)client_idx,
             (unsigned)msg_id,
             topic);

    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QMTUNS:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) return st;

    const char *p = strchr(resp, ',');
    if (!p) return EC200_ERR_PARSE;
    p = strchr(p + 1, ',');
    if (!p) return EC200_ERR_PARSE;
    return (atoi(p + 1) == 0) ? EC200_OK : EC200_ERR_UNKNOWN;
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
    if (!topic || !payload) return EC200_ERR_PARAM;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "AT+QMTPUB=%u,%u,%u,%u,\"%s\"",
             (unsigned)client_idx,
             (unsigned)msg_id,
             (unsigned)qos,
             retain ? 1U : 0U,
             topic);

    /* Send command, wait for ">" prompt */
    int cmdlen = snprintf(h->_tx_buf, sizeof(h->_tx_buf), "%s\r\n", cmd);
    if (cmdlen < 0 || (size_t)cmdlen >= sizeof(h->_tx_buf)) {
        return EC200_ERR_OVERFLOW;
    }
    int n = h->write((const uint8_t *)h->_tx_buf, (uint16_t)cmdlen, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

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

    /* Send payload then Ctrl-Z */
    if (payload_len > EC200_MAX_PAYLOAD_LEN) payload_len = EC200_MAX_PAYLOAD_LEN;
    n = h->write(payload, (uint16_t)payload_len, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    uint8_t ctrlz = 0x1A;
    n = h->write(&ctrlz, 1, h->user_ctx);
    if (n < 0) return EC200_ERR_IO;

    /* Wait for +QMTPUB: response */
    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, "", "+QMTPUB:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) return st;

    /* +QMTPUB: <client_idx>,<msg_id>,<result>  (result 0 = ok) */
    const char *p = strchr(resp, ',');
    if (!p) return EC200_ERR_PARSE;
    p = strchr(p + 1, ',');
    if (!p) return EC200_ERR_PARSE;
    return (atoi(p + 1) == 0) ? EC200_OK : EC200_ERR_UNKNOWN;
}

void ec200_mqtt_set_message_cb(ec200_handle_t   *h,
                               ec200_mqtt_msg_fn callback)
{
    if (h) {
        h->mqtt_msg_cb = callback;
    }
}
