/**
 * @file test_protocols.c
 * @brief Unit tests for the TCP/IP, MQTT, HTTP, and SMS protocol modules.
 *
 * Response scripts follow real EC200 firmware behaviour: asynchronous
 * commands acknowledge with "OK" first and deliver the result as a URC.
 */

#include <string.h>
#include <stdio.h>

#include "test_helpers.h"

static ec200_handle_t h;

void setUp(void)
{
    SETUP_MODEM(&h);
}

void tearDown(void)
{
}

/* =========================================================================
 * TCP/IP
 * ========================================================================= */

void test_tcp_open_async_ok(void)
{
    /* Regression: "OK first, +QIOPEN URC later" must succeed. */
    lb_on_write("AT+QIOPEN=1,0,\"TCP\",\"example.com\",80,0,0",
                "\r\nOK\r\n+QIOPEN: 0,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_tcp_open(&h, 1, 0, EC200_SOCK_TCP, "example.com", 80,
                       EC200_ACCESS_BUFFER));
}

void test_tcp_open_module_error(void)
{
    lb_on_write("AT+QIOPEN", "\r\nOK\r\n+QIOPEN: 0,566\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_tcp_open(&h, 1, 0, EC200_SOCK_TCP, "example.com", 80,
                       EC200_ACCESS_BUFFER));
}

void test_tcp_open_param_validation(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_open(&h, 1, 12, EC200_SOCK_TCP, "x", 80,
                       EC200_ACCESS_BUFFER));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_open(&h, 0, 0, EC200_SOCK_TCP, "x", 80,
                       EC200_ACCESS_BUFFER));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_open(&h, 1, 0, EC200_SOCK_TCP, NULL, 80,
                       EC200_ACCESS_BUFFER));
}

void test_tcp_send_ok(void)
{
    lb_on_write("AT+QISEND=0,5", "\r\n> ");
    lb_on_write("HELLO", "\r\nSEND OK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_tcp_send(&h, 0, (const uint8_t *)"HELLO", 5));
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "AT+QISEND=0,5\r"));
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "HELLO"));
}

void test_tcp_send_fail_status(void)
{
    lb_on_write("AT+QISEND=0,2", "\r\n> ");
    lb_on_write("XY", "\r\nSEND FAIL\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_tcp_send(&h, 0, (const uint8_t *)"XY", 2));
}

void test_tcp_send_error_instead_of_prompt(void)
{
    /* Regression: an early ERROR must abort, not block for 30 seconds. */
    lb_on_write("AT+QISEND=3,2", "\r\n+CME ERROR: 3\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_tcp_send(&h, 3, (const uint8_t *)"XY", 2));
}

void test_tcp_recv_full_payload(void)
{
    /* Regression: the payload must arrive intact (no drained bytes). */
    lb_on_write("AT+QIRD=0,16", "\r\n+QIRD: 5\r\nHELLO\r\nOK\r\n");
    uint8_t buf[16];
    uint16_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_tcp_recv(&h, 0, buf, sizeof(buf), &got, 1000));
    TEST_ASSERT_EQUAL_UINT16(5, got);
    TEST_ASSERT_EQUAL_MEMORY("HELLO", buf, 5);
    TEST_ASSERT_EQUAL_size_t(0, lb_rx_pending()); /* stream in sync */
}

void test_tcp_recv_no_data(void)
{
    lb_on_write("AT+QIRD=0,16", "\r\n+QIRD: 0\r\n\r\nOK\r\n");
    uint8_t buf[16];
    uint16_t got = 0xFFFF;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_tcp_recv(&h, 0, buf, sizeof(buf), &got, 1000));
    TEST_ASSERT_EQUAL_UINT16(0, got);
}

void test_tcp_get_state_connected(void)
{
    /* Regression: correct AT+QISTATE=1,<id> syntax and numeric state. */
    lb_on_write("AT+QISTATE=1,0",
        "\r\n+QISTATE: 0,\"TCP\",\"1.2.3.4\",8705,65344,2,1,0,0,\"usbmodem\""
        "\r\n\r\nOK\r\n");
    ec200_socket_t sock;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_tcp_get_state(&h, 0, &sock));
    TEST_ASSERT_TRUE(sock.connected);
    TEST_ASSERT_EQUAL_INT(EC200_SOCK_TCP, sock.type);
    TEST_ASSERT_EQUAL_STRING("1.2.3.4", sock.remote_host);
    TEST_ASSERT_EQUAL_UINT16(8705, sock.remote_port);
}

void test_tcp_bytes_available(void)
{
    lb_on_write("AT+QIRD=0,0", "\r\n+QIRD: 100,60,40\r\n\r\nOK\r\n");
    uint32_t avail = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_tcp_bytes_available(&h, 0, &avail));
    TEST_ASSERT_EQUAL_UINT32(40, avail);
}

/* =========================================================================
 * MQTT
 * ========================================================================= */

static ec200_mqtt_config_t mqtt_cfg(void)
{
    ec200_mqtt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    (void)snprintf(cfg.host, sizeof(cfg.host), "%s", "broker.example.com");
    (void)snprintf(cfg.client_id, sizeof(cfg.client_id), "%s", "dev1");
    cfg.port = 1883;
    return cfg;
}

void test_mqtt_open_async_ok(void)
{
    ec200_mqtt_config_t cfg = mqtt_cfg();
    lb_on_write("AT+QMTOPEN=0,\"broker.example.com\",1883",
                "\r\nOK\r\n+QMTOPEN: 0,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_mqtt_open(&h, &cfg));
}

void test_mqtt_open_pdp_error(void)
{
    ec200_mqtt_config_t cfg = mqtt_cfg();
    lb_on_write("AT+QMTOPEN", "\r\nOK\r\n+QMTOPEN: 0,3\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_mqtt_open(&h, &cfg));
}

void test_mqtt_connect_ok(void)
{
    ec200_mqtt_config_t cfg = mqtt_cfg();
    lb_on_write("AT+QMTCONN=0,\"dev1\"",
                "\r\nOK\r\n+QMTCONN: 0,0,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_mqtt_connect(&h, &cfg));
}

void test_mqtt_subscribe_ok(void)
{
    lb_on_write("AT+QMTSUB=0,1,\"sensors/+\",1",
                "\r\nOK\r\n+QMTSUB: 0,1,0,1\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_mqtt_subscribe(&h, 0, 1, "sensors/+", EC200_MQTT_QOS1));
}

void test_mqtt_subscribe_validation(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_subscribe(&h, 0, 0, "t", EC200_MQTT_QOS1));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_subscribe(&h, 0, 1, NULL, EC200_MQTT_QOS1));
}

void test_mqtt_publish_binary_safe(void)
{
    /* Regression: payloads containing 0x1A must survive intact (QMTPUBEX,
     * length-parameterised — no Ctrl-Z terminator). */
    static const uint8_t payload[5] = { 'A', 0x1A, 'B', 0x1A, 'C' };

    lb_on_write("AT+QMTPUBEX=0,1,1,0,\"data\",5", "\r\n> ");
    lb_on_write("A\x1a" "B\x1a" "C", "\r\nOK\r\n+QMTPUBEX: 0,1,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS1, false, "data",
                           payload, sizeof(payload)));
}

void test_mqtt_publish_validation(void)
{
    uint8_t p[4] = {0};
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS0, false, "t", p, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS0, false, "t", p,
                           EC200_MAX_PAYLOAD_LEN + 1U));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_publish(&h, 0, 0, EC200_MQTT_QOS1, false, "t", p, 4));
}

