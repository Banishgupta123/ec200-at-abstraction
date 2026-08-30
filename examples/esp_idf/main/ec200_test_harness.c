/**
 * @file ec200_test_harness.c
 * @brief On-target exhaustive test harness for the EC200 library.
 *
 * Exercises every public API against a real EC200U module — happy path,
 * bad path (validation / error propagation), and security (AT-command
 * injection, oversized inputs, binary-payload safety) — printing PASS/FAIL
 * per case with a final summary.
 *
 * Board: isolated_dcu_esp32 V1.0.  Select this file in main/CMakeLists.txt.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_random.h"

#include "ec200.h"
#include "test_certs.h"

/* ---- board wiring (see ec200_demo_main.c) ------------------------------- */
#define UART_NUM        UART_NUM_1
#define UART_TX_PIN     (8)
#define UART_RX_PIN     (19)
#define UART_BAUD       (115200)
#define RX_BUF          (4096)
#define PWRKEY_GPIO     (9)
#define RESET_GPIO      (10)

/* ---- test parameters ---------------------------------------------------- */
#define APN             "internet"
#define SMS_DEST        ""    /* MASKED: send disabled (inbox/bad-path only) */
#define TCP_HOST        "tcpbin.com"
#define TCP_PORT        (4242)                /* line echo */
#define MQTT_HOST       "test.mosquitto.org"
#define MQTT_PORT       (1883)
#define HTTP_URL        "http://httpbin.org/get"

static ec200_handle_t m;
static int g_pass, g_fail, g_skip;

/* ------------------------------------------------------------------------- */
static void ck(const char *desc, bool ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", desc);
    if (ok) { g_pass++; } else { g_fail++; }
}

static void ck_st(const char *desc, ec200_status_t got, ec200_status_t want)
{
    bool ok = (got == want);
    printf("  [%s] %s (got %s", ok ? "PASS" : "FAIL", desc,
           ec200_status_str(got));
    if (got == EC200_ERR_CME) { printf(":%d", ec200_at_last_cme_error(&m)); }
    if (got == EC200_ERR_CMS) { printf(":%d", ec200_at_last_cms_error(&m)); }
    printf(")\n");
    if (ok) { g_pass++; } else { g_fail++; }
}

static void skip(const char *desc, const char *why)
{
    printf("  [SKIP] %s (%s)\n", desc, why);
    g_skip++;
}

static void banner(const char *mod)
{
    printf("\n==== %s ====\n", mod);
}

/* ---- transport ---------------------------------------------------------- */
static int tx(const uint8_t *d, uint16_t n, void *c)
{ (void)c; return uart_write_bytes(UART_NUM, d, n); }
static int rx(uint8_t *d, uint16_t n, uint32_t to, void *c)
{ (void)c; return uart_read_bytes(UART_NUM, d, n, pdMS_TO_TICKS(to)); }
static void dly(uint32_t ms, void *c) { (void)c; vTaskDelay(pdMS_TO_TICKS(ms)); }

