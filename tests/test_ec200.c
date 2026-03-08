/**
 * @file test_ec200.c
 * @brief Unit tests for the EC200 AT abstraction library.
 *
 * These tests run on the host (no hardware required) by providing a
 * "loopback" transport: write() pushes bytes into a ring-buffer,
 * read() pops bytes from the same ring-buffer.  Individual tests seed
 * the buffer with canned module responses to exercise parsing logic.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "ec200.h"

/* =========================================================================
 * Loopback transport
 * ========================================================================= */
#define LOOPBACK_BUF_SIZE 4096

static uint8_t  lb_buf[LOOPBACK_BUF_SIZE];
static uint16_t lb_head = 0;   /* write index */
static uint16_t lb_tail = 0;   /* read index  */

static void lb_reset(void)
{
    lb_head = lb_tail = 0;
    memset(lb_buf, 0, sizeof(lb_buf));
}

/** Feed a canned response string into the loopback buffer. */
static void lb_feed(const char *s)
{
    while (*s) {
        lb_buf[lb_head % LOOPBACK_BUF_SIZE] = (uint8_t)(*s++);
        lb_head++;
    }
}

static int lb_write(const uint8_t *data, uint16_t len, void *ctx)
{
    (void)data; (void)len; (void)ctx;
    /* Discard – tests pre-load responses */
    return (int)len;
}

static int lb_read(uint8_t *data, uint16_t len,
                   uint32_t timeout_ms, void *ctx)
{
    (void)timeout_ms; (void)ctx;
    uint16_t avail = lb_head - lb_tail;
    if (avail == 0) return -1; /* Simulate timeout */
    uint16_t n = (avail < len) ? avail : len;
    for (uint16_t i = 0; i < n; i++) {
        data[i] = lb_buf[lb_tail % LOOPBACK_BUF_SIZE];
        lb_tail++;
    }
    return (int)n;
}

static void lb_delay(uint32_t ms, void *ctx)
{
    (void)ms; (void)ctx;
}

/* =========================================================================
 * Helper: create a handle backed by the loopback transport
 * ========================================================================= */
static ec200_handle_t g_h;

static void setup_handle(void)
{
    lb_reset();
    /* Pre-load "AT\r\nOK\r\n" for the init probe */
    lb_feed("\r\nOK\r\n");
    ec200_status_t st = ec200_init(&g_h, lb_write, lb_read, lb_delay, NULL);
    assert(st == EC200_OK);
}

/* =========================================================================
 * Test helpers
 * ========================================================================= */