static const ec200_mqtt_message_t *g_msg;
static ec200_mqtt_message_t        g_msg_copy;
static void mqtt_cb(const ec200_mqtt_message_t *msg, void *ctx)
{
    (void)ctx;
    g_msg_copy = *msg;
    g_msg = &g_msg_copy;
}

void test_mqtt_message_dispatched_mid_command(void)
{
    /* Regression: a +QMTRECV arriving during another command must reach
     * the message callback instead of being lost as response data. */
    g_msg = NULL;
    ec200_mqtt_set_message_cb(&h, mqtt_cb);

    lb_on_write("AT+CSQ",
        "\r\n+QMTRECV: 0,1,\"sensors/t1\",\"22.5\"\r\n"
        "+CSQ: 20,0\r\n\r\nOK\r\n");
    ec200_signal_quality_t sq;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_signal(&h, &sq));
    TEST_ASSERT_EQUAL_INT(-73, sq.rssi);

    TEST_ASSERT_NOT_NULL(g_msg);
    TEST_ASSERT_EQUAL_STRING("sensors/t1", g_msg->topic);
    TEST_ASSERT_EQUAL_UINT32(4, g_msg->payload_len);
    TEST_ASSERT_EQUAL_MEMORY("22.5", g_msg->payload, 4);

    ec200_mqtt_set_message_cb(&h, NULL);
}

/* =========================================================================
 * HTTP
 * ========================================================================= */

void test_http_set_url_flow(void)
{
    lb_on_write("AT+QHTTPURL=26,80", "\r\nCONNECT\r\n");
    lb_on_write("http://example.com/api/get", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_http_set_url(&h, "http://example.com/api/get"));
}

void test_http_get_async_ok(void)
{
    /* Regression: "OK first, +QHTTPGET URC later" must succeed. */
    lb_on_write("AT+QHTTPGET=30", "\r\nOK\r\n+QHTTPGET: 0,200,512\r\n");
    ec200_http_response_t resp;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_http_get(&h, 30000, &resp));
    TEST_ASSERT_EQUAL_UINT16(200, resp.status_code);
    TEST_ASSERT_EQUAL_UINT32(512, resp.content_length);
}

void test_http_get_module_error(void)
{
    lb_on_write("AT+QHTTPGET=30", "\r\nOK\r\n+QHTTPGET: 703\r\n");
    ec200_http_response_t resp;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_http_get(&h, 30000, &resp));
}

void test_http_post_flow(void)
{
    lb_on_write("contenttype", "\r\nOK\r\n");
    lb_on_write("AT+QHTTPPOST=11,10,10", "\r\nCONNECT\r\n");
    lb_on_write("hello=world",
                "\r\nOK\r\n+QHTTPPOST: 0,200,4\r\n");
    ec200_http_response_t resp;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_http_post(&h, (const uint8_t *)"hello=world", 11,
                        EC200_HTTP_CT_URLENCODED, 10000, &resp));
    TEST_ASSERT_EQUAL_UINT16(200, resp.status_code);
    /* the numeric index, not a MIME string, must go on the wire */
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "\"contenttype\",0"));
}

void test_http_read_body_intact(void)
{
    /* Regression: the body must not lose its first bytes to a drain. */
    lb_on_write("AT+QHTTPREAD=10",
        "\r\nCONNECT\r\nHello Body\r\nOK\r\n+QHTTPREAD: 0\r\n");
    uint8_t body[64];
    uint32_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_http_read(&h, body, sizeof(body), &got, 10000));
    TEST_ASSERT_EQUAL_UINT32(10, got);
    TEST_ASSERT_EQUAL_MEMORY("Hello Body", body, 10);
}

void test_http_read_truncation_reports_overflow(void)
{
    lb_on_write("AT+QHTTPREAD=10",
        "\r\nCONNECT\r\nABCDEFGHIJKLMNOP\r\nOK\r\n+QHTTPREAD: 0\r\n");
    uint8_t body[8];
    uint32_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW,
        ec200_http_read(&h, body, sizeof(body), &got, 10000));
    TEST_ASSERT_EQUAL_UINT32(8, got);
    TEST_ASSERT_EQUAL_MEMORY("ABCDEFGH", body, 8);
}

void test_http_read_crlf_inside_body(void)
{
    /* "\r\n" sequences inside the body must not be eaten as the trailer. */
    lb_on_write("AT+QHTTPREAD=10",
        "\r\nCONNECT\r\nline1\r\nline2\r\nOK\r\n+QHTTPREAD: 0\r\n");
    uint8_t body[64];
    uint32_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_http_read(&h, body, sizeof(body), &got, 10000));
    TEST_ASSERT_EQUAL_UINT32(12, got);
    TEST_ASSERT_EQUAL_MEMORY("line1\r\nline2", body, 12);
}

/* =========================================================================
 * SMS
 * ========================================================================= */

void test_sms_send_flow(void)
{
    lb_on_write("AT+CMGS=\"+1234567890\"", "\r\n> ");
    lb_on_write("Hello!", "\r\n+CMGS: 5\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sms_send(&h, "+1234567890", "Hello!"));
    /* Ctrl-Z terminator must have been transmitted. */
    TEST_ASSERT_NOT_NULL(strchr(lb_tx_data(), '\x1a'));
}

void test_sms_send_validation(void)
{
    char toolong[EC200_MAX_SMS_TEXT_LEN + 2];
    memset(toolong, 'a', sizeof(toolong) - 1U);
    toolong[sizeof(toolong) - 1U] = '\0';

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_send(&h, "+123", toolong));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_send(&h, "+123", "bad\x1amessage"));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_send(&h, NULL, "hi"));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_send(&h, "", "hi"));
}

void test_sms_read_body_intact(void)
{
    /* Regression: the body used to be swallowed by the engine drain and
     * msg->text ended up holding "OK". */
    lb_on_write("AT+CMGR=1",
        "\r\n+CMGR: \"REC READ\",\"+1234567890\",,\"21/01/01,12:00:00+00\""
        "\r\nHello world\r\n\r\nOK\r\n");
    ec200_sms_message_t msg;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sms_read(&h, 1, &msg));
    TEST_ASSERT_EQUAL_STRING("Hello world", msg.text);
    TEST_ASSERT_EQUAL_STRING("+1234567890", msg.sender);
    TEST_ASSERT_EQUAL_INT(EC200_SMS_STAT_REC_READ, msg.stat);
    TEST_ASSERT_EQUAL_STRING("21/01/01,12:00:00+00", msg.timestamp);
}