static void gpio_init(void)
{
    const gpio_config_t o = {
        .pin_bit_mask = (1ULL << PWRKEY_GPIO) | (1ULL << RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&o);
    gpio_set_level(RESET_GPIO, 0);   /* RESET released */
    gpio_set_level(PWRKEY_GPIO, 0);  /* PWRKEY released */
}

/* PWRKEY is a TOGGLE: pulsing an already-on modem powers it OFF.  Only call
 * this after probing and finding the modem unresponsive. */
static void pwrkey_pulse(void)
{
    gpio_set_level(PWRKEY_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(750));
    gpio_set_level(PWRKEY_GPIO, 0);
}

/* ========================================================================= */
/* Core / AT                                                                 */
/* ========================================================================= */
static void test_core(void)
{
    banner("CORE / AT");
    char buf[64];

    ck_st("check_at", ec200_check_at(&m), EC200_OK);
    ck_st("get_imei", ec200_get_imei(&m, buf, sizeof(buf)), EC200_OK);
    printf("        IMEI=%s\n", buf);
    ck_st("get_fw_version", ec200_get_fw_version(&m, buf, sizeof(buf)),
          EC200_OK);
    ck_st("get_module_info", ec200_get_module_info(&m, buf, sizeof(buf)),
          EC200_OK);
    ck_st("set_cmee(2)", ec200_set_cmee(&m, 2), EC200_OK);

    /* bad path */
    ck_st("imei NULL buf", ec200_get_imei(&m, NULL, 0), EC200_ERR_PARAM);
    ck_st("cmee bad mode", ec200_set_cmee(&m, 9), EC200_ERR_PARAM);
    ck_st("imei tiny buf -> OVERFLOW",
          ec200_get_imei(&m, buf, 4), EC200_ERR_OVERFLOW);

    /* status_str never NULL */
    ck("status_str all codes", ec200_status_str(EC200_OK) &&
       ec200_status_str((ec200_status_t)-123));

    /* security: oversized raw command must not corrupt, returns OVERFLOW */
    static char big[700];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    ck_st("oversized cmd -> OVERFLOW",
          ec200_at_send(&m, big, NULL, 0, 1000), EC200_ERR_OVERFLOW);
    /* engine still usable afterwards */
    ck_st("engine alive after overflow", ec200_check_at(&m), EC200_OK);
}

/* ========================================================================= */
/* SIM                                                                       */
/* ========================================================================= */
static void test_sim(void)
{
    banner("SIM");
    char buf[32];
    ec200_sim_status_t st;

    ck_st("sim_get_status", ec200_sim_get_status(&m, &st), EC200_OK);
    printf("        SIM state=%d (0=READY)\n", (int)st);
    ck_st("sim_get_imsi", ec200_sim_get_imsi(&m, buf, sizeof(buf)), EC200_OK);
    printf("        IMSI=%s\n", buf);
    ck_st("sim_get_iccid", ec200_sim_get_iccid(&m, buf, sizeof(buf)),
          EC200_OK);
    printf("        ICCID=%s\n", buf);

    ck_st("status NULL", ec200_sim_get_status(&m, NULL), EC200_ERR_PARAM);
    ck_st("enter_pin NULL", ec200_sim_enter_pin(&m, NULL), EC200_ERR_PARAM);
    ck_st("enter_pin too long",
          ec200_sim_enter_pin(&m, "123456789"), EC200_ERR_PARAM);
    ck_st("imsi tiny buf -> OVERFLOW",
          ec200_sim_get_imsi(&m, buf, 3), EC200_ERR_OVERFLOW);
}

/* ========================================================================= */
/* Network                                                                   */
/* ========================================================================= */
static void test_network(void)
{
    banner("NETWORK");
    ec200_reg_status_t rs;
    ec200_signal_quality_t sq;
    ec200_operator_info_t op;

    ck_st("get_creg", ec200_net_get_creg(&m, &rs), EC200_OK);
    ck_st("get_cgreg", ec200_net_get_cgreg(&m, &rs), EC200_OK);
    ck_st("get_cereg", ec200_net_get_cereg(&m, &rs), EC200_OK);
    printf("        cereg=%d\n", (int)rs);
    ck_st("get_signal(CSQ)", ec200_net_get_signal(&m, &sq), EC200_OK);
    ck_st("get_signal_ext(QCSQ)", ec200_net_get_signal_ext(&m, &sq),
          EC200_OK);
    printf("        RSSI=%d RSRP=%d SINR=%d\n",
           (int)sq.rssi, (int)sq.rsrp, (int)sq.sinr);
    ck_st("get_operator", ec200_net_get_operator(&m, &op), EC200_OK);
    printf("        oper=\"%s\" act=%d\n", op.oper, (int)op.act);

    ck_st("creg NULL", ec200_net_get_creg(&m, NULL), EC200_ERR_PARAM);
    ck_st("signal NULL", ec200_net_get_signal(&m, NULL), EC200_ERR_PARAM);
    ck_st("wait_registered (already)",
          ec200_net_wait_registered(&m, 5000), EC200_OK);
}

/* ========================================================================= */
/* Data / PDP                                                                */
/* ========================================================================= */
static void test_data(void)
{
    banner("DATA / PDP");
    char ip[EC200_MAX_IP_ADDR_LEN];
    ec200_pdp_context_t pdp;
    memset(&pdp, 0, sizeof(pdp));
    pdp.cid = 1;
    pdp.type = EC200_PDP_TYPE_IP;
    snprintf(pdp.apn, sizeof(pdp.apn), "%s", APN);

    ck_st("data_connect", ec200_data_connect(&m, &pdp), EC200_OK);
    printf("        IP=%s\n", pdp.ip_addr);
    ck_st("data_get_ip", ec200_data_get_ip(&m, 1, ip, sizeof(ip)), EC200_OK);

    ck_st("set_pdp NULL", ec200_data_set_pdp(&m, NULL), EC200_ERR_PARAM);
    ck_st("bad cid 0", ec200_data_activate(&m, 0), EC200_ERR_PARAM);
    ck_st("bad cid 17", ec200_data_activate(&m, 17), EC200_ERR_PARAM);
    ck_st("get_ip bad cid", ec200_data_get_ip(&m, 99, ip, sizeof(ip)),
          EC200_ERR_PARAM);

    /* security: oversized APN must be handled safely (snprintf truncates
     * into the fixed field; call must not corrupt the engine). */
    ec200_pdp_context_t big;
    memset(&big, 0, sizeof(big));
    big.cid = 2;
    big.type = EC200_PDP_TYPE_IP;
    memset(big.apn, 'X', sizeof(big.apn) - 1);
    ec200_status_t s = ec200_data_set_pdp(&m, &big);
    ck("oversized APN handled (no crash)",
       s == EC200_OK || s == EC200_ERR_CME || s == EC200_ERR_MODULE);
    ck_st("engine alive after big APN", ec200_check_at(&m), EC200_OK);
}

/* ========================================================================= */
/* TCP/IP                                                                    */
/* ========================================================================= */
static void test_tcp(void)
{
    banner("TCP/IP");
    ec200_socket_t sk;
    uint8_t rbuf[128];
    uint16_t got = 0;
    uint32_t avail = 0;

    ck_st("tcp_open echo", ec200_tcp_open(&m, 1, 0, EC200_SOCK_TCP,
          TCP_HOST, TCP_PORT, EC200_ACCESS_BUFFER), EC200_OK);
    ck_st("tcp_get_state", ec200_tcp_get_state(&m, 0, &sk), EC200_OK);
    printf("        connected=%d host=%s\n", (int)sk.connected, sk.remote_host);

    /* happy: send a line, tcpbin echoes it back */
    ck_st("tcp_send", ec200_tcp_send(&m, 0, (const uint8_t *)"hello\n", 6),
          EC200_OK);
    vTaskDelay(pdMS_TO_TICKS(1500));
    ck_st("tcp_bytes_available", ec200_tcp_bytes_available(&m, 0, &avail),
          EC200_OK);
    printf("        bytes available=%u\n", (unsigned)avail);
    ec200_status_t rs = ec200_tcp_recv(&m, 0, rbuf, sizeof(rbuf), &got, 5000);
    ck("tcp_recv got echo", rs == EC200_OK && got >= 6);
    if (got) { rbuf[got < sizeof(rbuf) ? got : sizeof(rbuf) - 1] = 0;
               printf("        echo(%u)=%.20s\n", got, (char *)rbuf); }

    /* security: binary payload with CR/LF/0x1A must be TRANSMITTED intact
     * (transmit transparency).  tcpbin is a line-echo and mangles binary,
     * so we assert the send path only; recv is best-effort. */
    static const uint8_t bin[] = {0x00, '\r', '\n', 0x1A, 0xFF, 'Z', '\n'};
    ck_st("tcp_send binary+ctrl (transmit ok)",
          ec200_tcp_send(&m, 0, bin, sizeof(bin)), EC200_OK);
    vTaskDelay(pdMS_TO_TICKS(1000));
    (void)ec200_tcp_recv(&m, 0, rbuf, sizeof(rbuf), &got, 3000);
    printf("        binary echo returned %u bytes (best-effort)\n", got);

    ck_st("tcp_close", ec200_tcp_close(&m, 0), EC200_OK);

    /* bad path */
    ck_st("open bad conn_id", ec200_tcp_open(&m, 1, 99, EC200_SOCK_TCP,
          TCP_HOST, TCP_PORT, EC200_ACCESS_BUFFER), EC200_ERR_PARAM);
    ck_st("open NULL host", ec200_tcp_open(&m, 1, 0, EC200_SOCK_TCP,
          NULL, TCP_PORT, EC200_ACCESS_BUFFER), EC200_ERR_PARAM);
    ck_st("send NULL data", ec200_tcp_send(&m, 0, NULL, 4), EC200_ERR_PARAM);
    ck_st("recv NULL buf",
          ec200_tcp_recv(&m, 0, NULL, 10, &got, 100), EC200_ERR_PARAM);
}

/* ========================================================================= */
/* HTTP                                                                      */
/* ========================================================================= */
static void test_http(void)
{
    banner("HTTP");
    ec200_http_response_t r;
    static uint8_t body[1025];
    uint32_t got = 0;

    ck_st("set_context", ec200_http_set_context(&m, 1), EC200_OK);
    ck_st("set_url", ec200_http_set_url(&m, HTTP_URL), EC200_OK);
    ck_st("http_get", ec200_http_get(&m, 30000, &r), EC200_OK);
    printf("        status=%u len=%u\n", r.status_code,
           (unsigned)r.content_length);
    ec200_status_t rs = ec200_http_read(&m, body, sizeof(body) - 1, &got,
                                        15000);
    ck("http_read body", (rs == EC200_OK || rs == EC200_ERR_OVERFLOW) &&
       got > 0);
    /* Clear the HTTP context before switching to a POST (diagnostic for the
     * observed +QHTTPPOST module error after a GET). */
    ec200_http_stop(&m);

    /* POST */
    ck_st("set_url post",
          ec200_http_set_url(&m, "http://httpbin.org/post"), EC200_OK);
    ec200_status_t ps = ec200_http_post(&m,
          (const uint8_t *)"field=value", 11,
          EC200_HTTP_CT_URLENCODED, 30000, &r);
    ck_st("http_post", ps, EC200_OK);
    if (ps == EC200_OK) { printf("        post status=%u\n", r.status_code); }

    /* bad path */
    ck_st("set_url NULL", ec200_http_set_url(&m, NULL), EC200_ERR_PARAM);
    ck_st("set_context bad", ec200_http_set_context(&m, 0), EC200_ERR_PARAM);
    ck_st("get NULL resp", ec200_http_get(&m, 1000, NULL), EC200_ERR_PARAM);

    /* security: oversized URL rejected, not truncated into a live request */
    static char url[EC200_MAX_URL_LEN + 64];
    memcpy(url, "http://", 7);
    memset(url + 7, 'a', sizeof(url) - 8);
    url[sizeof(url) - 1] = '\0';
    ck_st("oversized URL -> PARAM",
          ec200_http_set_url(&m, url), EC200_ERR_PARAM);
    ck_st("http_stop", ec200_http_stop(&m), EC200_OK);
}

/* ========================================================================= */
/* MQTT                                                                      */
/* ========================================================================= */
static volatile bool s_got_msg;
static char s_msg_topic[EC200_MAX_TOPIC_LEN];
static void mqtt_cb(const ec200_mqtt_message_t *msg, void *ctx)
{
    (void)ctx;
    snprintf(s_msg_topic, sizeof(s_msg_topic), "%s", msg->topic);
    s_got_msg = true;
    printf("        [URC] MQTT msg topic=\"%s\" len=%u\n",
           msg->topic, (unsigned)msg->payload_len);
}

static void test_mqtt(void)
{
    banner("MQTT");
    ec200_mqtt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.host, sizeof(cfg.host), "%s", MQTT_HOST);
    cfg.port = MQTT_PORT;
    snprintf(cfg.client_id, sizeof(cfg.client_id), "ec200hw%06u",
             (unsigned)(esp_random() % 1000000U));

    ec200_mqtt_set_message_cb(&m, mqtt_cb);

    ck_st("mqtt_open", ec200_mqtt_open(&m, &cfg), EC200_OK);
    ck_st("mqtt_connect", ec200_mqtt_connect(&m, &cfg), EC200_OK);
    ck_st("mqtt_subscribe", ec200_mqtt_subscribe(&m, 0, 1,
          "ec200hw/test", EC200_MQTT_QOS1), EC200_OK);

    /* happy: publish to our own subscription, poll for the loopback */
    s_got_msg = false;
    ck_st("mqtt_publish", ec200_mqtt_publish(&m, 0, 2, EC200_MQTT_QOS1,
          false, "ec200hw/test", (const uint8_t *)"hello", 5), EC200_OK);
    for (int i = 0; i < 50 && !s_got_msg; i++) {
        ec200_at_poll_urc(&m, 100);
    }
    ck("mqtt receive loopback", s_got_msg);

    /* security: binary payload containing 0x1A (QMTPUBEX, not Ctrl-Z) */
    static const uint8_t bin[] = {'A', 0x1A, 'B', 0x00, 'C'};
    ck_st("mqtt_publish binary 0x1A", ec200_mqtt_publish(&m, 0, 3,
          EC200_MQTT_QOS1, false, "ec200hw/test", bin, sizeof(bin)),
          EC200_OK);

    /* security: topic with embedded CR — AT-command-injection probe.
     * If the library forwards the CR, the modem would see a second line.
     * A hardened library rejects it (EC200_ERR_PARAM). */
    ec200_status_t inj = ec200_mqtt_publish(&m, 0, 4, EC200_MQTT_QOS0,
          false, "ec200hw/t\r\nAT+CFUN=0", (const uint8_t *)"x", 1);
    printf("        injection publish -> %s\n", ec200_status_str(inj));
    /* Drain any stray lines the malformed command produced, then resync. */
    for (int i = 0; i < 5; i++) { ec200_at_poll_urc(&m, 100); }
    (void)ec200_check_at(&m);
    /* Verify RF was NOT turned off: CFUN must still read FULL, and the
     * modem must still be registered. */
    ec200_cfun_t lvl = EC200_CFUN_MIN;
    ec200_status_t cs = ec200_power_get_cfun(&m, &lvl);
    ec200_reg_status_t rr = EC200_REG_UNKNOWN;
    ec200_net_get_cereg(&m, &rr);
    printf("        after injection: cfun=%s/%d reg=%d\n",
           ec200_status_str(cs), (int)lvl, (int)rr);
    ck("no CFUN=0 injection (RF still full & registered)",
       cs == EC200_OK && lvl == EC200_CFUN_FULL &&
       (rr == EC200_REG_REGISTERED_HOME ||
        rr == EC200_REG_REGISTERED_ROAMING));

    /* bad path */
    ck_st("open NULL cfg", ec200_mqtt_open(&m, NULL), EC200_ERR_PARAM);
    ck_st("sub NULL topic",
          ec200_mqtt_subscribe(&m, 0, 1, NULL, EC200_MQTT_QOS0),
          EC200_ERR_PARAM);
    ck_st("pub msg_id 0 QoS1", ec200_mqtt_publish(&m, 0, 0,
          EC200_MQTT_QOS1, false, "t", (const uint8_t *)"x", 1),
          EC200_ERR_PARAM);

    ck_st("mqtt_disconnect", ec200_mqtt_disconnect(&m, 0), EC200_OK);
    /* NOTE (hardware finding): on EC200UCNAAR03A03M08 QMTDISC already tears
     * down the network connection, so a following QMTCLOSE has nothing to
     * close and never emits its result URC (times out).  Accept either. */
    {
        ec200_status_t cs = ec200_mqtt_close(&m, 0);
        ck("mqtt_close after disconnect (OK or TIMEOUT: already closed)",
           cs == EC200_OK || cs == EC200_ERR_TIMEOUT);
        printf("        close-after-disconnect -> %s\n",
               ec200_status_str(cs));
    }
    ec200_mqtt_set_message_cb(&m, NULL);

    /* Let the module's MQTT subsystem settle before the TLS session
     * reuses it later in the run. */
    vTaskDelay(pdMS_TO_TICKS(10000));
}

