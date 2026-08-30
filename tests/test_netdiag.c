/**
 * @file test_netdiag.c
 * @brief Unit tests for network diagnostics (QNWINFO/QSPN/CGATT), DNS,
 *        ping, and the clock / NTP module.
 */

#include <string.h>
#include <stdio.h>

#include "test_helpers.h"

static ec200_handle_t h;

void setUp(void)  { SETUP_MODEM(&h); }
void tearDown(void) { }

/* =========================================================================
 * Network info / SPN / attach
 * ========================================================================= */

void test_net_get_info_ok(void)
{
    lb_on_write("AT+QNWINFO",
                "\r\n+QNWINFO: \"FDD LTE\",\"40404\",\"LTE BAND 3\",1650"
                "\r\n\r\nOK\r\n");
    ec200_network_info_t info;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_info(&h, &info));
    TEST_ASSERT_EQUAL_STRING("FDD LTE", info.act);
    TEST_ASSERT_EQUAL_STRING("40404", info.oper);
    TEST_ASSERT_EQUAL_STRING("LTE BAND 3", info.band);
    TEST_ASSERT_EQUAL_UINT32(1650, info.channel);
}

void test_net_get_info_no_service(void)
{
    lb_on_write("AT+QNWINFO", "\r\n+QNWINFO: No Service\r\n\r\nOK\r\n");
    ec200_network_info_t info;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_net_get_info(&h, &info));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_net_get_info(&h, NULL));

    lb_on_write("AT+QNWINFO", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_net_get_info(&h, &info));
}