void test_sms_list_two_messages(void)
{
    lb_on_write("AT+CMGL=\"ALL\"",
        "\r\n+CMGL: 1,\"REC READ\",\"+111\",,\"21/01/01,10:00:00+00\"\r\n"
        "first\r\n"
        "+CMGL: 2,\"REC UNREAD\",\"+222\",,\"21/01/02,11:00:00+00\"\r\n"
        "second\r\n\r\nOK\r\n");
    ec200_sms_message_t msgs[4];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, msgs, 4, &count));
    TEST_ASSERT_EQUAL_UINT8(2, count);
    TEST_ASSERT_EQUAL_INT(1, msgs[0].index);
    TEST_ASSERT_EQUAL_STRING("first", msgs[0].text);
    TEST_ASSERT_EQUAL_STRING("+111", msgs[0].sender);
    TEST_ASSERT_EQUAL_INT(2, msgs[1].index);
    TEST_ASSERT_EQUAL_STRING("second", msgs[1].text);
    TEST_ASSERT_EQUAL_INT(EC200_SMS_STAT_REC_UNREAD, msgs[1].stat);
}

void test_sms_delete_all_validation(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_sms_delete_all(&h, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_sms_delete_all(&h, 5));
}

/* =========================================================================
 * Cross-command stream integrity
 * ========================================================================= */

void test_sequential_commands_stay_in_sync(void)
{
    /* Regression: mixed transaction shapes must not desynchronise. */
    lb_on_write("AT+CPIN?", "\r\n+CPIN: READY\r\n\r\nOK\r\n");
    ec200_sim_status_t sim;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sim_get_status(&h, &sim));

    lb_on_write("AT+QIRD=0,8", "\r\n+QIRD: 3\r\nABC\r\nOK\r\n");
    uint8_t buf[8];
    uint16_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_tcp_recv(&h, 0, buf, sizeof(buf), &got, 1000));
    TEST_ASSERT_EQUAL_UINT16(3, got);

    lb_on_write("AT+CSQ", "\r\n+CSQ: 15,0\r\n\r\nOK\r\n");
    ec200_signal_quality_t sq;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_signal(&h, &sq));
    TEST_ASSERT_EQUAL_INT(-83, sq.rssi);

    TEST_ASSERT_EQUAL_size_t(0, lb_rx_pending());
}

/* =========================================================================
 * Coverage round: remaining flows and validation branches
 * ========================================================================= */

void test_tcp_close_ok(void)
{
    lb_on_write("AT+QICLOSE=0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_tcp_close(&h, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_tcp_close(&h, 12));
}

void test_tcp_get_state_udp(void)
{
    lb_on_write("AT+QISTATE=1,1",
        "\r\n+QISTATE: 1,\"UDP\",\"8.8.8.8\",53,60000,2,1,0,0,\"usbmodem\""
        "\r\n\r\nOK\r\n");
    ec200_socket_t sock;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_tcp_get_state(&h, 1, &sock));
    TEST_ASSERT_EQUAL_INT(EC200_SOCK_UDP, sock.type);
    TEST_ASSERT_TRUE(sock.connected);
}

void test_mqtt_connect_with_credentials(void)
{
    ec200_mqtt_config_t cfg = mqtt_cfg();
    (void)snprintf(cfg.username, sizeof(cfg.username), "%s", "user");
    (void)snprintf(cfg.password, sizeof(cfg.password), "%s", "pw");

    lb_on_write("AT+QMTCONN=0,\"dev1\",\"user\",\"pw\"",
                "\r\nOK\r\n+QMTCONN: 0,0,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_mqtt_connect(&h, &cfg));
}

void test_mqtt_disconnect_close_unsubscribe(void)
{
    lb_on_write("AT+QMTDISC=0", "\r\nOK\r\n+QMTDISC: 0,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_mqtt_disconnect(&h, 0));

    lb_on_write("AT+QMTCLOSE=0", "\r\nOK\r\n+QMTCLOSE: 0,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_mqtt_close(&h, 0));

    lb_on_write("AT+QMTUNS=0,2,\"sensors/+\"",
                "\r\nOK\r\n+QMTUNS: 0,2,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_mqtt_unsubscribe(&h, 0, 2, "sensors/+"));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_unsubscribe(&h, 0, 0, "t"));
}

void test_mqtt_open_param_validation(void)
{
    ec200_mqtt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_mqtt_open(&h, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_mqtt_open(&h, &cfg));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_mqtt_connect(&h, &cfg));
}

void test_http_set_context_validation(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_http_set_context(&h, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_http_set_context(&h, 17));

    lb_on_write("AT+QHTTPCFG=\"contextid\",1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_http_set_context(&h, 1));
}

void test_http_set_url_validation(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_http_set_url(&h, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_http_set_url(&h, ""));
}

void test_http_get_parse_error(void)
{
    lb_on_write("AT+QHTTPGET=30", "\r\nOK\r\n+QHTTPGET: abc\r\n");
    ec200_http_response_t resp;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_http_get(&h, 30000, &resp));
}

void test_http_stop(void)
{
    lb_on_write("AT+QHTTPSTOP", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_http_stop(&h));
}

void test_sms_set_format_and_delete(void)
{
    lb_on_write("AT+CMGF=1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sms_set_format(&h, EC200_SMS_FORMAT_TEXT));

    lb_on_write("AT+CMGD=3", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sms_delete(&h, 3));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_sms_delete(&h, -1));

    lb_on_write("AT+CMGD=1,4", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sms_delete_all(&h, 4));
}

void test_sms_read_empty_slot(void)
{
    lb_on_write("AT+CMGR=9", "\r\nOK\r\n");
    ec200_sms_message_t msg;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_sms_read(&h, 9, &msg));
}

void test_sms_list_stored_states(void)
{
    lb_on_write("AT+CMGL=\"ALL\"",
        "\r\n+CMGL: 3,\"STO UNSENT\",\"+333\",,\"21/01/03,09:00:00+00\"\r\n"
        "draft\r\n"
        "+CMGL: 4,\"STO SENT\",\"+444\",,\"21/01/04,09:30:00+00\"\r\n"
        "sent one\r\n\r\nOK\r\n");
    ec200_sms_message_t msgs[4];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, msgs, 4, &count));
    TEST_ASSERT_EQUAL_UINT8(2, count);
    TEST_ASSERT_EQUAL_INT(EC200_SMS_STAT_STO_UNSENT, msgs[0].stat);
    TEST_ASSERT_EQUAL_INT(EC200_SMS_STAT_STO_SENT,   msgs[1].stat);
}

/* =========================================================================
 * Coverage round 2: failure paths and malformed responses
 * ========================================================================= */