/* ========================================================================= */
/* SMS                                                                       */
/* ========================================================================= */
static void test_sms(void)
{
    banner("SMS");
    ck_st("set_format(text)",
          ec200_sms_set_format(&m, EC200_SMS_FORMAT_TEXT), EC200_OK);

    if (SMS_DEST[0] != '\0') {
        ck_st("sms_send", ec200_sms_send(&m, SMS_DEST, "EC200 harness test"),
              EC200_OK);
    } else {
        skip("sms_send", "no SMS_DEST set");
    }

    /* inbox list + read */
    ec200_sms_message_t msgs[4];
    uint8_t cnt = 0;
    ck_st("sms_list ALL",
          ec200_sms_list(&m, EC200_SMS_STAT_ALL, msgs, 4, &cnt), EC200_OK);
    printf("        inbox count=%u\n", cnt);
    if (cnt > 0) {
        ec200_sms_message_t one;
        ck_st("sms_read[0]", ec200_sms_read(&m, msgs[0].index, &one),
              EC200_OK);
    } else {
        skip("sms_read", "empty inbox");
    }

    /* bad path + security */
    ck_st("send NULL num", ec200_sms_send(&m, NULL, "x"), EC200_ERR_PARAM);
    ck_st("send empty num", ec200_sms_send(&m, "", "x"), EC200_ERR_PARAM);
    static char longtext[EC200_MAX_SMS_TEXT_LEN + 8];
    memset(longtext, 'T', sizeof(longtext) - 1);
    longtext[sizeof(longtext) - 1] = '\0';
    ck_st("send oversized text",
          ec200_sms_send(&m, "+10000000000", longtext), EC200_ERR_PARAM);
    ck_st("send Ctrl-Z injection",
          ec200_sms_send(&m, "+10000000000", "a\x1a" "b"), EC200_ERR_PARAM);
    ck_st("delete_all bad flag",
          ec200_sms_delete_all(&m, 9), EC200_ERR_PARAM);
}

