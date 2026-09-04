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
/* SMS destination.  Left empty here on purpose: this file is public, so a
 * personal number must never be committed.  Put
 *   #define SMS_DEST "+1234567890"
 * in main/test_secrets.h (gitignored) to enable the live send test. */
#if defined(__has_include)
#  if __has_include("test_secrets.h")
#    include "test_secrets.h"
#  endif
#endif
#ifndef SMS_DEST
#define SMS_DEST        ""
#endif
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
    if (got == EC200_ERR_CME || got == EC200_ERR_CMS) {
        /* With AT+CMEE=2 the module sends text, so the numeric code is
         * -1 and this string carries the actual diagnostic. */
        printf(" \"%s\"", ec200_at_last_error_text(&m));
    }
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
/* Error reporting: numeric (CMEE=1) vs verbose (CMEE=2)                     */
/* ========================================================================= */
static void test_error_reporting(void)
{
    banner("ERROR REPORTING");
    uint32_t sz;

    /* Numeric mode: the code parses and the text holds the digits. */
    ck_st("set_cmee(1) numeric", ec200_set_cmee(&m, 1), EC200_OK);
    ec200_status_t s = ec200_file_size(&m, "no_such_file.pem", &sz);
    int code = ec200_at_last_cme_error(&m);
    const char *txt = ec200_at_last_error_text(&m);
    printf("        CMEE=1 -> %s code=%d text=\"%s\"\n",
           ec200_status_str(s), code, txt);
    ck("CMEE=1 gives a numeric code", s == EC200_ERR_CME && code >= 0);

    /* Verbose mode: the module sends text.  Regression - atoi() used to
     * turn that into the fake code 0 and the message was lost. */
    ck_st("set_cmee(2) verbose", ec200_set_cmee(&m, 2), EC200_OK);
    s = ec200_file_size(&m, "no_such_file.pem", &sz);
    code = ec200_at_last_cme_error(&m);
    txt = ec200_at_last_error_text(&m);
    printf("        CMEE=2 -> %s code=%d text=\"%s\"\n",
           ec200_status_str(s), code, txt);
    ck("CMEE=2 still classified as CME", s == EC200_ERR_CME);
    ck("CMEE=2 verbose text retained", txt[0] != 0);
    ck("CMEE=2 reports -1, not a fake code 0",
       (code == -1) || (txt[0] >= (char)0x30 && txt[0] <= (char)0x39));
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
/* Destination for the tests that really transmit, resolved once per run.
 *
 * SMS_DEST (from the gitignored test_secrets.h) wins when present.  Failing
 * that the SIM's own number is used — AT+CNUM, then the "own numbers"
 * phonebook — so the loopback messages nobody.  NULL when neither exists,
 * which is why the send tests skip rather than fail on a fresh clone. */
static char g_sms_dest[EC200_MAX_PHONE_NUM_LEN];
static bool g_sms_dest_is_self;
static bool g_sms_dest_resolved;

static const char *sms_dest(void)
{
    if (g_sms_dest_resolved) {
        return (g_sms_dest[0] != '\0') ? g_sms_dest : NULL;
    }
    g_sms_dest_resolved = true;

    char self_num[EC200_MAX_PHONE_NUM_LEN] = {0};
    char cnum[96];
    if (ec200_at_send_wait(&m, "AT+CNUM", "+CNUM:", cnum, sizeof(cnum),
                           3000) == EC200_OK) {
        /* +CNUM: "","<number>",<type> - take the second quoted field */
        const char *q1 = strchr(cnum, '\"');
        const char *q2 = q1 ? strchr(q1 + 1, '\"') : NULL;
        const char *q3 = q2 ? strchr(q2 + 1, '\"') : NULL;
        const char *q4 = q3 ? strchr(q3 + 1, '\"') : NULL;
        if (q3 && q4 && (size_t)(q4 - q3 - 1) < sizeof(self_num)) {
            memcpy(self_num, q3 + 1, (size_t)(q4 - q3 - 1));
        }
    }
    if (self_num[0] == '\0') {
        /* Some SIMs leave CNUM empty but keep the MSISDN in the "own
         * numbers" phonebook.  Free to read and avoids messaging anyone. */
        char pbr[96];
        if (ec200_at_send(&m, "AT+CPBS=\"ON\"", NULL, 0, 3000) == EC200_OK &&
            ec200_at_send_wait(&m, "AT+CPBR=1", "+CPBR:", pbr, sizeof(pbr),
                               3000) == EC200_OK) {
            const char *a1 = strchr(pbr, '\"');
            const char *a2 = a1 ? strchr(a1 + 1, '\"') : NULL;
            if (a1 && a2 && (size_t)(a2 - a1 - 1) < sizeof(self_num)) {
                memcpy(self_num, a1 + 1, (size_t)(a2 - a1 - 1));
            }
        }
    }
    printf("        own number = \"%s\" (empty = not provisioned)\n",
           self_num);

    if (SMS_DEST[0] != '\0') {
        (void)snprintf(g_sms_dest, sizeof(g_sms_dest), "%s", SMS_DEST);
        g_sms_dest_is_self = false;
    } else if (self_num[0] != '\0') {
        (void)snprintf(g_sms_dest, sizeof(g_sms_dest), "%s", self_num);
        g_sms_dest_is_self = true;
    }
    return (g_sms_dest[0] != '\0') ? g_sms_dest : NULL;
}

static void test_sms(void)
{
    banner("SMS");
    ck_st("set_format(text)",
          ec200_sms_set_format(&m, EC200_SMS_FORMAT_TEXT), EC200_OK);

    /* Exercises the full CMGS prompt/Ctrl-Z path against a real number. */
    const char *dest = sms_dest();
    if (dest != NULL) {
        uint8_t before = 0, after = 0;
        ec200_sms_message_t tmp[8];
        (void)ec200_sms_list(&m, EC200_SMS_STAT_ALL, tmp, 8, &before);
        ck_st("sms_send", ec200_sms_send(&m, dest, "EC200 harness test"),
              EC200_OK);
        if (g_sms_dest_is_self) {
            /* Loopback: wait for our own message to come back. */
            for (int i = 0; i < 20 && after <= before; i++) {
                vTaskDelay(pdMS_TO_TICKS(3000));
                (void)ec200_at_poll_urc(&m, 100);
                (void)ec200_sms_list(&m, EC200_SMS_STAT_ALL, tmp, 8,
                                     &after);
            }
            printf("        inbox %u -> %u\n", before, after);
            ck("sms loopback delivered to self", after > before);
        }
    } else {
        skip("sms_send", "SMS_DEST empty and CNUM gave no own number");
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
/* SMS storage, service centre, stored messages, new-message indication      */
/* ========================================================================= */
static void test_sms_extras(void)
{
    banner("SMS EXTRAS (CPMS / CSCA / CMGW / CMSS / CNMI)");

    /* --- storage selection (CPMS) ------------------------------------- */
    ec200_sms_storage_t usage;
    ck_st("sms_get_storage", ec200_sms_get_storage(&m, &usage), EC200_OK);
    printf("        read/del %u/%u  write/send %u/%u  recv %u/%u\n",
           usage.read_delete.used, usage.read_delete.total,
           usage.write_send.used,  usage.write_send.total,
           usage.receive.used,     usage.receive.total);
    ck("storage totals are non-zero", usage.read_delete.total > 0);

    ec200_sms_storage_t after_set;
    ck_st("sms_set_storage ME/ME/ME",
          ec200_sms_set_storage(&m, EC200_SMS_MEM_ME, EC200_SMS_MEM_ME,
                                EC200_SMS_MEM_ME, &after_set), EC200_OK);
    ck_st("sms_set_storage discard usage",
          ec200_sms_set_storage(&m, EC200_SMS_MEM_ME, EC200_SMS_MEM_ME,
                                EC200_SMS_MEM_ME, NULL), EC200_OK);
    ck_st("set_storage rejects MT as write store",
          ec200_sms_set_storage(&m, EC200_SMS_MEM_ME, EC200_SMS_MEM_MT,
                                EC200_SMS_MEM_ME, NULL), EC200_ERR_PARAM);
    ck_st("get_storage NULL", ec200_sms_get_storage(&m, NULL),
          EC200_ERR_PARAM);

    /* --- service centre (CSCA) ---------------------------------------- */
    char smsc[EC200_MAX_PHONE_NUM_LEN] = {0};
    ck_st("sms_get_smsc", ec200_sms_get_smsc(&m, smsc, sizeof(smsc)),
          EC200_OK);
    ck("smsc is non-empty", smsc[0] != '\0');
    /* Do not print the SMSC: it identifies the carrier account. */

    if (smsc[0] != '\0') {
        /* Writing back the value just read changes nothing observable. */
        ck_st("sms_set_smsc round-trip", ec200_sms_set_smsc(&m, smsc),
              EC200_OK);
    } else {
        skip("sms_set_smsc", "no SMSC provisioned to write back");
    }
    ck_st("set_smsc NULL", ec200_sms_set_smsc(&m, NULL), EC200_ERR_PARAM);
    ck_st("set_smsc empty", ec200_sms_set_smsc(&m, ""), EC200_ERR_PARAM);
    ck_st("get_smsc NULL", ec200_sms_get_smsc(&m, NULL, sizeof(smsc)),
          EC200_ERR_PARAM);

    /* --- store a draft, send it, then clean up (CMGW + CMSS) ----------- */
    /* This transmits: CMSS puts the stored draft on the air, so it needs a
     * real destination.  Same resolution as the CMGS test — the SIM's own
     * number when SMS_DEST is unset, so the message comes back to us. */
    const char *dest = sms_dest();
    if (dest != NULL) {
        int stored_index = -1;
        ec200_status_t wst = ec200_sms_write(&m, dest,
                                             "EC200 harness stored draft",
                                             &stored_index);
        ck_st("sms_write (stored, not yet sent)", wst, EC200_OK);
        if (wst == EC200_OK) {
            printf("        stored at index=%d\n", stored_index);
            ck("sms_write reported an index", stored_index >= 0);

            if (stored_index >= 0) {
                ec200_sms_message_t drafted;
                ck_st("read back the stored draft",
                      ec200_sms_read(&m, stored_index, &drafted), EC200_OK);
                ck("draft kept its body",
                   strstr(drafted.text, "stored draft") != NULL);

                int mr = -1;
                ck_st("sms_send_stored (CMSS transmits)",
                      ec200_sms_send_stored(&m, stored_index, &mr), EC200_OK);
                printf("        message reference=%d\n", mr);
                ck("CMSS reported a message reference", mr >= 0);

                /* CMSS leaves the stored copy behind (now STO SENT). */
                ck_st("delete the sent draft",
                      ec200_sms_delete(&m, stored_index), EC200_OK);
            }
        } else {
            skip("stored-draft read/send/delete", "sms_write failed");
        }
    } else {
        skip("sms_write / sms_send_stored",
             "SMS_DEST empty and CNUM gave no own number");
    }
    ck_st("write rejects Ctrl-Z",
          ec200_sms_write(&m, "+10000000000", "a\x1a" "b", NULL),
          EC200_ERR_PARAM);
    ck_st("send_stored bad index", ec200_sms_send_stored(&m, -1, NULL),
          EC200_ERR_PARAM);

    /* --- new-message indication (CNMI) --------------------------------- */
    const ec200_sms_cnmi_t want = { 2, 1, 0, 0, 0 };
    ck_st("sms_set_indication 2,1,0,0,0",
          ec200_sms_set_indication(&m, &want), EC200_OK);

    ec200_sms_cnmi_t got;
    ck_st("sms_get_indication", ec200_sms_get_indication(&m, &got), EC200_OK);
    printf("        cnmi=%u,%u,%u,%u,%u\n",
           got.mode, got.mt, got.bm, got.ds, got.bfr);
    ck("cnmi mode round-tripped", got.mode == want.mode);
    ck("cnmi mt round-tripped",   got.mt   == want.mt);

    ck_st("set_indication NULL", ec200_sms_set_indication(&m, NULL),
          EC200_ERR_PARAM);
    const ec200_sms_cnmi_t bad = { 9, 1, 0, 0, 0 };
    ck_st("set_indication out of range",
          ec200_sms_set_indication(&m, &bad), EC200_ERR_PARAM);
    ck_st("get_indication NULL", ec200_sms_get_indication(&m, NULL),
          EC200_ERR_PARAM);

    /* --- +CMTI parsing (pure, no module traffic) ----------------------- */
    ec200_sms_notification_t note;
    ck_st("parse +CMTI",
          ec200_sms_parse_notification("+CMTI: \"ME\",3", &note), EC200_OK);
    ck("parsed CMTI index", note.index == 3);
    ck("parsed CMTI storage", note.mem == EC200_SMS_MEM_ME);
    ck_st("parse non-CMTI line",
          ec200_sms_parse_notification("+CMGS: 3", &note), EC200_ERR_PARSE);
    ck_st("parse CMTI NULL",
          ec200_sms_parse_notification(NULL, &note), EC200_ERR_PARAM);
}

/* ========================================================================= */
/* Low power: PSM (CPSMS) and eDRX (CEDRXS / CEDRXRDP)                       */
/* ========================================================================= */
static void test_lowpower(void)
{
    banner("LOW POWER (CPSMS / CEDRXS)");

    /* --- timer encoding: pure, no module traffic ----------------------- */
    char tau[EC200_PSM_TIMER_STR_LEN];
    char act[EC200_PSM_TIMER_STR_LEN];
    ck_st("encode TAU 1 hour",
          ec200_psm_encode_tau(3600, tau, sizeof(tau)), EC200_OK);
    ck("TAU 1 hour encodes to 00100001", strcmp(tau, "00100001") == 0);
    ck_st("encode active time 2 s",
          ec200_psm_encode_active_time(2, act, sizeof(act)), EC200_OK);
    ck("active 2 s encodes to 00000001", strcmp(act, "00000001") == 0);
    ck_st("encode refuses 3660 s",
          ec200_psm_encode_tau(3660, tau, sizeof(tau)), EC200_ERR_PARAM);
    /* tau still holds the 1-hour value from above. */
    ck_st("encode TAU 1 hour (again)",
          ec200_psm_encode_tau(3600, tau, sizeof(tau)), EC200_OK);

    uint32_t secs = 0;
    ck_st("decode TAU string",
          ec200_psm_decode_timer(tau, true, &secs), EC200_OK);
    ck("TAU decodes back to 3600 s", secs == 3600U);
    ck_st("decode rejects short string",
          ec200_psm_decode_timer("0010", true, &secs), EC200_ERR_PARSE);

    /* --- PSM against the module ---------------------------------------- */
    ec200_psm_config_t psm_before;
    ck_st("psm_get (initial)", ec200_psm_get(&m, &psm_before), EC200_OK);
    printf("        psm enabled=%d tau=\"%s\" active=\"%s\"\n",
           (int)psm_before.enabled, psm_before.periodic_tau,
           psm_before.active_time);

    ec200_psm_config_t want = { true, "", "" };
    (void)snprintf(want.periodic_tau, sizeof(want.periodic_tau), "%s", tau);
    (void)snprintf(want.active_time, sizeof(want.active_time), "%s", act);
    ck_st("psm_set (1 h TAU, 2 s active)", ec200_psm_set(&m, &want),
          EC200_OK);

    ec200_psm_config_t psm_after;
    ck_st("psm_get after set", ec200_psm_get(&m, &psm_after), EC200_OK);
    printf("        psm now enabled=%d tau=\"%s\" active=\"%s\"\n",
           (int)psm_after.enabled, psm_after.periodic_tau,
           psm_after.active_time);
    ck("psm reports enabled after set", psm_after.enabled);
    ck("psm TAU round-tripped",
       strcmp(psm_after.periodic_tau, tau) == 0);
    ck("psm active time round-tripped",
       strcmp(psm_after.active_time, act) == 0);

    /* Decode what the module reports back into seconds, so the whole
     * encode -> module -> read -> decode loop is proven, not just the
     * string comparison. */
    uint32_t tau_secs = 0;
    uint32_t act_secs = 0;
    ck_st("decode reported TAU",
          ec200_psm_decode_timer(psm_after.periodic_tau, true, &tau_secs),
          EC200_OK);
    ck_st("decode reported active time",
          ec200_psm_decode_timer(psm_after.active_time, false, &act_secs),
          EC200_OK);
    printf("        module reports TAU=%us active=%us\n",
           (unsigned)tau_secs, (unsigned)act_secs);
    ck("reported TAU is 3600 s", tau_secs == 3600U);
    ck("reported active time is 2 s", act_secs == 2U);

    /* Leave PSM off: it would let the module stop answering the UART. */
    ck_st("psm_disable", ec200_psm_disable(&m), EC200_OK);
    ck_st("psm_get after disable", ec200_psm_get(&m, &psm_after), EC200_OK);
    ck("psm reports disabled", !psm_after.enabled);

    ck_st("psm_set NULL", ec200_psm_set(&m, NULL), EC200_ERR_PARAM);
    ck_st("psm_get NULL", ec200_psm_get(&m, NULL), EC200_ERR_PARAM);
    ec200_psm_config_t bad = { true, "0010", "00000001" };
    ck_st("psm_set short timer", ec200_psm_set(&m, &bad), EC200_ERR_PARAM);

    /* Raw replies, so the field layout is read off the module rather than
     * assumed from the spec. */
    char raw[128];
    if (ec200_at_send_wait(&m, "AT+CPSMS?", "+CPSMS:", raw, sizeof(raw),
                           3000) == EC200_OK) {
        printf("        RAW CPSMS?   [%s]\n", raw);
    }
    if (ec200_at_send_wait(&m, "AT+CEDRXS?", "+CEDRXS:", raw, sizeof(raw),
                           3000) == EC200_OK) {
        printf("        RAW CEDRXS?  [%s]\n", raw);
    }
    if (ec200_at_send_wait(&m, "AT+CEDRXRDP", "+CEDRXRDP:", raw, sizeof(raw),
                           3000) == EC200_OK) {
        printf("        RAW CEDRXRDP [%s]\n", raw);
    }

    /* --- eDRX against the module --------------------------------------- */
    ec200_edrx_config_t edrx_before;
    ck_st("edrx_get (initial)", ec200_edrx_get(&m, &edrx_before), EC200_OK);
    printf("        edrx enabled=%d act=%d requested=\"%s\"\n",
           (int)edrx_before.enabled, (int)edrx_before.act_type,
           edrx_before.requested);

    ec200_edrx_config_t edrx_want = {
        true, EC200_EDRX_ACT_LTE_CAT_M1, "0101"
    };
    ck_st("edrx_set (Cat-M1, 0101)", ec200_edrx_set(&m, &edrx_want),
          EC200_OK);

    /* Read the settings back and check the VALUES, not just the status.
     * An earlier version of this test only asserted EC200_OK and happily
     * passed while the parser was reading the mode as the access
     * technology — the numbers are all small integers, so a field-offset
     * bug looks like success unless the values themselves are checked. */
    ec200_edrx_config_t edrx_after;
    ck_st("edrx_get after set", ec200_edrx_get(&m, &edrx_after), EC200_OK);
    printf("        edrx now enabled=%d act=%d requested=\"%s\"\n",
           (int)edrx_after.enabled, (int)edrx_after.act_type,
           edrx_after.requested);
    ck("edrx reports enabled after set", edrx_after.enabled);
    ck("edrx act-type round-tripped",
       edrx_after.act_type == EC200_EDRX_ACT_LTE_CAT_M1);
    ck("edrx requested value round-tripped",
       strcmp(edrx_after.requested, "0101") == 0);

    ec200_edrx_dynamic_t dyn;
    ck_st("edrx_get_dynamic", ec200_edrx_get_dynamic(&m, &dyn), EC200_OK);
    printf("        edrx dynamic act=%d requested=\"%s\" granted=\"%s\" "
           "ptw=\"%s\"\n",
           (int)dyn.act_type, dyn.requested, dyn.granted,
           dyn.paging_time_window);

    ck_st("edrx_disable", ec200_edrx_disable(&m, EC200_EDRX_ACT_LTE_CAT_M1),
          EC200_OK);
    ck_st("edrx_get after disable", ec200_edrx_get(&m, &edrx_after),
          EC200_OK);
    ck("edrx reports disabled", !edrx_after.enabled);

    ck_st("edrx_set NULL", ec200_edrx_set(&m, NULL), EC200_ERR_PARAM);
    ck_st("edrx_get NULL", ec200_edrx_get(&m, NULL), EC200_ERR_PARAM);
    ck_st("edrx_get_dynamic NULL", ec200_edrx_get_dynamic(&m, NULL),
          EC200_ERR_PARAM);
    ck_st("edrx_disable bad act",
          ec200_edrx_disable(&m, (ec200_edrx_act_t)9), EC200_ERR_PARAM);
    ec200_edrx_config_t edrx_bad = { true, EC200_EDRX_ACT_GSM, "01" };
    ck_st("edrx_set short value", ec200_edrx_set(&m, &edrx_bad),
          EC200_ERR_PARAM);
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

/* Release any lingering HTTP/socket session and let the module's network
 * subsystems settle.  Heavy back-to-back TLS/HTTP/MQTT work otherwise
 * leaves the module busy and the next subsystem times out. */
static void settle(void)
{
    (void)ec200_http_stop(&m);
    for (uint8_t c = 0; c < 3U; c++) {
        (void)ec200_tcp_close(&m, c);
        (void)ec200_ssl_socket_close(&m, c);
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    (void)ec200_check_at(&m);
}

/* ========================================================================= */
/* Network diagnostics: QNWINFO / QSPN / CGATT, DNS, ping, clock, NTP        */
/* ========================================================================= */
static void test_netdiag(void)
{
    banner("NETWORK DIAGNOSTICS");

    ec200_network_info_t ni;
    ec200_status_t s = ec200_net_get_info(&m, &ni);
    ck_st("net_get_info (QNWINFO)", s, EC200_OK);
    if (s == EC200_OK) {
        printf("        act=\"%s\" oper=\"%s\" band=\"%s\" ch=%u\n",
               ni.act, ni.oper, ni.band, (unsigned)ni.channel);
    }

    char spn[40];
    s = ec200_net_get_spn(&m, spn, sizeof(spn));
    ck_st("net_get_spn (QSPN)", s, EC200_OK);
    if (s == EC200_OK) { printf("        SPN=\"%s\"\n", spn); }

    bool att = false;
    ck_st("net_is_attached", ec200_net_is_attached(&m, &att), EC200_OK);
    ck("packet domain attached", att);

    ck_st("get_info NULL", ec200_net_get_info(&m, NULL), EC200_ERR_PARAM);
    ck_st("get_spn NULL", ec200_net_get_spn(&m, NULL, 0), EC200_ERR_PARAM);

    banner("DNS / PING");
    char ip[48];
    s = ec200_tcp_dns_resolve(&m, 1, "example.com", ip, sizeof(ip), 30000);
    ck_st("dns_resolve example.com", s, EC200_OK);
    if (s == EC200_OK) { printf("        resolved -> %s\n", ip); }

    s = ec200_tcp_dns_resolve(&m, 1, "no-such-host.invalid", ip, sizeof(ip),
                              30000);
    ck("dns_resolve invalid host fails", s != EC200_OK);
    printf("        invalid host -> %s\n", ec200_status_str(s));

    ec200_ping_result_t pr;
    s = ec200_tcp_ping(&m, 1, "8.8.8.8", 3, &pr, 30000);
    ck_st("ping 8.8.8.8", s, EC200_OK);
    if (s == EC200_OK) {
        printf("        sent=%u recv=%u lost=%u rtt min/avg/max=%u/%u/%u ms\n",
               pr.sent, pr.received, pr.lost,
               (unsigned)pr.min_rtt_ms, (unsigned)pr.avg_rtt_ms,
               (unsigned)pr.max_rtt_ms);
    }
    ck_st("ping bad count", ec200_tcp_ping(&m, 1, "8.8.8.8", 0, &pr, 1000),
          EC200_ERR_PARAM);

}

/* ========================================================================= */
/* Clock / NTP (runs last: a slow NTP attempt must not stall other tests)    */
/* ========================================================================= */
static void test_clock(void)
{
    banner("CLOCK / NTP");
    ec200_datetime_t dt;
    ec200_status_t s;
    ck_st("time_set_auto_update", ec200_time_set_auto_update(&m, true),
          EC200_OK);

    s = ec200_time_get(&m, &dt);
    ck_st("time_get (CCLK)", s, EC200_OK);
    if (s == EC200_OK) {
        printf("        module clock %04u-%02u-%02u %02u:%02u:%02u tz=%d\n",
               dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second,
               (int)dt.tz_quarters);
    }

    s = ec200_time_sync_ntp(&m, 1, "pool.ntp.org", 0, 60000);
    /* NTP reachability depends on the operator (many block/deprioritise
     * UDP 123), so a timeout here is an environment result, not a defect. */
    ck("time_sync_ntp (OK, module error, or unreachable)",
       s == EC200_OK || s == EC200_ERR_MODULE || s == EC200_ERR_TIMEOUT);
    printf("        NTP -> %s\n", ec200_status_str(s));
    if (s == EC200_OK && ec200_time_get(&m, &dt) == EC200_OK) {
        printf("        after NTP  %04u-%02u-%02u %02u:%02u:%02u\n",
               dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        ck("clock year is plausible after NTP", dt.year >= 2024U);
    }

    s = ec200_time_get_network(&m, &dt);
    ck("time_get_network (QLTS) OK or not-yet-available",
       s == EC200_OK || s == EC200_ERR_PARSE || s == EC200_ERR_CME);
    printf("        QLTS -> %s\n", ec200_status_str(s));

    ck_st("time_get NULL", ec200_time_get(&m, NULL), EC200_ERR_PARAM);
    ck_st("ntp bad ctx", ec200_time_sync_ntp(&m, 0, "x", 0, 1000),
          EC200_ERR_PARAM);
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
    printf("        dial -> %s (%s)\n", ec200_status_str(s),
           ec200_at_last_error_text(&m));

    /* ATD*99 conflicts with a PDP context that is already active in
     * AT mode (on LTE the attach bearer is up).  Drop it and retry:
     * the control-plane cycle needs no PPP stack, only CONNECT. */
    if (s != EC200_OK) {
        printf("        retrying with PDP context deactivated\n");
        (void)ec200_data_deactivate(&m, 1);
        vTaskDelay(pdMS_TO_TICKS(2000));
        s = ec200_ppp_dial(&m, 1);
        printf("        dial#2 -> %s (%s)\n",
               ec200_status_str(s), ec200_at_last_error_text(&m));
    }

    if (s == EC200_OK) {
        ck("in data mode after dial", ec200_ppp_in_data_mode(&m));
        ck_st("AT refused in data mode (BUSY)",
              ec200_check_at(&m), EC200_ERR_BUSY);
        /* Without a PPP stack driving LCP the module may drop the call,
         * answering "+++" with NO CARRIER.  Either outcome is fine; what
         * must NOT happen is the handle staying stuck in data mode. */
        ec200_status_t es = ec200_ppp_escape(&m);
        printf("        escape -> %s\n", ec200_status_str(es));
        ck("escape leaves data mode (no wedge)",
           !ec200_ppp_in_data_mode(&m));
        ck_st("AT works again after escape",
              ec200_check_at(&m), EC200_OK);

        if (es == EC200_OK) {
            /* Session survived: the full suspend/resume cycle. */
            ec200_status_t rs = ec200_ppp_resume(&m);
            printf("        resume -> %s\n", ec200_status_str(rs));
            if (rs == EC200_OK) {
                ck("in data mode after resume",
                   ec200_ppp_in_data_mode(&m));
                ck_st("ppp_disconnect (escape + ATH)",
                      ec200_ppp_disconnect(&m), EC200_OK);
            } else {
                skip("ppp resume cycle", "call ended before ATO");
            }
        } else {
            /* Carrier already gone - hang up to tidy the module up. */
            ck_st("ppp_hangup after carrier loss",
                  ec200_ppp_hangup(&m), EC200_OK);
        }
        ck("cmd mode at end of PPP", !ec200_ppp_in_data_mode(&m));
    } else {
        skip("ppp data-mode cycle", "dial did not CONNECT");
    }

    /* Leave data usable for anything that follows. */
    (void)ec200_data_activate(&m, 1);
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

    /* QPOWD really powers the module down.  Run it last, then bring it
     * back with a PWRKEY pulse so the rig is left usable. */
    printf("  -> powering modem OFF (QPOWD)\n");
    ck_st("power_down", ec200_power_down(&m, true), EC200_OK);
    vTaskDelay(pdMS_TO_TICKS(8000));
    ck("modem is really off (AT no longer answers)",
       ec200_check_at(&m) != EC200_OK);

    /* QPOWD shutdown is not instantaneous; a PWRKEY pulse that arrives
     * while the module is still powering down is ignored.  Let it settle
     * fully, then pulse - retrying once, since this must leave the rig
     * usable. */
    printf("  -> waiting for shutdown to complete\n");
    vTaskDelay(pdMS_TO_TICKS(15000));

    ec200_status_t back = EC200_ERR_TIMEOUT;
    for (int attempt = 1; attempt <= 3 && back != EC200_OK; attempt++) {
        printf("  -> PWRKEY pulse, attempt %d\n", attempt);
        pwrkey_pulse();
        for (int i = 0; i < 30 && back != EC200_OK; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            back = ec200_check_at(&m);
        }
    }
    ck_st("modem recovered after PWRKEY", back, EC200_OK);
    if (back == EC200_OK) {
        /* A fresh boot restores the factory ATE1, so re-init the handle. */
        (void)ec200_set_echo(&m, false);
        ck_st("usable after recovery", ec200_check_at(&m), EC200_OK);
    }
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
    test_error_reporting();
    test_sim();
    test_network();
    test_data();
    test_tcp();
    settle();
    test_netdiag();
    /*
     * TLS first: on this firmware a completed plaintext MQTT session
     * prevents a later MQTTS connect within the same power cycle, so the
     * secure tests run before the plaintext HTTP/MQTT ones.
     */
    settle();
    test_file_and_ssl();
    /* MQTTS before HTTPS: an HTTP(S) request left in flight keeps the
     * module's TLS subsystem busy and blocks a following MQTT connect. */
    settle();
    test_mqtts();
    settle();
    test_tls_socket();
    settle();
    test_https();
    /* Plaintext protocol tests after the secure ones. */
    settle();
    test_http();
    settle();
    test_mqtt();
    test_sms();
    test_sms_extras();
    test_lowpower();
    settle();
    test_clock();
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