void test_tcp_open_bad_type_and_failures(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_open(&h, 1, 0, (ec200_sock_type_t)7, "x", 80,
                       EC200_ACCESS_BUFFER));

    lb_on_write("AT+QIOPEN", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_tcp_open(&h, 1, 0, EC200_SOCK_TCP, "x", 80,
                       EC200_ACCESS_BUFFER));

    /* Result URC missing the <err> field */
    lb_on_write("AT+QIOPEN", "\r\nOK\r\n+QIOPEN: 0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_tcp_open(&h, 1, 0, EC200_SOCK_TCP, "x", 80,
                       EC200_ACCESS_BUFFER));
}

void test_tcp_send_param_and_failures(void)
{
    uint8_t d[4] = {0};
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_tcp_send(&h, 0, NULL, 4));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_tcp_send(&h, 0, d, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_tcp_send(&h, 12, d, 4));

    /* Payload write fails after the prompt */
    lb_on_write("AT+QISEND=0,4", "\r\n> ");
    lb_fail_write_after(1);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO, ec200_tcp_send(&h, 0, d, 4));

    /* No SEND OK after the payload */
    SETUP_MODEM(&h);
    lb_on_write("AT+QISEND=0,4", "\r\n> ");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT, ec200_tcp_send(&h, 0, d, 4));
}

void test_tcp_recv_param_and_failures(void)
{
    uint8_t buf[16];
    uint16_t got;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_recv(&h, 0, NULL, 16, &got, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_recv(&h, 0, buf, 0, &got, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_recv(&h, 0, buf, 16, NULL, 100));

    lb_on_write("AT+QIRD=0,16", "\r\n+CME ERROR: 3\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_tcp_recv(&h, 0, buf, 16, &got, 1000));

    /* Non-numeric length */
    lb_on_write("AT+QIRD=0,16", "\r\n+QIRD: x\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_tcp_recv(&h, 0, buf, 16, &got, 1000));

    /* Announced length exceeds what was requested */
    SETUP_MODEM(&h);
    lb_on_write("AT+QIRD=0,16", "\r\n+QIRD: 999\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_tcp_recv(&h, 0, buf, 16, &got, 1000));

    /* Announced 5 bytes but only 3 arrive */
    SETUP_MODEM(&h);
    lb_on_write("AT+QIRD=0,16", "\r\n+QIRD: 5\r\nABC");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_tcp_recv(&h, 0, buf, 16, &got, 300));
    TEST_ASSERT_EQUAL_UINT16(3, got);
}

void test_tcp_get_state_param_and_failures(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_get_state(&h, 0, NULL));

    ec200_socket_t sock;
    lb_on_write("AT+QISTATE=1,0", "\r\n+CME ERROR: 3\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_tcp_get_state(&h, 0, &sock));

    lb_on_write("AT+QISTATE=1,0", "\r\n+QISTATE: 0,\"TCP\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_tcp_get_state(&h, 0, &sock));
}

void test_tcp_bytes_available_param_and_failures(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_bytes_available(&h, 0, NULL));

    uint32_t avail;
    lb_on_write("AT+QIRD=0,0", "\r\n+CME ERROR: 3\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_tcp_bytes_available(&h, 0, &avail));

    /* Missing <unread> field */
    lb_on_write("AT+QIRD=0,0", "\r\n+QIRD: 1,2\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_tcp_bytes_available(&h, 0, &avail));
}

void test_mqtt_result_parse_error(void)
{
    ec200_mqtt_config_t cfg = mqtt_cfg();
    lb_on_write("AT+QMTOPEN", "\r\nOK\r\n+QMTOPEN: 0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_mqtt_open(&h, &cfg));
}

void test_mqtt_command_failure_paths(void)
{
    ec200_mqtt_config_t cfg = mqtt_cfg();

    lb_on_write("AT+QMTOPEN", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_mqtt_open(&h, &cfg));

    lb_on_write("AT+QMTCONN", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_mqtt_connect(&h, &cfg));

    lb_on_write("AT+QMTDISC", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_mqtt_disconnect(&h, 0));

    lb_on_write("AT+QMTCLOSE", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_mqtt_close(&h, 0));

    lb_on_write("AT+QMTSUB", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_mqtt_subscribe(&h, 0, 1, "t", EC200_MQTT_QOS0));

    lb_on_write("AT+QMTUNS", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_mqtt_unsubscribe(&h, 0, 1, "t"));
}

void test_mqtt_recv_urc_malformed_and_truncated(void)
{
    g_msg = NULL;
    ec200_mqtt_set_message_cb(&h, mqtt_cb);

    /* No quotes at all: ignored */
    lb_feed("+QMTRECV: 0,1\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_NULL(g_msg);

    /* Unterminated topic quote: ignored */
    lb_feed("+QMTRECV: 0,1,\"top\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_NULL(g_msg);

    /* Oversized topic: truncated to fit */
    static char urc[2000];
    int n = snprintf(urc, sizeof(urc), "+QMTRECV: 0,1,\"");
    memset(&urc[n], 'T', 200);
    n += 200;
    n += snprintf(&urc[n], sizeof(urc) - (size_t)n, "\",\"x\"\r\n");
    (void)n;
    lb_feed(urc);
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_NOT_NULL(g_msg);
    TEST_ASSERT_EQUAL_size_t(EC200_MAX_TOPIC_LEN - 1U,
                             strlen(g_msg->topic));

    /* Oversized payload: clamped to EC200_MAX_PAYLOAD_LEN */
    g_msg = NULL;
    n = snprintf(urc, sizeof(urc), "+QMTRECV: 0,1,\"t\",\"");
    memset(&urc[n], 'P', 1500);
    n += 1500;
    n += snprintf(&urc[n], sizeof(urc) - (size_t)n, "\"\r\n");
    (void)n;
    lb_feed(urc);
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_NOT_NULL(g_msg);
    TEST_ASSERT_EQUAL_UINT32(EC200_MAX_PAYLOAD_LEN, g_msg->payload_len);

    /* Guard: registered handler with the callback field cleared */
    h.mqtt_msg_cb = NULL; /* white-box: registration stays active */
    lb_feed("+QMTRECV: 0,1,\"t\",\"x\"\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));

    ec200_mqtt_set_message_cb(&h, NULL);
}

void test_mqtt_publish_more_validation_and_failures(void)
{
    uint8_t p[4] = {0};

    /* Topic validation */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS0, false, "", p, 4));
    static char longtopic[EC200_MAX_TOPIC_LEN + 8];
    memset(longtopic, 't', sizeof(longtopic) - 1U);
    longtopic[sizeof(longtopic) - 1U] = '\0';
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS0, false, longtopic,
                           p, 4));

    /* ERROR instead of the prompt */
    lb_on_write("AT+QMTPUBEX", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS0, false, "t", p, 4));

    /* Payload write fails */
    SETUP_MODEM(&h);
    lb_on_write("AT+QMTPUBEX", "\r\n> ");
    lb_fail_write_after(1);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS0, false, "t", p, 4));

    /* No result URC after the payload */
    SETUP_MODEM(&h);
    lb_on_write("AT+QMTPUBEX", "\r\n> ");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS0, false, "t", p, 4));
}