/* ========================================================================= */
/* GNSS (no antenna: control-plane only)                                     */
/* ========================================================================= */
static void test_gnss(void)
{
    banner("GNSS (no antenna)");
    bool en = false;
    ck_st("gnss_start", ec200_gnss_start(&m), EC200_OK);
    ck_st("gnss_get_status", ec200_gnss_get_status(&m, &en), EC200_OK);
    printf("        gnss enabled=%d\n", (int)en);

    ec200_gnss_location_t loc;
    ec200_status_t s = ec200_gnss_get_location(&m, &loc);
    /* No antenna/fix: the module errors (this firmware reports GNSS
     * not-fixed as +CMS ERROR).  A fix (OK) is also acceptable. */
    ck("get_location no-fix (CME/CMS) or OK",
       s == EC200_ERR_CME || s == EC200_ERR_CMS || s == EC200_OK);
    printf("        get_location -> %s\n", ec200_status_str(s));

    ck_st("set_nmea_output", ec200_gnss_set_nmea_output(&m, 1), EC200_OK);
    ck_st("gnss_get_status NULL",
          ec200_gnss_get_status(&m, NULL), EC200_ERR_PARAM);
    ck_st("gnss_stop", ec200_gnss_stop(&m), EC200_OK);
}

/* ========================================================================= */
/* TLS: filesystem, SSL contexts, HTTPS, MQTTS, TLS sockets                  */
/* ========================================================================= */
static void test_file_and_ssl(void)
{
    banner("FILESYSTEM (UFS)");
    ec200_file_storage_t fs;
    ec200_status_t s = ec200_file_storage(&m, &fs);
    ck_st("file_storage", s, EC200_OK);
    if (s == EC200_OK) {
        printf("        UFS free=%u total=%u\n",
               (unsigned)fs.free_bytes, (unsigned)fs.total_bytes);
    }

    (void)ec200_file_delete(&m, "mosq_ca.pem");
    (void)ec200_file_delete(&m, "isrg_ca.pem");

    uint16_t crc = 0;
    ck_st("upload mosquitto CA",
          ec200_file_upload(&m, "mosq_ca.pem",
                            (const uint8_t *)MOSQUITTO_CA_PEM,
                            (uint32_t)strlen(MOSQUITTO_CA_PEM), &crc),
          EC200_OK);
    printf("        crc=0x%04X\n", crc);

    ck_st("upload ISRG root CA",
          ec200_file_upload(&m, "isrg_ca.pem",
                            (const uint8_t *)ISRG_ROOT_X1_PEM,
                            (uint32_t)strlen(ISRG_ROOT_X1_PEM), NULL),
          EC200_OK);

    /* httpbin.org chains to the Amazon root — used by the HTTPS and TLS
     * socket tests below. */
    (void)ec200_file_delete(&m, "amzn_ca.pem");
    ck_st("upload Amazon root CA",
          ec200_file_upload(&m, "amzn_ca.pem",
                            (const uint8_t *)AMAZON_ROOT_CA1_PEM,
                            (uint32_t)strlen(AMAZON_ROOT_CA1_PEM), NULL),
          EC200_OK);

    bool ex = false;
    ck_st("file_exists(uploaded)",
          ec200_file_exists(&m, "mosq_ca.pem", &ex), EC200_OK);
    ck("file exists == true", ex);

    uint32_t sz = 0;
    s = ec200_file_size(&m, "mosq_ca.pem", &sz);
    ck_st("file_size", s, EC200_OK);
    ck("size matches upload", sz == strlen(MOSQUITTO_CA_PEM));
    printf("        size=%u (expected %u)\n", (unsigned)sz,
           (unsigned)strlen(MOSQUITTO_CA_PEM));

    ck_st("file_exists(absent)",
          ec200_file_exists(&m, "no_such_file.bin", &ex), EC200_OK);
    ck("absent file exists == false", !ex);

    ck_st("upload NULL name",
          ec200_file_upload(&m, NULL, (const uint8_t *)"x", 1, NULL),
          EC200_ERR_PARAM);

    banner("SSL CONTEXTS");
    ec200_ssl_config_t sc;
    memset(&sc, 0, sizeof(sc));
    sc.ctx_id = 2;
    sc.version = EC200_SSL_VER_TLS1_2;
    sc.ciphersuite = EC200_SSL_CIPHER_ALL;
    sc.seclevel = EC200_SSL_SECLEVEL_SERVER;
    sc.ignore_localtime = true;
    sc.enable_sni = true;
    snprintf(sc.cacert, sizeof(sc.cacert), "%s", "amzn_ca.pem");
    ck_st("ssl_configure ctx2 (server auth, Amazon CA)",
          ec200_ssl_configure(&m, &sc), EC200_OK);

    ec200_ssl_config_t sc3 = sc;
    sc3.ctx_id = 3;
    snprintf(sc3.cacert, sizeof(sc3.cacert), "%s", "mosq_ca.pem");
    ck_st("ssl_configure ctx3 (mosquitto CA)",
          ec200_ssl_configure(&m, &sc3), EC200_OK);

    ec200_ssl_config_t sc4;
    memset(&sc4, 0, sizeof(sc4));
    sc4.ctx_id = 4;
    sc4.version = EC200_SSL_VER_ALL;
    sc4.ciphersuite = EC200_SSL_CIPHER_ALL;
    sc4.seclevel = EC200_SSL_SECLEVEL_NONE;
    sc4.ignore_localtime = true;
    ck_st("ssl_configure ctx4 (no auth)",
          ec200_ssl_configure(&m, &sc4), EC200_OK);
    ck_st("ssl bad ctx id",
          ec200_ssl_set_seclevel(&m, 9, EC200_SSL_SECLEVEL_NONE),
          EC200_ERR_PARAM);
}