void test_net_get_spn(void)
{
    lb_on_write("AT+QSPN",
                "\r\n+QSPN: \"IND airtel\",\"airtel\",\"\",0,\"40410\""
                "\r\n\r\nOK\r\n");
    char name[32];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_net_get_spn(&h, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("IND airtel", name);

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_net_get_spn(&h, NULL, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_net_get_spn(&h, name, 0));

    /* truncation into a small buffer is safe */
    lb_on_write("AT+QSPN", "\r\n+QSPN: \"LongOperatorName\"\r\n\r\nOK\r\n");
    char tiny[6];
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_spn(&h, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("LongO", tiny);
}

void test_net_get_spn_malformed(void)
{
    lb_on_write("AT+QSPN", "\r\n+QSPN: noquotes\r\n\r\nOK\r\n");
    char name[32];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_net_get_spn(&h, name, sizeof(name)));

    lb_on_write("AT+QSPN", "\r\n+QSPN: \"unterminated\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_net_get_spn(&h, name, sizeof(name)));
}

void test_net_attach(void)
{
    lb_on_write("AT+CGATT=1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_set_attached(&h, true));
    lb_on_write("AT+CGATT=0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_set_attached(&h, false));

    lb_on_write("AT+CGATT?", "\r\n+CGATT: 1\r\n\r\nOK\r\n");
    bool att = false;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_is_attached(&h, &att));
    TEST_ASSERT_TRUE(att);

    lb_on_write("AT+CGATT?", "\r\n+CGATT: 0\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_is_attached(&h, &att));
    TEST_ASSERT_FALSE(att);

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_net_is_attached(&h, NULL));

    lb_on_write("AT+CGATT?", "\r\n+CGATT: x\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_net_is_attached(&h, &att));

    lb_on_write("AT+CGATT?", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_net_is_attached(&h, &att));
}

/* =========================================================================
 * DNS
 * ========================================================================= */

void test_dns_resolve_ok(void)
{
    lb_on_write("AT+QIDNSGIP=1,\"example.com\"",
                "\r\nOK\r\n"
                "+QIURC: \"dnsgip\",0,1,60\r\n"
                "+QIURC: \"dnsgip\",\"93.184.216.34\"\r\n");
    char ip[32];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_tcp_dns_resolve(&h, 1, "example.com", ip, sizeof(ip), 5000));
    TEST_ASSERT_EQUAL_STRING("93.184.216.34", ip);
}

void test_dns_resolve_failure_and_params(void)
{
    char ip[32];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_dns_resolve(&h, 1, NULL, ip, sizeof(ip), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_dns_resolve(&h, 1, "", ip, sizeof(ip), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_dns_resolve(&h, 0, "x", ip, sizeof(ip), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_dns_resolve(&h, 1, "x", NULL, 10, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_dns_resolve(&h, 1, "x", ip, 0, 100));

    /* module reports a lookup error */
    lb_on_write("AT+QIDNSGIP",
                "\r\nOK\r\n+QIURC: \"dnsgip\",565,0,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_tcp_dns_resolve(&h, 1, "bad.invalid", ip, sizeof(ip), 3000));

    /* header arrives but no address line follows */
    SETUP_MODEM(&h);
    lb_on_write("AT+QIDNSGIP", "\r\nOK\r\n+QIURC: \"dnsgip\",0,1,60\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_tcp_dns_resolve(&h, 1, "x", ip, sizeof(ip), 300));

    /* address line without quotes */
    SETUP_MODEM(&h);
    lb_on_write("AT+QIDNSGIP",
                "\r\nOK\r\n+QIURC: \"dnsgip\",0,1,60\r\n"
                "+QIURC: \"dnsgip\",noquote\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_tcp_dns_resolve(&h, 1, "x", ip, sizeof(ip), 3000));

    /* truncation into a small buffer */
    SETUP_MODEM(&h);
    lb_on_write("AT+QIDNSGIP",
                "\r\nOK\r\n+QIURC: \"dnsgip\",0,1,60\r\n"
                "+QIURC: \"dnsgip\",\"1.2.3.4\"\r\n");
    char tiny[4];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_tcp_dns_resolve(&h, 1, "x", tiny, sizeof(tiny), 3000));
    TEST_ASSERT_EQUAL_STRING("1.2", tiny);
}

/* =========================================================================
 * Ping
 * ========================================================================= */

void test_ping_ok(void)
{
    lb_on_write("AT+QPING=1,\"8.8.8.8\",4,4",
                "\r\nOK\r\n"
                "+QPING: 0,\"8.8.8.8\",32,52,255\r\n"
                "+QPING: 0,4,4,0,48,60,52\r\n");
    ec200_ping_result_t r;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_tcp_ping(&h, 1, "8.8.8.8", 4, &r, 10000));
    TEST_ASSERT_EQUAL_UINT16(4, r.sent);
    TEST_ASSERT_EQUAL_UINT16(4, r.received);
    TEST_ASSERT_EQUAL_UINT16(0, r.lost);
    TEST_ASSERT_EQUAL_UINT32(48, r.min_rtt_ms);
    TEST_ASSERT_EQUAL_UINT32(60, r.max_rtt_ms);
    TEST_ASSERT_EQUAL_UINT32(52, r.avg_rtt_ms);
}

void test_ping_failure_and_params(void)
{
    ec200_ping_result_t r;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_ping(&h, 1, NULL, 4, &r, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_ping(&h, 1, "", 4, &r, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_ping(&h, 0, "x", 4, &r, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_ping(&h, 1, "x", 0, &r, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_ping(&h, 1, "x", 11, &r, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_ping(&h, 1, "x", 4, NULL, 100));

    /* summary reports an error code */
    lb_on_write("AT+QPING",
                "\r\nOK\r\n+QPING: 569,0,0,0,0,0,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_tcp_ping(&h, 1, "bad", 1, &r, 3000));

    /* only per-reply lines, summary never arrives */
    SETUP_MODEM(&h);
    lb_on_write("AT+QPING",
                "\r\nOK\r\n+QPING: 0,\"1.1.1.1\",32,50,255\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_tcp_ping(&h, 1, "1.1.1.1", 1, &r, 300));
}

/* =========================================================================
 * Clock / NTP
 * ========================================================================= */

void test_time_get_ok(void)
{
    lb_on_write("AT+CCLK?",
                "\r\n+CCLK: \"26/08/30,04:15:20+22\"\r\n\r\nOK\r\n");
    ec200_datetime_t dt;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_time_get(&h, &dt));
    TEST_ASSERT_EQUAL_UINT16(2026, dt.year);
    TEST_ASSERT_EQUAL_UINT8(8, dt.month);
    TEST_ASSERT_EQUAL_UINT8(30, dt.day);
    TEST_ASSERT_EQUAL_UINT8(4, dt.hour);
    TEST_ASSERT_EQUAL_UINT8(15, dt.minute);
    TEST_ASSERT_EQUAL_UINT8(20, dt.second);
    TEST_ASSERT_EQUAL_INT8(22, dt.tz_quarters);
}

void test_time_get_errors(void)
{
    ec200_datetime_t dt;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_get(&h, NULL));

    lb_on_write("AT+CCLK?", "\r\n+CCLK: \"garbage\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_time_get(&h, &dt));

    /* out-of-range month */
    lb_on_write("AT+CCLK?",
                "\r\n+CCLK: \"26/13/30,04:15:20+22\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_time_get(&h, &dt));

    lb_on_write("AT+CCLK?", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_time_get(&h, &dt));
}

void test_time_set(void)
{
    ec200_datetime_t dt = { 2026, 8, 30, 4, 15, 20, 22 };
    lb_on_write("AT+CCLK=\"26/08/30,04:15:20+22\"", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_time_set(&h, &dt));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_set(&h, NULL));
    ec200_datetime_t bad = dt;
    bad.year = 1999;   TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_set(&h, &bad));
    bad = dt; bad.year = 2100;  TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_set(&h, &bad));
    bad = dt; bad.month = 0;    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_set(&h, &bad));
    bad = dt; bad.month = 13;   TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_set(&h, &bad));
    bad = dt; bad.day = 0;      TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_set(&h, &bad));
    bad = dt; bad.day = 32;     TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_set(&h, &bad));
    bad = dt; bad.hour = 24;    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_set(&h, &bad));
    bad = dt; bad.minute = 60;  TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_set(&h, &bad));
    bad = dt; bad.second = 60;  TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_set(&h, &bad));
}

void test_time_auto_update_and_network(void)
{
    lb_on_write("AT+CTZU=1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_time_set_auto_update(&h, true));
    lb_on_write("AT+CTZU=0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_time_set_auto_update(&h, false));

    lb_on_write("AT+QLTS=2",
                "\r\n+QLTS: \"26/08/30,09:45:00+22,0\"\r\n\r\nOK\r\n");
    ec200_datetime_t dt;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_time_get_network(&h, &dt));
    TEST_ASSERT_EQUAL_UINT16(2026, dt.year);
    TEST_ASSERT_EQUAL_UINT8(9, dt.hour);

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_time_get_network(&h, NULL));

    lb_on_write("AT+QLTS=2", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_time_get_network(&h, &dt));
}

void test_time_sync_ntp(void)
{
    lb_on_write("AT+QNTP=1,\"pool.ntp.org\",123",
                "\r\nOK\r\n+QNTP: 0,\"2026/08/30,09:45:00+22\"\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_time_sync_ntp(&h, 1, "pool.ntp.org", 0, 10000));

    /* explicit port */
    lb_on_write("AT+QNTP=1,\"ntp.example\",1123",
                "\r\nOK\r\n+QNTP: 0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_time_sync_ntp(&h, 1, "ntp.example", 1123, 10000));

    /* module error */
    lb_on_write("AT+QNTP", "\r\nOK\r\n+QNTP: 5\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_time_sync_ntp(&h, 1, "x", 0, 5000));

    /* unparseable result */
    SETUP_MODEM(&h);
    lb_on_write("AT+QNTP", "\r\nOK\r\n+QNTP: bad\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_time_sync_ntp(&h, 1, "x", 0, 5000));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_time_sync_ntp(&h, 1, NULL, 0, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_time_sync_ntp(&h, 1, "", 0, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_time_sync_ntp(&h, 0, "x", 0, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_time_sync_ntp(&h, 17, "x", 0, 100));
}

/* =========================================================================
 * Failure-propagation coverage
 * ========================================================================= */

void test_netdiag_command_failures(void)
{
    char name[32];
    char ip[32];
    ec200_ping_result_t r;

    /* SPN command itself fails */
    lb_on_write("AT+QSPN", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_net_get_spn(&h, name, sizeof(name)));

    /* DNS: the initial command errors before any URC */
    SETUP_MODEM(&h);
    lb_on_write("AT+QIDNSGIP", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_tcp_dns_resolve(&h, 1, "x", ip, sizeof(ip), 1000));

    /* DNS: address line present but has no comma at all */
    SETUP_MODEM(&h);
    lb_on_write("AT+QIDNSGIP",
                "\r\nOK\r\n+QIURC: \"dnsgip\",0,1,60\r\n"
                "+QIURC: \"dnsgip\"\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_tcp_dns_resolve(&h, 1, "x", ip, sizeof(ip), 1000));

    /* DNS: opening quote without a closing one */
    SETUP_MODEM(&h);
    lb_on_write("AT+QIDNSGIP",
                "\r\nOK\r\n+QIURC: \"dnsgip\",0,1,60\r\n"
                "+QIURC: \"dnsgip\",\"1.2.3.4\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_tcp_dns_resolve(&h, 1, "x", ip, sizeof(ip), 1000));

    /* Ping: command fails outright */
    SETUP_MODEM(&h);
    lb_on_write("AT+QPING", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_tcp_ping(&h, 1, "x", 1, &r, 1000));

    /* NTP: command fails outright */
    SETUP_MODEM(&h);
    lb_on_write("AT+QNTP", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_time_sync_ntp(&h, 1, "x", 0, 1000));
}

void test_branch_guard_arms(void)
{
    char ip[32];
    ec200_ping_result_t r;
    ec200_datetime_t dt;

    /* pdp_ctx upper bound arms */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_dns_resolve(&h, 17, "x", ip, sizeof(ip), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_tcp_ping(&h, 17, "x", 1, &r, 100));

    /* DNS header line whose error field is unparseable: treated as OK and
     * the address line is used. */
    lb_on_write("AT+QIDNSGIP",
                "\r\nOK\r\n+QIURC: \"dnsgip\",\"5.6.7.8\"\r\n"
                "+QIURC: \"dnsgip\",\"5.6.7.8\"\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_tcp_dns_resolve(&h, 1, "x", ip, sizeof(ip), 3000));
    TEST_ASSERT_EQUAL_STRING("5.6.7.8", ip);

    /* Timestamp without quotes (QLTS variants omit them). */
    lb_on_write("AT+CCLK?", "\r\n+CCLK: 26/08/30,04:15:20+22\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_time_get(&h, &dt));
    TEST_ASSERT_EQUAL_UINT16(2026, dt.year);

    /* Each out-of-range field arm of the timestamp validator. */
    static const char *bad[] = {
        "\r\n+CCLK: \"26/00/30,04:15:20+22\"\r\n\r\nOK\r\n", /* month low  */
        "\r\n+CCLK: \"26/08/00,04:15:20+22\"\r\n\r\nOK\r\n", /* day low    */
        "\r\n+CCLK: \"26/08/32,04:15:20+22\"\r\n\r\nOK\r\n", /* day high   */
        "\r\n+CCLK: \"26/08/30,24:15:20+22\"\r\n\r\nOK\r\n", /* hour high  */
        "\r\n+CCLK: \"26/08/30,04:60:20+22\"\r\n\r\nOK\r\n", /* min high   */
        "\r\n+CCLK: \"26/08/30,04:15:61+22\"\r\n\r\nOK\r\n", /* sec high   */
        "\r\n+CCLK: \"26/08/30,-1:15:20+22\"\r\n\r\nOK\r\n", /* hour neg   */
        "\r\n+CCLK: \"26/08/30,04:-1:20+22\"\r\n\r\nOK\r\n", /* min neg    */
        "\r\n+CCLK: \"26/08/30,04:15:-1+22\"\r\n\r\nOK\r\n", /* sec neg    */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        SETUP_MODEM(&h);
        lb_on_write("AT+CCLK?", bad[i]);
        TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_time_get(&h, &dt));
    }
}

void test_ping_many_reply_lines_then_timeout(void)
{
    /* A long run of per-reply lines with no summary must end in a timeout
     * rather than looping forever. */
    SETUP_MODEM(&h);
    static char resp[2048];
    int n = snprintf(resp, sizeof(resp), "\r\nOK\r\n");
    for (int i = 0; i < 70; i++) {
        n += snprintf(&resp[n], sizeof(resp) - (size_t)n,
                      "+QPING: 0,\"1.1.1.1\",32,50,255\r\n");
    }
    lb_on_write("AT+QPING", resp);
    ec200_ping_result_t r;
    ec200_status_t st = ec200_tcp_ping(&h, 1, "1.1.1.1", 1, &r, 500);
    TEST_ASSERT_TRUE(st == EC200_ERR_TIMEOUT || st == EC200_ERR_OVERFLOW);
}

/* ========================================================================= */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_net_get_info_ok);
    RUN_TEST(test_net_get_info_no_service);
    RUN_TEST(test_net_get_spn);
    RUN_TEST(test_net_get_spn_malformed);
    RUN_TEST(test_net_attach);
    RUN_TEST(test_dns_resolve_ok);
    RUN_TEST(test_dns_resolve_failure_and_params);
    RUN_TEST(test_ping_ok);
    RUN_TEST(test_ping_failure_and_params);
    RUN_TEST(test_time_get_ok);
    RUN_TEST(test_time_get_errors);
    RUN_TEST(test_time_set);
    RUN_TEST(test_time_auto_update_and_network);
    RUN_TEST(test_time_sync_ntp);
    RUN_TEST(test_netdiag_command_failures);
    RUN_TEST(test_branch_guard_arms);
    RUN_TEST(test_ping_many_reply_lines_then_timeout);
    return UNITY_END();
}