void test_mqtt_set_cb_null_handle(void)
{
    ec200_mqtt_set_message_cb(NULL, mqtt_cb); /* must not crash */
}

void test_http_timeout_secs_clamps(void)
{
    /* 500 ms clamps up to 1 s */
    lb_on_write("AT+QHTTPGET=1", "\r\nOK\r\n+QHTTPGET: 0,200,1\r\n");
    ec200_http_response_t resp;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_http_get(&h, 500, &resp));
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "AT+QHTTPGET=1\r"));

    /* Huge timeout clamps down to 65535 s */
    lb_on_write("AT+QHTTPGET=65535", "\r\nOK\r\n+QHTTPGET: 0,200,1\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_http_get(&h, 4000000000U, &resp));
}

void test_http_get_and_set_url_failures(void)
{
    ec200_http_response_t resp;
    lb_on_write("AT+QHTTPGET", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_http_get(&h, 30000, &resp));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_http_get(&h, 30000, NULL));

    /* set_url: CONNECT refused */
    lb_on_write("AT+QHTTPURL", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_http_set_url(&h, "http://x.com"));

    /* set_url: URL write fails */
    SETUP_MODEM(&h);
    lb_on_write("AT+QHTTPURL", "\r\nCONNECT\r\n");
    lb_fail_write_after(1);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_http_set_url(&h, "http://x.com"));
}

void test_http_post_param_and_failures(void)
{
    ec200_http_response_t resp;
    const uint8_t body[4] = "abc";

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_http_post(&h, NULL, 4, EC200_HTTP_CT_URLENCODED, 1000, &resp));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_http_post(&h, body, 0, EC200_HTTP_CT_URLENCODED, 1000, &resp));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_http_post(&h, body, 70000, EC200_HTTP_CT_URLENCODED, 1000,
                        &resp));
    /* out-of-range content type (both arms of the range check) */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_http_post(&h, body, 3, (ec200_http_content_type_t)9, 1000,
                        &resp));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_http_post(&h, body, 3, (ec200_http_content_type_t)-1, 1000,
                        &resp));

    /* content-type configuration fails */
    lb_on_write("\"contenttype\",1", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_http_post(&h, body, 3, EC200_HTTP_CT_TEXT_PLAIN, 1000, &resp));

    /* CONNECT refused */
    SETUP_MODEM(&h);
    lb_on_write("contenttype", "\r\nOK\r\n");
    lb_on_write("AT+QHTTPPOST", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_http_post(&h, body, 3, EC200_HTTP_CT_URLENCODED, 1000, &resp));

    /* Body write fails */
    SETUP_MODEM(&h);
    lb_on_write("contenttype", "\r\nOK\r\n");
    lb_on_write("AT+QHTTPPOST", "\r\nCONNECT\r\n");
    lb_fail_write_after(2);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_http_post(&h, body, 3, EC200_HTTP_CT_URLENCODED, 1000, &resp));

    /* No OK after the body */
    SETUP_MODEM(&h);
    lb_on_write("contenttype", "\r\nOK\r\n");
    lb_on_write("AT+QHTTPPOST", "\r\nCONNECT\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_http_post(&h, body, 3, EC200_HTTP_CT_URLENCODED, 300, &resp));

    /* OK but no result URC */
    SETUP_MODEM(&h);
    lb_on_write("contenttype", "\r\nOK\r\n");
    lb_on_write("AT+QHTTPPOST", "\r\nCONNECT\r\n");
    lb_on_write("abc", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_http_post(&h, body, 3, EC200_HTTP_CT_URLENCODED, 300, &resp));
}

void test_http_read_param_and_failures(void)
{
    uint8_t buf[16];
    uint32_t got;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_http_read(&h, NULL, 16, &got, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_http_read(&h, buf, 0, &got, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_http_read(&h, buf, 16, NULL, 100));

    /* CONNECT refused */
    lb_on_write("AT+QHTTPREAD", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_http_read(&h, buf, sizeof(buf), &got, 1000));

    /* Body never terminated by the OK trailer */
    SETUP_MODEM(&h);
    lb_on_write("AT+QHTTPREAD", "\r\nCONNECT\r\npartial-body");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_http_read(&h, buf, sizeof(buf), &got, 300));
}

void test_http_read_trailer_flush_paths(void)
{
    /* CR that does not start the trailer, and a flush into a full buffer */
    lb_on_write("AT+QHTTPREAD=1",
        "\r\nCONNECT\r\nABCD\r\rEF\r\nOK\r\n+QHTTPREAD: 0\r\n");
    uint8_t buf[4];
    uint32_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW,
        ec200_http_read(&h, buf, sizeof(buf), &got, 1000));
    TEST_ASSERT_EQUAL_UINT32(4, got);
    TEST_ASSERT_EQUAL_MEMORY("ABCD", buf, 4);
}

void test_http_read_discard_cap(void)
{
    /* >64 KB of overflow body trips the discard guard early. */
    static char bigresp[70032];
    int n = snprintf(bigresp, sizeof(bigresp), "\r\nCONNECT\r\n");
    memset(&bigresp[n], 'X', 70000);
    bigresp[n + 70000] = '\0';

    lb_on_write("AT+QHTTPREAD=1", bigresp);

    uint8_t buf[8];
    uint32_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW,
        ec200_http_read(&h, buf, sizeof(buf), &got, 1000));
    TEST_ASSERT_EQUAL_UINT32(8, got);
}

void test_http_read_result_urc_error(void)
{
    lb_on_write("AT+QHTTPREAD=1",
        "\r\nCONNECT\r\nbody\r\nOK\r\n+QHTTPREAD: 715\r\n");
    uint8_t buf[16];
    uint32_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_http_read(&h, buf, sizeof(buf), &got, 1000));
}

void test_sms_set_format_invalid(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_set_format(&h, (ec200_sms_format_t)2));
}

void test_sms_send_number_too_long(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_send(&h, "+123456789012345678901234", "hi"));
}

void test_sms_send_flow_failures(void)
{
    /* ERROR instead of the prompt */
    lb_on_write("AT+CMGS", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_sms_send(&h, "+123", "hi"));

    /* Text write fails */
    SETUP_MODEM(&h);
    lb_on_write("AT+CMGS", "\r\n> ");
    lb_fail_write_after(1);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO, ec200_sms_send(&h, "+123", "hi"));

    /* Ctrl-Z write fails */
    SETUP_MODEM(&h);
    lb_on_write("AT+CMGS", "\r\n> ");
    lb_fail_write_after(2);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO, ec200_sms_send(&h, "+123", "hi"));

    /* No +CMGS confirmation */
    SETUP_MODEM(&h);
    lb_on_write("AT+CMGS", "\r\n> ");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_sms_send(&h, "+123", "hi"));
}