static void test_https(void)
{
    banner("HTTPS");
    ec200_http_response_t r;
    static uint8_t body[1025];
    uint32_t got = 0;

    ec200_http_stop(&m);
    ck_st("http_set_context", ec200_http_set_context(&m, 1), EC200_OK);
    ck_st("http_set_ssl_context(2)",
          ec200_http_set_ssl_context(&m, 2), EC200_OK);
    ck_st("set https url",
          ec200_http_set_url(&m, "https://httpbin.org/get"), EC200_OK);

    ec200_status_t s = ec200_http_get(&m, 60000, &r);
    ck_st("https GET (server-auth TLS)", s, EC200_OK);
    if (s == EC200_OK) {
        printf("        HTTPS status=%u len=%u\n", r.status_code,
               (unsigned)r.content_length);
        s = ec200_http_read(&m, body, sizeof(body) - 1U, &got, 20000);
        ck("https body read", (s == EC200_OK || s == EC200_ERR_OVERFLOW) &&
           got > 0);
        body[(got < sizeof(body)) ? got : (sizeof(body) - 1U)] = '\0';
        printf("        body(%u): %.60s...\n", (unsigned)got,
               (const char *)body);
    }
    ec200_http_stop(&m);

    ck_st("http_set_ssl_context(2) again",
          ec200_http_set_ssl_context(&m, 2), EC200_OK);
    ck_st("set https post url",
          ec200_http_set_url(&m, "https://httpbin.org/post"), EC200_OK);
    s = ec200_http_post(&m, (const uint8_t *)"tls=yes", 7,
                        EC200_HTTP_CT_URLENCODED, 60000, &r);
    ck_st("https POST", s, EC200_OK);
    if (s == EC200_OK) {
        printf("        HTTPS post status=%u\n", r.status_code);
    }
    ec200_http_stop(&m);
}