static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do { \
        tests_run++; \
        printf("  %-50s ", #name); \
        test_##name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } while (0)

/* =========================================================================
 * Test: ec200_status_str
 * ========================================================================= */
TEST(status_str)
{
    assert(strcmp(ec200_status_str(EC200_OK),          "OK")              == 0);
    assert(strcmp(ec200_status_str(EC200_ERR_TIMEOUT),  "Timeout")         == 0);
    assert(strcmp(ec200_status_str(EC200_ERR_PARSE),    "Parse error")     == 0);
    assert(strcmp(ec200_status_str(EC200_ERR_CME),      "+CME ERROR")      == 0);
    assert(strcmp(ec200_status_str(EC200_ERR_CMS),      "+CMS ERROR")      == 0);
    assert(strcmp(ec200_status_str(EC200_ERR_PARAM),    "Invalid parameter") == 0);
    assert(strcmp(ec200_status_str(EC200_ERR_NOT_READY),"Not ready")       == 0);
    /* Unknown code must not return NULL */
    assert(ec200_status_str((ec200_status_t)-50) != NULL);
}

/* =========================================================================
 * Test: ec200_init parameter validation
 * ========================================================================= */
TEST(init_null_params)
{
    ec200_handle_t h;
    /* NULL write callback */
    assert(ec200_init(&h, NULL, lb_read, lb_delay, NULL) == EC200_ERR_PARAM);
    /* NULL read callback */
    assert(ec200_init(&h, lb_write, NULL, lb_delay, NULL) == EC200_ERR_PARAM);
    /* NULL delay callback */
    assert(ec200_init(&h, lb_write, lb_read, NULL, NULL)  == EC200_ERR_PARAM);
    /* NULL handle */
    assert(ec200_init(NULL, lb_write, lb_read, lb_delay, NULL) == EC200_ERR_PARAM);
}

/* =========================================================================
 * Test: ec200_check_at
 * ========================================================================= */
TEST(check_at_ok)
{
    setup_handle();
    lb_reset();
    lb_feed("\r\nOK\r\n");
    assert(ec200_check_at(&g_h) == EC200_OK);
}

TEST(check_at_timeout)
{
    setup_handle();
    lb_reset();
    /* No response at all → timeout */
    assert(ec200_check_at(&g_h) == EC200_ERR_TIMEOUT);
}

/* =========================================================================
 * Test: ec200_get_imei
 * ========================================================================= */
TEST(get_imei_ok)
{
    setup_handle();
    lb_reset();
    lb_feed("867698040000001\r\nOK\r\n");

    char imei[EC200_MAX_IMEI_LEN];
    assert(ec200_get_imei(&g_h, imei, sizeof(imei)) == EC200_OK);
    assert(strcmp(imei, "867698040000001") == 0);
}

TEST(get_imei_null_buf)
{
    setup_handle();
    assert(ec200_get_imei(&g_h, NULL, 0) == EC200_ERR_PARAM);
}

/* =========================================================================
 * Test: ec200_get_fw_version
 * ========================================================================= */
TEST(get_fw_version_ok)
{
    setup_handle();
    lb_reset();
    lb_feed("EC200UCNAAR01A04V04\r\nOK\r\n");

    char ver[EC200_MAX_FW_VER_LEN];
    assert(ec200_get_fw_version(&g_h, ver, sizeof(ver)) == EC200_OK);
    assert(strcmp(ver, "EC200UCNAAR01A04V04") == 0);
}

/* =========================================================================
 * Test: SIM status parsing
 * ========================================================================= */
TEST(sim_get_status_ready)
{
    setup_handle();
    lb_reset();
    lb_feed("+CPIN: READY\r\nOK\r\n");

    ec200_sim_status_t stat;
    assert(ec200_sim_get_status(&g_h, &stat) == EC200_OK);
    assert(stat == EC200_SIM_READY);
}

TEST(sim_get_status_pin)
{
    setup_handle();
    lb_reset();
    lb_feed("+CPIN: SIM PIN\r\nOK\r\n");

    ec200_sim_status_t stat;
    assert(ec200_sim_get_status(&g_h, &stat) == EC200_OK);
    assert(stat == EC200_SIM_PIN_REQUIRED);
}

TEST(sim_get_status_not_inserted)
{
    setup_handle();
    lb_reset();
    lb_feed("+CPIN: NOT INSERTED\r\nOK\r\n");

    ec200_sim_status_t stat;
    assert(ec200_sim_get_status(&g_h, &stat) == EC200_OK);
    assert(stat == EC200_SIM_NOT_INSERTED);
}

/* =========================================================================
 * Test: Network registration parsing
 * ========================================================================= */
TEST(net_get_creg_home)
{
    setup_handle();
    lb_reset();
    lb_feed("+CREG: 0,1\r\nOK\r\n");

    ec200_reg_status_t st;
    assert(ec200_net_get_creg(&g_h, &st) == EC200_OK);
    assert(st == EC200_REG_REGISTERED_HOME);
}

TEST(net_get_creg_roaming)
{
    setup_handle();
    lb_reset();
    lb_feed("+CREG: 0,5\r\nOK\r\n");

    ec200_reg_status_t st;
    assert(ec200_net_get_creg(&g_h, &st) == EC200_OK);
    assert(st == EC200_REG_REGISTERED_ROAMING);
}

TEST(net_get_cereg_searching)
{
    setup_handle();
    lb_reset();
    lb_feed("+CEREG: 0,2\r\nOK\r\n");

    ec200_reg_status_t st;
    assert(ec200_net_get_cereg(&g_h, &st) == EC200_OK);
    assert(st == EC200_REG_SEARCHING);
}

/* =========================================================================
 * Test: Signal quality parsing
 * ========================================================================= */
TEST(net_get_signal_ok)
{
    setup_handle();
    lb_reset();
    lb_feed("+CSQ: 20,0\r\nOK\r\n");

    ec200_signal_quality_t sq;
    assert(ec200_net_get_signal(&g_h, &sq) == EC200_OK);
    /* rssi = -113 + 20*2 = -73 dBm */
    assert(sq.rssi == -73);
    assert(sq.ber  == 0);
}

TEST(net_get_signal_unknown)
{
    setup_handle();
    lb_reset();
    lb_feed("+CSQ: 99,99\r\nOK\r\n");

    ec200_signal_quality_t sq;
    assert(ec200_net_get_signal(&g_h, &sq) == EC200_OK);
    assert(sq.rssi == 0); /* 99 → unknown → 0 */
}

/* =========================================================================
 * Test: Operator parsing
 * ========================================================================= */
TEST(net_get_operator_ok)
{
    setup_handle();
    lb_reset();
    lb_feed("+COPS: 0,0,\"Telstra\",7\r\nOK\r\n");

    ec200_operator_info_t info;
    assert(ec200_net_get_operator(&g_h, &info) == EC200_OK);
    assert(info.mode   == EC200_COPS_MODE_AUTOMATIC);
    assert(info.format == EC200_COPS_FMT_LONG_NAME);
    assert(strcmp(info.oper, "Telstra") == 0);
    assert(info.act    == EC200_ACT_LTE);
}

/* =========================================================================
 * Test: CME / CMS error extraction
 * ========================================================================= */
TEST(cme_error_code)
{
    setup_handle();
    lb_reset();
    lb_feed("+CME ERROR: 10\r\n");

    ec200_status_t st = ec200_check_at(&g_h);
    assert(st == EC200_ERR_CME);
    assert(ec200_at_last_cme_error(&g_h) == 10);
}

TEST(cms_error_code)
{
    setup_handle();
    lb_reset();
    lb_feed("+CMS ERROR: 302\r\n");

    ec200_status_t st = ec200_check_at(&g_h);
    assert(st == EC200_ERR_CMS);
    assert(ec200_at_last_cms_error(&g_h) == 302);
}

/* =========================================================================
 * Test: PDP context helpers
 * ========================================================================= */
TEST(data_set_pdp_invalid_cid)
{
    setup_handle();
    ec200_pdp_context_t ctx = { .cid = 0 };
    assert(ec200_data_set_pdp(&g_h, &ctx) == EC200_ERR_PARAM);
}

TEST(data_get_ip_parse)
{
    setup_handle();
    lb_reset();
    lb_feed("+CGPADDR: 1,\"10.0.0.1\"\r\nOK\r\n");

    char ip[EC200_MAX_IP_ADDR_LEN];
    assert(ec200_data_get_ip(&g_h, 1, ip, sizeof(ip)) == EC200_OK);
    assert(strcmp(ip, "10.0.0.1") == 0);
}

/* =========================================================================
 * Test: SMS helpers
 * ========================================================================= */
TEST(sms_delete_invalid_flag)
{
    setup_handle();
    /* flag must be 1-4 */
    assert(ec200_sms_delete_all(&g_h, 0) == EC200_ERR_PARAM);
    assert(ec200_sms_delete_all(&g_h, 5) == EC200_ERR_PARAM);
}

/* =========================================================================
 * Test: Power management
 * ========================================================================= */
TEST(power_get_cfun_ok)
{
    setup_handle();
    lb_reset();
    lb_feed("+CFUN: 1\r\nOK\r\n");

    ec200_cfun_t level;
    assert(ec200_power_get_cfun(&g_h, &level) == EC200_OK);
    assert(level == EC200_CFUN_FULL);
}

TEST(power_get_cfun_null)
{
    setup_handle();
    assert(ec200_power_get_cfun(&g_h, NULL) == EC200_ERR_PARAM);
}

/* =========================================================================
 * Test: GNSS status parsing
 * ========================================================================= */
TEST(gnss_get_status_on)
{
    setup_handle();
    lb_reset();
    lb_feed("+QGPS: 1\r\nOK\r\n");

    bool enabled;
    assert(ec200_gnss_get_status(&g_h, &enabled) == EC200_OK);
    assert(enabled == true);
}

TEST(gnss_get_status_off)
{
    setup_handle();
    lb_reset();
    lb_feed("+QGPS: 0\r\nOK\r\n");

    bool enabled;
    assert(ec200_gnss_get_status(&g_h, &enabled) == EC200_OK);
    assert(enabled == false);
}

/* =========================================================================
 * Test: URC handler dispatch
 * ========================================================================= */
static const char *last_urc = NULL;
static void test_urc_cb(const char *urc, void *ctx)
{
    (void)ctx;
    last_urc = urc;
}

TEST(urc_dispatch)
{
    setup_handle();
    ec200_set_urc_handler(&g_h, test_urc_cb);

    lb_reset();
    lb_feed("+CREG: 1\r\n");

    last_urc = NULL;
    ec200_at_poll_urc(&g_h, 100);
    assert(last_urc != NULL);
    assert(strncmp(last_urc, "+CREG:", 6) == 0);
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    printf("EC200 AT Abstraction Library - Unit Tests\n");
    printf("==========================================\n\n");

    RUN(status_str);
    RUN(init_null_params);
    RUN(check_at_ok);
    RUN(check_at_timeout);
    RUN(get_imei_ok);
    RUN(get_imei_null_buf);
    RUN(get_fw_version_ok);
    RUN(sim_get_status_ready);
    RUN(sim_get_status_pin);
    RUN(sim_get_status_not_inserted);
    RUN(net_get_creg_home);
    RUN(net_get_creg_roaming);
    RUN(net_get_cereg_searching);
    RUN(net_get_signal_ok);
    RUN(net_get_signal_unknown);
    RUN(net_get_operator_ok);
    RUN(cme_error_code);
    RUN(cms_error_code);
    RUN(data_set_pdp_invalid_cid);
    RUN(data_get_ip_parse);
    RUN(sms_delete_invalid_flag);
    RUN(power_get_cfun_ok);
    RUN(power_get_cfun_null);
    RUN(gnss_get_status_on);
    RUN(gnss_get_status_off);
    RUN(urc_dispatch);

    printf("\n==========================================\n");
    printf("Results: %d / %d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