void test_sms_read_param_error_and_long_body(void)
{
    ec200_sms_message_t msg;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_sms_read(&h, 1, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_sms_read(&h, -1, &msg));

    lb_on_write("AT+CMGR=1", "\r\n+CMS ERROR: 321\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CMS, ec200_sms_read(&h, 1, &msg));

    /* Body longer than EC200_MAX_SMS_TEXT_LEN is clamped */
    static char resp[512];
    int n = snprintf(resp, sizeof(resp),
                     "\r\n+CMGR: \"REC READ\",\"+1\",,\"ts\"\r\n");
    memset(&resp[n], 'b', 200);
    n += 200;
    n += snprintf(&resp[n], sizeof(resp) - (size_t)n, "\r\nOK\r\n");
    (void)n;
    lb_on_write("AT+CMGR=1", resp);
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sms_read(&h, 1, &msg));
    TEST_ASSERT_EQUAL_size_t(EC200_MAX_SMS_TEXT_LEN, strlen(msg.text));
}

void test_sms_read_malformed_headers(void)
{
    ec200_sms_message_t msg;

    /* No quotes at all: fields stay empty, call still succeeds */
    lb_on_write("AT+CMGR=1", "\r\n+CMGR: 0,,7\r\nbody\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sms_read(&h, 1, &msg));
    TEST_ASSERT_EQUAL_STRING("", msg.sender);

    /* Unterminated quote */
    lb_on_write("AT+CMGR=1", "\r\n+CMGR: \"REC READ\r\nbody\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sms_read(&h, 1, &msg));

    /* Oversized stat string is truncated safely */
    lb_on_write("AT+CMGR=1",
        "\r\n+CMGR: \"THISSTATUSISWAYTOOLONG\",\"+1\",,\"ts\"\r\n"
        "body\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sms_read(&h, 1, &msg));
}

void test_sms_read_alpha_field_present(void)
{
    /* Non-empty alpha field: timestamp must come from the NEXT quote. */
    lb_on_write("AT+CMGR=1",
        "\r\n+CMGR: \"REC READ\",\"+123\",\"Alice\","
        "\"21/01/01,12:00:00+00\"\r\nhello\r\nOK\r\n");
    ec200_sms_message_t msg;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sms_read(&h, 1, &msg));
    TEST_ASSERT_EQUAL_STRING("21/01/01,12:00:00+00", msg.timestamp);
    TEST_ASSERT_EQUAL_STRING("hello", msg.text);
}

void test_sms_list_param_error_and_edge_cases(void)
{
    ec200_sms_message_t msgs[2];
    uint8_t count;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, NULL, 2, &count));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, msgs, 0, &count));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_list(&h, (ec200_sms_stat_t)5, msgs, 2, &count));

    lb_on_write("AT+CMGL", "\r\n+CMS ERROR: 302\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CMS,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, msgs, 2, &count));

    /* Header as the very last line (no body) */
    lb_on_write("AT+CMGL",
        "\r\n+CMGL: 1,\"REC READ\",\"+1\",,\"ts\"\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, msgs, 2, &count));
    TEST_ASSERT_EQUAL_UINT8(0, count);

    /* Junk lines around the entry; trailing junk without newline */
    lb_on_write("AT+CMGL",
        "\r\nnoise\r\n+CMGL: 1,\"FOO\",\"+1\",,\"ts\"\r\nbody\r\ntail"
        "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, msgs, 2, &count));
    TEST_ASSERT_EQUAL_UINT8(1, count);
    TEST_ASSERT_EQUAL_INT(EC200_SMS_STAT_ALL, msgs[0].stat); /* unknown */
    TEST_ASSERT_EQUAL_STRING("body", msgs[0].text);
}

void test_sms_list_long_body_clamped(void)
{
    static char resp[512];
    int n = snprintf(resp, sizeof(resp),
                     "\r\n+CMGL: 1,\"REC READ\",\"+1\",,\"ts\"\r\n");
    memset(&resp[n], 'c', 200);
    n += 200;
    n += snprintf(&resp[n], sizeof(resp) - (size_t)n, "\r\n\r\nOK\r\n");
    (void)n;
    lb_on_write("AT+CMGL", resp);

    ec200_sms_message_t msgs[2];
    uint8_t count;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, msgs, 2, &count));
    TEST_ASSERT_EQUAL_UINT8(1, count);
    TEST_ASSERT_EQUAL_size_t(EC200_MAX_SMS_TEXT_LEN, strlen(msgs[0].text));
}

/* =========================================================================
 * Branch-permutation round: every arm of every guard
 * ========================================================================= */

void test_branch_tcp_param_arms(void)
{
    uint8_t d[4] = {0};
    uint8_t buf[8];
    uint16_t got;
    ec200_socket_t sock;
    uint32_t avail;

    /* Empty host, out-of-range ctx */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_open(&h, 1, 0, EC200_SOCK_TCP, "", 80,
                       EC200_ACCESS_BUFFER));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_open(&h, 17, 0, EC200_SOCK_TCP, "x", 80,
                       EC200_ACCESS_BUFFER));

    /* Oversized send, out-of-range conn ids */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_send(&h, 0, d, EC200_MAX_PAYLOAD_LEN + 1U));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_recv(&h, 12, buf, sizeof(buf), &got, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_get_state(&h, 12, &sock));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_bytes_available(&h, 12, &avail));
}

void test_branch_tcp_negative_lengths(void)
{
    uint8_t buf[16];
    uint16_t got;
    lb_on_write("AT+QIRD=0,16", "\r\n+QIRD: -5\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_tcp_recv(&h, 0, buf, sizeof(buf), &got, 1000));

    /* Fresh handle: the aborted recv above leaves its OK unconsumed. */
    SETUP_MODEM(&h);
    uint32_t avail;
    lb_on_write("AT+QIRD=0,0", "\r\n+QIRD: 1,2,-3\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_tcp_bytes_available(&h, 0, &avail));
}

void test_branch_mqtt_param_arms(void)
{
    static char longtopic[EC200_MAX_TOPIC_LEN + 8];
    memset(longtopic, 't', sizeof(longtopic) - 1U);
    longtopic[sizeof(longtopic) - 1U] = '\0';
    uint8_t p[4] = {0};

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_mqtt_connect(&h, NULL));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_subscribe(&h, 0, 1, "", EC200_MQTT_QOS0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_subscribe(&h, 0, 1, longtopic, EC200_MQTT_QOS0));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_unsubscribe(&h, 0, 1, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_unsubscribe(&h, 0, 1, ""));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_unsubscribe(&h, 0, 1, longtopic));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS0, false, NULL, p, 4));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS0, false, "t", NULL, 4));
}