static void test_mqtts(void)
{
    banner("MQTTS (test.mosquitto.org:8883)");
    ec200_mqtt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.host, sizeof(cfg.host), "%s", MQTT_HOST);
    cfg.port = 8883;
    cfg.use_tls = true;
    cfg.ssl_ctx_id = 3;
    /* Slot 0: this firmware only accepted MQTTS on client/connection 0. */
    cfg.tcp_connect_id = 0;
    cfg.client_idx = 0;
    snprintf(cfg.client_id, sizeof(cfg.client_id), "ec200tls%06u",
             (unsigned)(esp_random() % 1000000U));

    ec200_mqtt_set_message_cb(&m, mqtt_cb);

    ec200_status_t s = ec200_mqtt_open(&m, &cfg);
    ck_st("mqtts_open (TLS)", s, EC200_OK);
    if (s != EC200_OK) {
        skip("mqtts session", "open failed");
        ec200_mqtt_set_message_cb(&m, NULL);
        return;
    }
    s = ec200_mqtt_connect(&m, &cfg);
    ck_st("mqtts_connect", s, EC200_OK);
    if (s != EC200_OK) {
        /* Diagnostic: show the module's own view of the MQTT clients. */
        char q[128] = {0};
        if (ec200_at_send_wait(&m, "AT+QMTCONN?", "+QMTCONN:", q, sizeof(q),
                               3000) == EC200_OK) {
            printf("        QMTCONN? -> %s\n", q);
        }
    }
    ck_st("mqtts_subscribe",
          ec200_mqtt_subscribe(&m, 0, 1, "ec200tls/test", EC200_MQTT_QOS1),
          EC200_OK);

    s_got_msg = false;
    ck_st("mqtts_publish",
          ec200_mqtt_publish(&m, 0, 2, EC200_MQTT_QOS1, false,
                             "ec200tls/test",
                             (const uint8_t *)"secure", 6), EC200_OK);
    for (int i = 0; i < 60 && !s_got_msg; i++) {
        ec200_at_poll_urc(&m, 100);
    }
    ck("mqtts receive loopback over TLS", s_got_msg);

    ck_st("mqtts_disconnect", ec200_mqtt_disconnect(&m, 0), EC200_OK);
    {
        ec200_status_t cc = ec200_mqtt_close(&m, 0);
        ck("mqtts_close (OK or TIMEOUT: disconnect already closed)",
           cc == EC200_OK || cc == EC200_ERR_TIMEOUT);
    }
    ec200_mqtt_set_message_cb(&m, NULL);
}

static void test_tls_socket(void)
{
    banner("TLS SOCKET");
    ec200_status_t s = ec200_ssl_socket_open(&m, 1, 2, 1,
                                             "httpbin.org", 443);
    ck_st("ssl_socket_open httpbin.org:443", s, EC200_OK);
    if (s != EC200_OK) {
        skip("tls socket exchange", "open failed");
    } else {
        static const char req[] =
            "GET /get HTTP/1.1\r\nHost: httpbin.org\r\n"
            "Connection: close\r\n\r\n";
        ck_st("ssl_socket_send",
              ec200_ssl_socket_send(&m, 1, (const uint8_t *)req,
                                    (uint16_t)strlen(req)), EC200_OK);

        uint8_t buf[256];
        uint16_t got = 0;
        bool saw_http = false;
        for (int i = 0; i < 20 && !saw_http; i++) {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (ec200_ssl_socket_recv(&m, 1, buf, sizeof(buf) - 1U, &got,
                                      5000) == EC200_OK && got > 0) {
                buf[got] = '\0';
                if (strstr((char *)buf, "HTTP/1.") != NULL) {
                    saw_http = true;
                }
                printf("        recv %u bytes: %.40s\n", got, (char *)buf);
            }
        }
        ck("tls socket got HTTP response", saw_http);
        ck_st("ssl_socket_close", ec200_ssl_socket_close(&m, 1), EC200_OK);
    }

    /* Negative control: a CA that cannot verify httpbin.org must FAIL,
     * proving certificate verification is actually enforced. */
    s = ec200_ssl_socket_open(&m, 1, 3 /* mosquitto CA */, 2,
                              "httpbin.org", 443);
    ck("cert verification rejects wrong CA", s != EC200_OK);
    printf("        wrong-CA open -> %s\n", ec200_status_str(s));
    if (s == EC200_OK) { (void)ec200_ssl_socket_close(&m, 2); }
}