void test_branch_mqtt_publish_retain(void)
{
    uint8_t p[4] = {'a', 'b', 'c', 'd'};
    lb_on_write("AT+QMTPUBEX=0,1,0,1,\"t\",4", "\r\n> ");
    lb_on_write("abcd", "\r\nOK\r\n+QMTPUBEX: 0,1,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_mqtt_publish(&h, 0, 1, EC200_MQTT_QOS0, true, "t", p, 4));
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "AT+QMTPUBEX=0,1,0,1,"));
}

void test_branch_mqtt_recv_payload_arms(void)
{
    g_msg = NULL;
    ec200_mqtt_set_message_cb(&h, mqtt_cb);

    /* Topic only — no payload quotes at all */
    lb_feed("+QMTRECV: 0,1,\"t\"\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_NOT_NULL(g_msg);
    TEST_ASSERT_EQUAL_UINT32(0, g_msg->payload_len);

    /* Opening payload quote without a closing one */
    g_msg = NULL;
    lb_feed("+QMTRECV: 0,1,\"t\",\"x\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_NOT_NULL(g_msg);
    TEST_ASSERT_EQUAL_UINT32(0, g_msg->payload_len);

    ec200_mqtt_set_message_cb(&h, NULL);
}

void test_branch_http_param_arms(void)
{
    /* Oversized URL */
    static char longurl[EC200_MAX_URL_LEN + 8];
    memset(longurl, 'u', sizeof(longurl) - 1U);
    longurl[sizeof(longurl) - 1U] = '\0';
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_http_set_url(&h, longurl));

    /* NULL response summary for POST */
    const uint8_t body[4] = "abc";
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_http_post(&h, body, 3, EC200_HTTP_CT_URLENCODED, 1000, NULL));
}

void test_branch_http_read_io_error_mid_body(void)
{
    /* Transport dies while streaming the body */
    lb_on_write("AT+QHTTPREAD=1", "\r\nCONNECT\r\npartial");
    lb_set_read_error_when_empty(true);
    uint8_t buf[32];
    uint32_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_http_read(&h, buf, sizeof(buf), &got, 1000));
}

void test_branch_http_read_malformed_result_urc(void)
{
    /* Unparseable +QHTTPREAD result is tolerated (body already read) */
    lb_on_write("AT+QHTTPREAD=1",
        "\r\nCONNECT\r\nbody\r\nOK\r\n+QHTTPREAD: x\r\n");
    uint8_t buf[16];
    uint32_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_http_read(&h, buf, sizeof(buf), &got, 1000));
    TEST_ASSERT_EQUAL_UINT32(4, got);
}

void test_branch_sms_arms(void)
{
    /* PDU is a valid format */
    lb_on_write("AT+CMGF=0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sms_set_format(&h, EC200_SMS_FORMAT_PDU));

    /* NULL text */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_send(&h, "+123", NULL));

    /* NULL count output */
    ec200_sms_message_t msgs[2];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, msgs, 2, NULL));
}

void test_branch_sms_read_header_only(void)
{
    /* +CMGR header with no body line at all */
    lb_on_write("AT+CMGR=1",
        "\r\n+CMGR: \"REC READ\",\"+123\",,\"ts\"\r\nOK\r\n");
    ec200_sms_message_t msg;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sms_read(&h, 1, &msg));
    TEST_ASSERT_EQUAL_STRING("", msg.text);
    TEST_ASSERT_EQUAL_STRING("+123", msg.sender);
}

void test_branch_sms_alpha_field_low_ascii(void)
{
    /* Alpha field starting with a char BELOW '0' ('+' = 0x2B) */
    lb_on_write("AT+CMGR=1",
        "\r\n+CMGR: \"REC READ\",\"+123\",\"+ali\","
        "\"21/01/01,12:00:00+00\"\r\nhello\r\nOK\r\n");
    ec200_sms_message_t msg;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sms_read(&h, 1, &msg));
    TEST_ASSERT_EQUAL_STRING("21/01/01,12:00:00+00", msg.timestamp);
}

void test_branch_sms_list_empty(void)
{
    /* No stored messages: response body is empty. */
    lb_on_write("AT+CMGL", "\r\nOK\r\n");
    ec200_sms_message_t msgs[2];
    uint8_t count = 0xFF;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, msgs, 2, &count));
    TEST_ASSERT_EQUAL_UINT8(0, count);
}

void test_branch_sms_list_capacity_stop(void)
{
    /* More messages available than the caller's array holds */
    lb_on_write("AT+CMGL",
        "\r\n+CMGL: 1,\"REC READ\",\"+1\",,\"ts\"\r\none\r\n"
        "+CMGL: 2,\"REC READ\",\"+2\",,\"ts\"\r\ntwo\r\n\r\nOK\r\n");
    ec200_sms_message_t msgs[1];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sms_list(&h, EC200_SMS_STAT_ALL, msgs, 1, &count));
    TEST_ASSERT_EQUAL_UINT8(1, count);
    TEST_ASSERT_EQUAL_STRING("one", msgs[0].text);
}

/* =========================================================================
 * PPP control plane
 * ========================================================================= */

void test_ppp_dial_and_data_mode_guard(void)
{
    lb_on_write("ATD*99***1#", "\r\nCONNECT 150000000\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ppp_dial(&h, 1));
    TEST_ASSERT_TRUE(ec200_ppp_in_data_mode(&h));

    /* Every AT transaction API must refuse while PPP owns the UART. */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_BUSY, ec200_check_at(&h));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_BUSY, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_BUSY, ec200_ppp_dial(&h, 1));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_BUSY, ec200_ppp_resume(&h));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ppp_hangup(&h));

    /* Escape returns to command mode; "+++" goes out with NO terminator. */
    lb_on_write("+++", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ppp_escape(&h));
    TEST_ASSERT_FALSE(ec200_ppp_in_data_mode(&h));
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "+++"));
    TEST_ASSERT_NULL(strstr(lb_tx_data(), "+++\r"));

    /* AT transactions work again. */
    lb_on_write("AT\r", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_check_at(&h));
}

void test_ppp_resume_and_disconnect(void)
{
    lb_on_write("ATD*99***1#", "\r\nCONNECT\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ppp_dial(&h, 1));

    lb_on_write("+++", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ppp_escape(&h));

    /* ATO re-enters the suspended session. */
    lb_on_write("ATO", "\r\nCONNECT\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ppp_resume(&h));
    TEST_ASSERT_TRUE(ec200_ppp_in_data_mode(&h));

    /* disconnect = escape + hangup from data mode... */
    lb_on_write("+++", "\r\nOK\r\n");
    lb_on_write("ATH", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ppp_disconnect(&h));
    TEST_ASSERT_FALSE(ec200_ppp_in_data_mode(&h));

    /* ...and plain ATH from command mode. */
    lb_on_write("ATH", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ppp_disconnect(&h));
}

void test_ppp_resume_session_gone(void)
{
    lb_on_write("ATD*99***1#", "\r\nCONNECT\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ppp_dial(&h, 1));
    lb_on_write("+++", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ppp_escape(&h));

    lb_on_write("ATO", "\r\nNO CARRIER\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_ppp_resume(&h));
    TEST_ASSERT_FALSE(ec200_ppp_in_data_mode(&h));
}

void test_ppp_dial_failures(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ppp_dial(NULL, 1));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ppp_dial(&h, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ppp_dial(&h, 17));

    lb_on_write("ATD*99***1#", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_ppp_dial(&h, 1));
    TEST_ASSERT_FALSE(ec200_ppp_in_data_mode(&h));

    /* Regression target: NO CARRIER must fail fast, not time out. */
    lb_on_write("ATD*99***1#", "\r\nNO CARRIER\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_ppp_dial(&h, 1));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT, ec200_ppp_dial(&h, 1));
}

void test_ppp_escape_failures_and_null_args(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ppp_escape(NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ppp_resume(NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ppp_hangup(NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ppp_disconnect(NULL));
    TEST_ASSERT_FALSE(ec200_ppp_in_data_mode(NULL));

    /* Escape in command mode is refused ("+++" would pollute the buffer). */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ppp_escape(&h));

    /* No OK after the escape: stays in data mode. */
    lb_on_write("ATD*99***1#", "\r\nCONNECT\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ppp_dial(&h, 1));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT, ec200_ppp_escape(&h));
    TEST_ASSERT_TRUE(ec200_ppp_in_data_mode(&h));

    /* disconnect propagates the failing escape. */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT, ec200_ppp_disconnect(&h));

    /* Transport dies during the escape write. */
    lb_set_io_error(true);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO, ec200_ppp_escape(&h));
    TEST_ASSERT_TRUE(ec200_ppp_in_data_mode(&h));
}

/* ========================================================================= */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tcp_open_async_ok);
    RUN_TEST(test_tcp_open_module_error);
    RUN_TEST(test_tcp_open_param_validation);
    RUN_TEST(test_tcp_send_ok);
    RUN_TEST(test_tcp_send_fail_status);
    RUN_TEST(test_tcp_send_error_instead_of_prompt);
    RUN_TEST(test_tcp_recv_full_payload);
    RUN_TEST(test_tcp_recv_no_data);
    RUN_TEST(test_tcp_get_state_connected);
    RUN_TEST(test_tcp_bytes_available);
    RUN_TEST(test_mqtt_open_async_ok);
    RUN_TEST(test_mqtt_open_pdp_error);
    RUN_TEST(test_mqtt_connect_ok);
    RUN_TEST(test_mqtt_subscribe_ok);
    RUN_TEST(test_mqtt_subscribe_validation);
    RUN_TEST(test_mqtt_publish_binary_safe);
    RUN_TEST(test_mqtt_publish_validation);
    RUN_TEST(test_mqtt_message_dispatched_mid_command);
    RUN_TEST(test_http_set_url_flow);
    RUN_TEST(test_http_get_async_ok);
    RUN_TEST(test_http_get_module_error);
    RUN_TEST(test_http_post_flow);
    RUN_TEST(test_http_read_body_intact);
    RUN_TEST(test_http_read_truncation_reports_overflow);
    RUN_TEST(test_http_read_crlf_inside_body);
    RUN_TEST(test_sms_send_flow);
    RUN_TEST(test_sms_send_validation);
    RUN_TEST(test_sms_read_body_intact);
    RUN_TEST(test_sms_list_two_messages);
    RUN_TEST(test_sms_delete_all_validation);
    RUN_TEST(test_sequential_commands_stay_in_sync);
    RUN_TEST(test_tcp_close_ok);
    RUN_TEST(test_tcp_get_state_udp);
    RUN_TEST(test_mqtt_connect_with_credentials);
    RUN_TEST(test_mqtt_disconnect_close_unsubscribe);
    RUN_TEST(test_mqtt_open_param_validation);
    RUN_TEST(test_http_set_context_validation);
    RUN_TEST(test_http_set_url_validation);
    RUN_TEST(test_http_get_parse_error);
    RUN_TEST(test_http_stop);
    RUN_TEST(test_sms_set_format_and_delete);
    RUN_TEST(test_sms_read_empty_slot);
    RUN_TEST(test_sms_list_stored_states);
    RUN_TEST(test_tcp_open_bad_type_and_failures);
    RUN_TEST(test_tcp_send_param_and_failures);
    RUN_TEST(test_tcp_recv_param_and_failures);
    RUN_TEST(test_tcp_get_state_param_and_failures);
    RUN_TEST(test_tcp_bytes_available_param_and_failures);
    RUN_TEST(test_mqtt_result_parse_error);
    RUN_TEST(test_mqtt_command_failure_paths);
    RUN_TEST(test_mqtt_recv_urc_malformed_and_truncated);
    RUN_TEST(test_mqtt_publish_more_validation_and_failures);
    RUN_TEST(test_mqtt_set_cb_null_handle);
    RUN_TEST(test_http_timeout_secs_clamps);
    RUN_TEST(test_http_get_and_set_url_failures);
    RUN_TEST(test_http_post_param_and_failures);
    RUN_TEST(test_http_read_param_and_failures);
    RUN_TEST(test_http_read_trailer_flush_paths);
    RUN_TEST(test_http_read_discard_cap);
    RUN_TEST(test_http_read_result_urc_error);
    RUN_TEST(test_sms_set_format_invalid);
    RUN_TEST(test_sms_send_number_too_long);
    RUN_TEST(test_sms_send_flow_failures);
    RUN_TEST(test_sms_read_param_error_and_long_body);
    RUN_TEST(test_sms_read_malformed_headers);
    RUN_TEST(test_sms_read_alpha_field_present);
    RUN_TEST(test_sms_list_param_error_and_edge_cases);
    RUN_TEST(test_sms_list_long_body_clamped);
    RUN_TEST(test_branch_tcp_param_arms);
    RUN_TEST(test_branch_tcp_negative_lengths);
    RUN_TEST(test_branch_mqtt_param_arms);
    RUN_TEST(test_branch_mqtt_publish_retain);
    RUN_TEST(test_branch_mqtt_recv_payload_arms);
    RUN_TEST(test_branch_http_param_arms);
    RUN_TEST(test_branch_http_read_io_error_mid_body);
    RUN_TEST(test_branch_http_read_malformed_result_urc);
    RUN_TEST(test_branch_sms_arms);
    RUN_TEST(test_branch_sms_read_header_only);
    RUN_TEST(test_branch_sms_alpha_field_low_ascii);
    RUN_TEST(test_branch_sms_list_empty);
    RUN_TEST(test_branch_sms_list_capacity_stop);
    RUN_TEST(test_ppp_dial_and_data_mode_guard);
    RUN_TEST(test_ppp_resume_and_disconnect);
    RUN_TEST(test_ppp_resume_session_gone);
    RUN_TEST(test_ppp_dial_failures);
    RUN_TEST(test_ppp_escape_failures_and_null_args);
    return UNITY_END();
}