/* ========================================================================= */
/* PPP control plane                                                         */
/* ========================================================================= */
static void test_ppp(void)
{
    banner("PPP CONTROL PLANE");
    ck("not in data mode initially", !ec200_ppp_in_data_mode(&m));
    ck_st("escape in cmd mode refused",
          ec200_ppp_escape(&m), EC200_ERR_PARAM);
    ck_st("dial bad cid", ec200_ppp_dial(&m, 0), EC200_ERR_PARAM);

    ec200_status_t s = ec200_ppp_dial(&m, 1);
    printf("        dial -> %s\n", ec200_status_str(s));
    if (s == EC200_OK) {
        ck("in data mode after dial", ec200_ppp_in_data_mode(&m));
        ck_st("AT refused in data mode (BUSY)",
              ec200_check_at(&m), EC200_ERR_BUSY);
        ck_st("ppp_escape", ec200_ppp_escape(&m), EC200_OK);
        ck("back in cmd mode", !ec200_ppp_in_data_mode(&m));
        ck_st("ppp_hangup", ec200_ppp_hangup(&m), EC200_OK);
    } else {
        skip("ppp data-mode cycle", "dial did not CONNECT");
    }
}

/* ========================================================================= */
/* Power (destructive: airplane toggles RF, QPOWD powers off — run LAST)     */
/* ========================================================================= */
static void test_power(void)
{
    banner("POWER (destructive, last)");
    ec200_cfun_t lvl;
    ck_st("get_cfun", ec200_power_get_cfun(&m, &lvl), EC200_OK);
    printf("        cfun=%d\n", (int)lvl);
    ck_st("get_cfun NULL", ec200_power_get_cfun(&m, NULL), EC200_ERR_PARAM);

    ck_st("set RF off(4)",
          ec200_power_set_cfun(&m, EC200_CFUN_RF_OFF, false), EC200_OK);
    vTaskDelay(pdMS_TO_TICKS(500));
    ck_st("get_cfun after RF off",
          ec200_power_get_cfun(&m, &lvl), EC200_OK);
    printf("        cfun now=%d (expect 4)\n", (int)lvl);
    ck_st("restore full(1)",
          ec200_power_set_cfun(&m, EC200_CFUN_FULL, false), EC200_OK);

    ck_st("set_sleep on", ec200_power_set_sleep(&m, true), EC200_OK);
    ck_st("set_sleep off", ec200_power_set_sleep(&m, false), EC200_OK);

#if 0 /* QPOWD powers the modem off; kept off so the modem stays up for
       * repeated harness runs.  Enable to validate power_down. */
    printf("  -> powering modem OFF (QPOWD); power-cycle to recover\n");
    ck_st("power_down", ec200_power_down(&m, true), EC200_OK);
#else
    skip("power_down", "QPOWD disabled to keep modem up for iteration");
#endif
}

/* ========================================================================= */
static void run_all(void *arg)
{
    (void)arg;
    const uart_config_t c = {
        .baud_rate = UART_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, RX_BUF, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &c));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    printf("\n########## EC200 API TEST HARNESS (lib v%d.%d.%d) ##########\n",
           EC200_LIB_VERSION_MAJOR, EC200_LIB_VERSION_MINOR,
           EC200_LIB_VERSION_PATCH);

    gpio_init();
    /* Probe first — the modem is left powered between runs, so it is usually
     * already up.  Only pulse PWRKEY (a toggle!) if it does not answer. */
    ec200_status_t st = ec200_init(&m, tx, rx, dly, NULL);
    if (st != EC200_OK) {
        printf("no response; pulsing PWRKEY to power on...\n");
        pwrkey_pulse();
        for (int w = 0; w < 15000; w += 1000) {
            st = ec200_init(&m, tx, rx, dly, NULL);
            if (st == EC200_OK) { break; }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    if (st != EC200_OK) {
        printf("FATAL: modem not responding (%s). Halting.\n",
               ec200_status_str(st));
        vTaskDelete(NULL);
        return;
    }
    printf("modem up.\n");

    /* Wait for registration once, up front (many modules need data). */
    printf("waiting for registration...\n");
    ec200_net_wait_registered(&m, 60000);

    test_core();
    test_sim();
    test_network();
    test_data();
    test_tcp();
    /*
     * TLS first: on this firmware a completed plaintext MQTT session
     * prevents a later MQTTS connect within the same power cycle, so the
     * secure tests run before the plaintext HTTP/MQTT ones.
     */
    test_file_and_ssl();
    /* MQTTS before HTTPS: an HTTP(S) request left in flight keeps the
     * module's TLS subsystem busy and blocks a following MQTT connect. */
    test_mqtts();
    test_tls_socket();
    test_https();
    /* Plaintext protocol tests after the secure ones. */
    test_http();
    test_mqtt();
    test_sms();
    test_gnss();
    test_ppp();
    test_power();   /* powers the modem off at the end */

    printf("\n########## SUMMARY: %d passed, %d failed, %d skipped "
           "##########\n", g_pass, g_fail, g_skip);
    printf("########## %s ##########\n",
           g_fail == 0 ? "ALL PASS" : "HAS FAILURES");
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Run on a generous dedicated stack: the harness declares large local
     * test buffers that would overflow the default main-task stack. */
    xTaskCreate(run_all, "ec200_tests", 16384, NULL, 5, NULL);
}
