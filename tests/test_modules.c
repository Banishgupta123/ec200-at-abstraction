/**
 * @file test_modules.c
 * @brief Unit tests for the core / SIM / network / data / power / GNSS modules.
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
 * Core
 * ========================================================================= */

void test_status_str(void)
{
    TEST_ASSERT_EQUAL_STRING("OK",               ec200_status_str(EC200_OK));
    TEST_ASSERT_EQUAL_STRING("Timeout",          ec200_status_str(EC200_ERR_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("I/O error",        ec200_status_str(EC200_ERR_IO));
    TEST_ASSERT_EQUAL_STRING("Parse error",      ec200_status_str(EC200_ERR_PARSE));
    TEST_ASSERT_EQUAL_STRING("+CME ERROR",       ec200_status_str(EC200_ERR_CME));
    TEST_ASSERT_EQUAL_STRING("+CMS ERROR",       ec200_status_str(EC200_ERR_CMS));
    TEST_ASSERT_EQUAL_STRING("Invalid parameter",ec200_status_str(EC200_ERR_PARAM));
    TEST_ASSERT_EQUAL_STRING("Not ready",        ec200_status_str(EC200_ERR_NOT_READY));
    TEST_ASSERT_EQUAL_STRING("Module error",     ec200_status_str(EC200_ERR_MODULE));
    TEST_ASSERT_NOT_NULL(ec200_status_str((ec200_status_t)-50));
}

void test_init_null_params(void)
{
    ec200_handle_t x;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_init(&x, NULL, lb_read, lb_delay, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_init(&x, lb_write, NULL, lb_delay, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_init(&x, lb_write, lb_read, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_init(NULL, lb_write, lb_read, lb_delay, NULL));
}

void test_init_disables_echo(void)
{
    /* Regression: init must send ATE0 so echoed commands cannot poison
     * response parsing (the module's factory default is ATE1). */
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "ATE0\r"));
}

void test_init_timeout_leaves_handle_unusable(void)
{
    ec200_handle_t x;
    lb_reset(); /* no scripted responses -> probe times out */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_init(&x, lb_write, lb_read, lb_delay, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_check_at(&x));
}

void test_get_imei_ok(void)
{
    lb_on_write("AT+GSN", "\r\n867698040000001\r\nOK\r\n");
    char imei[EC200_MAX_IMEI_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_get_imei(&h, imei, sizeof(imei)));
    TEST_ASSERT_EQUAL_STRING("867698040000001", imei);
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "AT+GSN\r"));
}

void test_get_imei_rejects_truncation(void)
{
    lb_on_write("AT+GSN", "\r\n867698040000001\r\nOK\r\n");
    char tiny[4];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW,
        ec200_get_imei(&h, tiny, sizeof(tiny)));
}

void test_get_imei_null_buf(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_get_imei(&h, NULL, 0));
}

void test_get_fw_version_ok(void)
{
    lb_on_write("AT+GMR", "\r\nEC200UCNAAR01A04V04\r\nOK\r\n");
    char ver[EC200_MAX_FW_VER_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_get_fw_version(&h, ver, sizeof(ver)));
    TEST_ASSERT_EQUAL_STRING("EC200UCNAAR01A04V04", ver);
}

void test_set_cmee_validates_mode(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_set_cmee(&h, 3));
}

/* =========================================================================
 * SIM
 * ========================================================================= */

static void sim_status_case(const char *cpin, ec200_sim_status_t expect)
{
    SETUP_MODEM(&h);
    char resp[80];
    (void)snprintf(resp, sizeof(resp), "\r\n+CPIN: %s\r\n\r\nOK\r\n", cpin);
    lb_on_write("AT+CPIN?", resp);
    ec200_sim_status_t stat = EC200_SIM_UNKNOWN;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sim_get_status(&h, &stat));
    TEST_ASSERT_EQUAL_INT((int)expect, (int)stat);
}

void test_sim_status_ready(void)        { sim_status_case("READY",        EC200_SIM_READY); }
void test_sim_status_pin(void)          { sim_status_case("SIM PIN",      EC200_SIM_PIN_REQUIRED); }
void test_sim_status_puk(void)          { sim_status_case("SIM PUK",      EC200_SIM_PUK_REQUIRED); }
void test_sim_status_not_inserted(void) { sim_status_case("NOT INSERTED", EC200_SIM_NOT_INSERTED); }

void test_sim_status_pin2_puk2(void)
{
    /* Regression: PIN2/PUK2 were unreachable due to prefix-match order. */
    sim_status_case("SIM PIN2", EC200_SIM_PIN2_REQUIRED);
    sim_status_case("SIM PUK2", EC200_SIM_PUK2_REQUIRED);
}

void test_sim_enter_pin_validation(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_sim_enter_pin(&h, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_sim_enter_pin(&h, ""));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sim_enter_pin(&h, "123456789"));
}

void test_sim_get_iccid_prefixed(void)
{
    lb_on_write("AT+CCID", "\r\n+ICCID: 89860317482033551234\r\n\r\nOK\r\n");
    char iccid[EC200_MAX_ICCID_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sim_get_iccid(&h, iccid, sizeof(iccid)));
    TEST_ASSERT_EQUAL_STRING("89860317482033551234", iccid);
}

void test_sim_get_imsi_ok(void)
{
    lb_on_write("AT+CIMI", "\r\n404685505601234\r\nOK\r\n");
    char imsi[EC200_MAX_IMSI_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sim_get_imsi(&h, imsi, sizeof(imsi)));
    TEST_ASSERT_EQUAL_STRING("404685505601234", imsi);
}

/* =========================================================================
 * Network
 * ========================================================================= */

void test_net_creg_home(void)
{
    lb_on_write("AT+CREG?", "\r\n+CREG: 0,1\r\n\r\nOK\r\n");
    ec200_reg_status_t st = EC200_REG_UNKNOWN;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_creg(&h, &st));
    TEST_ASSERT_EQUAL_INT(EC200_REG_REGISTERED_HOME, st);
}

void test_net_creg_roaming(void)
{
    lb_on_write("AT+CREG?", "\r\n+CREG: 0,5\r\n\r\nOK\r\n");
    ec200_reg_status_t st = EC200_REG_UNKNOWN;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_creg(&h, &st));
    TEST_ASSERT_EQUAL_INT(EC200_REG_REGISTERED_ROAMING, st);
}

void test_net_cereg_urc_mode2_extended_fields(void)
{
    /* Regression: with URC mode 2 the AcT field used to be returned as the
     * registration status (strrchr picked the LAST comma). */
    lb_on_write("AT+CEREG?",
                "\r\n+CEREG: 2,1,\"D5F3\",\"01A2B303\",7\r\n\r\nOK\r\n");
    ec200_reg_status_t st = EC200_REG_UNKNOWN;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_cereg(&h, &st));
    TEST_ASSERT_EQUAL_INT(EC200_REG_REGISTERED_HOME, st);
}

void test_net_signal_ok(void)
{
    lb_on_write("AT+CSQ", "\r\n+CSQ: 20,0\r\n\r\nOK\r\n");
    ec200_signal_quality_t sq;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_signal(&h, &sq));
    TEST_ASSERT_EQUAL_INT(-73, sq.rssi); /* -113 + 20*2 */
    TEST_ASSERT_EQUAL_UINT8(0, sq.ber);
}

void test_net_signal_unknown(void)
{
    lb_on_write("AT+CSQ", "\r\n+CSQ: 99,99\r\n\r\nOK\r\n");
    ec200_signal_quality_t sq;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_signal(&h, &sq));
    TEST_ASSERT_EQUAL_INT(0, sq.rssi);
}

void test_net_signal_ext_negative_dbm(void)
{
    /* Regression: negative (real) RSRP values were rejected as invalid. */
    lb_on_write("AT+QCSQ",
                "\r\n+QCSQ: \"LTE\",-52,-81,195,-10\r\n\r\nOK\r\n");
    ec200_signal_quality_t sq;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_signal_ext(&h, &sq));
    TEST_ASSERT_EQUAL_INT(-52, sq.rssi);
    TEST_ASSERT_EQUAL_INT(-81, sq.rsrp);
    TEST_ASSERT_EQUAL_INT(195, sq.sinr);
}

void test_net_operator_ok(void)
{
    lb_on_write("AT+COPS?",
                "\r\n+COPS: 0,0,\"Telstra\",7\r\n\r\nOK\r\n");
    ec200_operator_info_t info;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_operator(&h, &info));
    TEST_ASSERT_EQUAL_INT(EC200_COPS_MODE_AUTOMATIC, info.mode);
    TEST_ASSERT_EQUAL_INT(EC200_COPS_FMT_LONG_NAME, info.format);
    TEST_ASSERT_EQUAL_STRING("Telstra", info.oper);
    TEST_ASSERT_EQUAL_INT(EC200_ACT_LTE, info.act);
}

/* =========================================================================
 * Data / PDP
 * ========================================================================= */

void test_data_set_pdp_invalid_cid(void)
{
    ec200_pdp_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cid = 0;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_data_set_pdp(&h, &ctx));
}

void test_data_set_pdp_sends_auth_when_credentials_set(void)
{
    /* Regression: username/password used to be silently ignored. */
    ec200_pdp_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cid  = 1;
    ctx.type = EC200_PDP_TYPE_IP;
    (void)snprintf(ctx.apn, sizeof(ctx.apn), "%s", "internet");
    (void)snprintf(ctx.username, sizeof(ctx.username), "%s", "user1");
    (void)snprintf(ctx.password, sizeof(ctx.password), "%s", "pass1");

    lb_on_write("AT+CGDCONT", "\r\nOK\r\n");
    lb_on_write("AT+QICSGP",  "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_data_set_pdp(&h, &ctx));
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(),
        "AT+QICSGP=1,1,\"internet\",\"user1\",\"pass1\",3"));
}

void test_data_get_ip_parse(void)
{
    lb_on_write("AT+CGPADDR=1",
                "\r\n+CGPADDR: 1,\"10.0.0.1\"\r\n\r\nOK\r\n");
    char ip[EC200_MAX_IP_ADDR_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_data_get_ip(&h, 1, ip, sizeof(ip)));
    TEST_ASSERT_EQUAL_STRING("10.0.0.1", ip);
}

/* =========================================================================
 * Power
 * ========================================================================= */

void test_power_get_cfun_ok(void)
{
    lb_on_write("AT+CFUN?", "\r\n+CFUN: 1\r\n\r\nOK\r\n");
    ec200_cfun_t level;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_power_get_cfun(&h, &level));
    TEST_ASSERT_EQUAL_INT(EC200_CFUN_FULL, level);
}

void test_power_get_cfun_null(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_power_get_cfun(&h, NULL));
}

/* =========================================================================
 * GNSS
 * ========================================================================= */

void test_gnss_status_on_off(void)
{
    lb_on_write("AT+QGPS?", "\r\n+QGPS: 1\r\n\r\nOK\r\n");
    bool enabled = false;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_gnss_get_status(&h, &enabled));
    TEST_ASSERT_TRUE(enabled);

    lb_on_write("AT+QGPS?", "\r\n+QGPS: 0\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_gnss_get_status(&h, &enabled));
    TEST_ASSERT_FALSE(enabled);
}

void test_gnss_location_parse(void)
{
    lb_on_write("AT+QGPSLOC=2",
        "\r\n+QGPSLOC: 061951.0,-27.469800,153.025100,1.2,45.5,3,"
        "120.5,20.0,10.8,150620,08\r\n\r\nOK\r\n");
    ec200_gnss_location_t loc;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_gnss_get_location(&h, &loc));
    TEST_ASSERT_TRUE(loc.fix_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -27.4698f, loc.latitude);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 153.0251f, loc.longitude);
    TEST_ASSERT_EQUAL_UINT8(8, loc.satellites_used);
}

/* =========================================================================
 * Coverage round: remaining happy paths and validation branches
 * ========================================================================= */

void test_status_str_remaining_codes(void)
{
    TEST_ASSERT_EQUAL_STRING("Busy",           ec200_status_str(EC200_ERR_BUSY));
    TEST_ASSERT_EQUAL_STRING("Buffer overflow",ec200_status_str(EC200_ERR_OVERFLOW));
    TEST_ASSERT_EQUAL_STRING("Unsupported",    ec200_status_str(EC200_ERR_UNSUPPORTED));
    TEST_ASSERT_EQUAL_STRING("Unknown error",  ec200_status_str(EC200_ERR_UNKNOWN));
}

void test_get_module_info(void)
{
    lb_on_write("ATI", "\r\nQuectel\r\nEC200U\r\nOK\r\n");
    char info[64];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_get_module_info(&h, info, sizeof(info)));
    TEST_ASSERT_EQUAL_STRING("Quectel\nEC200U", info);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_get_module_info(&h, NULL, 0));
}

void test_set_echo_on(void)
{
    lb_on_write("ATE1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_set_echo(&h, true));
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "ATE1\r"));
}

void test_sim_enter_pin_ok(void)
{
    lb_on_write("AT+CPIN=\"1234\"", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sim_enter_pin(&h, "1234"));
}

void test_sim_get_iccid_fallback_without_prefix(void)
{
    /* Firmware variant: no "+ICCID:" line, digits come as a bare body. */
    lb_on_write("AT+CCID", "\r\nOK\r\n");
    lb_on_write("AT+CCID", "\r\n89860317482033551234\r\nOK\r\n");
    char iccid[EC200_MAX_ICCID_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sim_get_iccid(&h, iccid, sizeof(iccid)));
    TEST_ASSERT_EQUAL_STRING("89860317482033551234", iccid);
}

void test_net_cgreg(void)
{
    lb_on_write("AT+CGREG?", "\r\n+CGREG: 0,1\r\n\r\nOK\r\n");
    ec200_reg_status_t st = EC200_REG_UNKNOWN;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_cgreg(&h, &st));
    TEST_ASSERT_EQUAL_INT(EC200_REG_REGISTERED_HOME, st);
}

void test_net_signal_ext_falls_back_to_csq(void)
{
    /* No AT+QCSQ support -> falls back to AT+CSQ. */
    lb_on_write("AT+CSQ", "\r\n+CSQ: 10,0\r\n\r\nOK\r\n");
    ec200_signal_quality_t sq;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_signal_ext(&h, &sq));
    TEST_ASSERT_EQUAL_INT(-93, sq.rssi);
    TEST_ASSERT_EQUAL_INT(EC200_SIGNAL_UNKNOWN, sq.rsrp);
}

void test_net_set_operator_variants(void)
{
    lb_on_write("AT+COPS=0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_net_set_operator(&h, EC200_COPS_MODE_AUTOMATIC,
                               EC200_COPS_FMT_LONG_NAME, NULL,
                               EC200_ACT_LTE));

    lb_on_write("AT+COPS=1,2,\"50501\",7", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_net_set_operator(&h, EC200_COPS_MODE_MANUAL,
                               EC200_COPS_FMT_NUMERIC, "50501",
                               EC200_ACT_LTE));

    lb_on_write("AT+COPS=2,0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_net_set_operator(&h, EC200_COPS_MODE_DEREGISTER,
                               EC200_COPS_FMT_LONG_NAME, NULL,
                               EC200_ACT_LTE));
}

void test_net_wait_registered_immediate(void)
{
    lb_on_write("AT+CEREG?", "\r\n+CEREG: 0,1\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_wait_registered(&h, 10000));
}

void test_net_wait_registered_timeout(void)
{
    lb_on_write("AT+CEREG?", "\r\n+CEREG: 0,2\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_net_wait_registered(&h, 0));
}

void test_data_activate_deactivate(void)
{
    lb_on_write("AT+CGACT=1,1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_data_activate(&h, 1));

    lb_on_write("AT+CGACT=0,1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_data_deactivate(&h, 1));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_data_activate(&h, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_data_activate(&h, 17));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_data_deactivate(&h, 0));
}

void test_data_connect_full_flow(void)
{
    ec200_pdp_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cid  = 1;
    ctx.type = EC200_PDP_TYPE_IP;
    (void)snprintf(ctx.apn, sizeof(ctx.apn), "%s", "internet");

    /* Initial probe finds no address yet, then the full flow runs. */
    lb_on_write("AT+CGPADDR=1", "\r\n+CGPADDR: 1\r\n\r\nOK\r\n");
    lb_on_write("AT+CGDCONT=1,\"IP\",\"internet\"", "\r\nOK\r\n");
    lb_on_write("AT+CGACT=1,1", "\r\nOK\r\n");
    lb_on_write("AT+CGPADDR=1",
                "\r\n+CGPADDR: 1,\"10.64.12.7\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_data_connect(&h, &ctx));
    TEST_ASSERT_EQUAL_STRING("10.64.12.7", ctx.ip_addr);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_data_connect(&h, NULL));
}

void test_data_connect_zero_ip_falls_through(void)
{
    /* Regression: CGPADDR reporting 0.0.0.0 means the context is defined
     * but not up - connect must run the full activation flow, not report
     * success with an unusable address. */
    ec200_pdp_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cid  = 1;
    ctx.type = EC200_PDP_TYPE_IP;

    lb_on_write("AT+CGPADDR=1",
                "\r\n+CGPADDR: 1,\"0.0.0.0\"\r\n\r\nOK\r\n");
    lb_on_write("AT+CGDCONT", "\r\nOK\r\n");
    lb_on_write("AT+CGACT=1,1", "\r\nOK\r\n");
    lb_on_write("AT+CGPADDR=1",
                "\r\n+CGPADDR: 1,\"10.1.2.3\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_data_connect(&h, &ctx));
    TEST_ASSERT_EQUAL_STRING("10.1.2.3", ctx.ip_addr);
}

void test_data_connect_activate_fails_but_address_present(void)
{
    /* The module rejects activating an already-active context; that is not
     * a failure when an address is assigned afterwards. */
    ec200_pdp_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cid  = 1;
    ctx.type = EC200_PDP_TYPE_IP;

    lb_on_write("AT+CGPADDR=1",
                "\r\n+CGPADDR: 1,\"0.0.0.0\"\r\n\r\nOK\r\n");
    lb_on_write("AT+CGDCONT", "\r\nOK\r\n");
    lb_on_write("AT+CGACT=1,1", "\r\nERROR\r\n");
    lb_on_write("AT+CGPADDR=1",
                "\r\n+CGPADDR: 1,\"10.9.9.9\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_data_connect(&h, &ctx));
    TEST_ASSERT_EQUAL_STRING("10.9.9.9", ctx.ip_addr);
}

void test_data_connect_never_gets_address(void)
{
    /* Activation fails AND no address is ever assigned: report the
     * activation error, not success. */
    ec200_pdp_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cid  = 1;
    ctx.type = EC200_PDP_TYPE_IP;

    lb_on_write("AT+CGPADDR=1",
                "\r\n+CGPADDR: 1,\"0.0.0.0\"\r\n\r\nOK\r\n");
    lb_on_write("AT+CGDCONT", "\r\nOK\r\n");
    lb_on_write("AT+CGACT=1,1", "\r\nERROR\r\n");
    lb_on_write("AT+CGPADDR=1",
                "\r\n+CGPADDR: 1,\"0.0.0.0\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_data_connect(&h, &ctx));

    /* Activation succeeds but the address query itself fails. */
    SETUP_MODEM(&h);
    lb_on_write("AT+CGPADDR=1",
                "\r\n+CGPADDR: 1,\"0.0.0.0\"\r\n\r\nOK\r\n");
    lb_on_write("AT+CGDCONT", "\r\nOK\r\n");
    lb_on_write("AT+CGACT=1,1", "\r\nOK\r\n");
    lb_on_write("AT+CGPADDR=1", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_data_connect(&h, &ctx));
}

void test_data_connect_already_active(void)
{
    /* Regression (real Airtel LTE): the attach bearer is already active —
     * connect must use the existing address instead of re-activating. */
    ec200_pdp_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cid  = 1;
    ctx.type = EC200_PDP_TYPE_IP;

    lb_on_write("AT+CGPADDR=1",
                "\r\n+CGPADDR: 1,\"100.64.21.9\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_data_connect(&h, &ctx));
    TEST_ASSERT_EQUAL_STRING("100.64.21.9", ctx.ip_addr);
    /* No CGDCONT/CGACT must have been sent. */
    TEST_ASSERT_NULL(strstr(lb_tx_data(), "AT+CGDCONT"));
    TEST_ASSERT_NULL(strstr(lb_tx_data(), "AT+CGACT"));
}

void test_power_set_cfun_variants(void)
{
    lb_on_write("AT+CFUN=1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_power_set_cfun(&h, EC200_CFUN_FULL, false));

    lb_on_write("AT+CFUN=0,1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_power_set_cfun(&h, EC200_CFUN_MIN, true));
}

void test_power_down_sleep_reset(void)
{
    lb_on_write("AT+QPOWD=1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_power_down(&h, true));

    lb_on_write("AT+QSCLK=1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_power_set_sleep(&h, true));

    lb_on_write("AT+QSCLK=0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_power_set_sleep(&h, false));

    lb_on_write("AT+CFUN=1,1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_power_reset(&h));
}

void test_gnss_start_stop(void)
{
    lb_on_write("AT+QGPS=1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_gnss_start(&h));

    lb_on_write("AT+QGPSEND", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_gnss_stop(&h));
}

void test_gnss_set_nmea_output(void)
{
    lb_on_write("AT+QGPSCFG=\"nmeasrc\",1", "\r\nOK\r\n");
    lb_on_write("AT+QGPSCFG=\"gpsnmeatype\",5", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_gnss_set_nmea_output(&h, 5));
}

/* =========================================================================
 * Coverage round 2: error propagation and malformed responses
 * ========================================================================= */

void test_get_imei_error_and_edge_cases(void)
{
    /* Module error propagates */
    lb_on_write("AT+GSN", "\r\n+CME ERROR: 10\r\n");
    char imei[EC200_MAX_IMEI_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_get_imei(&h, imei, sizeof(imei)));

    /* Multi-line body: only the first line is the IMEI */
    lb_on_write("AT+GSN", "\r\n867698040000001\r\nEXTRA\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_get_imei(&h, imei, sizeof(imei)));
    TEST_ASSERT_EQUAL_STRING("867698040000001", imei);

    /* Empty body is a parse error */
    lb_on_write("AT+GSN", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_get_imei(&h, imei, sizeof(imei)));
}

void test_init_fails_when_ate0_rejected(void)
{
    ec200_handle_t x;
    lb_reset();
    lb_on_write("AT\r",  "\r\nOK\r\n");
    lb_on_write("ATE0",  "\r\n+CME ERROR: 3\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_init(&x, lb_write, lb_read, lb_delay, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY, ec200_check_at(&x));
}

void test_get_fw_version_null(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_get_fw_version(&h, NULL, 0));
}

void test_set_cmee_ok(void)
{
    lb_on_write("AT+CMEE=2", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_set_cmee(&h, 2));
}

void test_sim_status_param_error_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sim_get_status(&h, NULL));

    lb_on_write("AT+CPIN?", "\r\n+CME ERROR: 13\r\n");
    ec200_sim_status_t stat;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_sim_get_status(&h, &stat));

    lb_on_write("AT+CPIN?", "\r\n+CPIN: WEIRD\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_sim_get_status(&h, &stat));
    TEST_ASSERT_EQUAL_INT(EC200_SIM_UNKNOWN, stat);
}

void test_sim_imsi_error_paths(void)
{
    char imsi[EC200_MAX_IMSI_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sim_get_imsi(&h, NULL, 0));

    lb_on_write("AT+CIMI", "\r\n+CME ERROR: 13\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_sim_get_imsi(&h, imsi, sizeof(imsi)));

    /* Multi-line: first line only */
    lb_on_write("AT+CIMI", "\r\n404685505601234\r\nEXTRA\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sim_get_imsi(&h, imsi, sizeof(imsi)));
    TEST_ASSERT_EQUAL_STRING("404685505601234", imsi);

    /* Empty body */
    lb_on_write("AT+CIMI", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_sim_get_imsi(&h, imsi, sizeof(imsi)));

    /* Truncation refused */
    lb_on_write("AT+CIMI", "\r\n404685505601234\r\nOK\r\n");
    char tiny[4];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW,
        ec200_sim_get_imsi(&h, tiny, sizeof(tiny)));
}

void test_sim_iccid_error_paths(void)
{
    char iccid[EC200_MAX_ICCID_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sim_get_iccid(&h, NULL, 0));

    /* Both the prefixed and plain forms fail */
    lb_on_write("AT+CCID", "\r\nERROR\r\n");
    lb_on_write("AT+CCID", "\r\n+CME ERROR: 10\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_sim_get_iccid(&h, iccid, sizeof(iccid)));

    /* Fallback multi-line body: first line only */
    lb_on_write("AT+CCID", "\r\nOK\r\n"); /* prefixed form: no +ICCID line */
    lb_on_write("AT+CCID", "\r\n89860317482033551234\r\nEXTRA\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_sim_get_iccid(&h, iccid, sizeof(iccid)));
    TEST_ASSERT_EQUAL_STRING("89860317482033551234", iccid);

    /* Prefixed but empty value */
    lb_on_write("AT+CCID", "\r\n+ICCID:\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_sim_get_iccid(&h, iccid, sizeof(iccid)));

    /* Truncation refused */
    lb_on_write("AT+CCID", "\r\n+ICCID: 89860317482033551234\r\n\r\nOK\r\n");
    char tiny[4];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW,
        ec200_sim_get_iccid(&h, tiny, sizeof(tiny)));
}

void test_net_reg_param_error_malformed(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_net_get_creg(&h, NULL));

    ec200_reg_status_t st;
    lb_on_write("AT+CREG?", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_net_get_creg(&h, &st));

    /* Non-digit status field */
    lb_on_write("AT+CREG?", "\r\n+CREG: 0,x\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_net_get_creg(&h, &st));

    /* Out-of-range status value */
    lb_on_write("AT+CREG?", "\r\n+CREG: 0,9\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_net_get_creg(&h, &st));
}

void test_net_signal_param_error_malformed(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_net_get_signal(&h, NULL));

    ec200_signal_quality_t sq;
    lb_on_write("AT+CSQ", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_net_get_signal(&h, &sq));

    /* Missing <ber> field */
    lb_on_write("AT+CSQ", "\r\n+CSQ: 20\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_net_get_signal(&h, &sq));
}

void test_net_signal_ext_param_and_malformed(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_net_get_signal_ext(&h, NULL));

    ec200_signal_quality_t sq;
    /* No comma at all (e.g. searching) */
    lb_on_write("AT+QCSQ", "\r\n+QCSQ: NOSERVICE\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_net_get_signal_ext(&h, &sq));

    /* Non-numeric first value */
    lb_on_write("AT+QCSQ", "\r\n+QCSQ: \"LTE\",abc\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_net_get_signal_ext(&h, &sq));
}

void test_net_operator_param_error_malformed(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_net_get_operator(&h, NULL));

    ec200_operator_info_t info;
    lb_on_write("AT+COPS?", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_net_get_operator(&h, &info));

    lb_on_write("AT+COPS?", "\r\n+COPS: abc\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_net_get_operator(&h, &info));
}

void test_net_wait_registered_null_handle(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_net_wait_registered(NULL, 1000));
}

void test_net_wait_registered_polls_until_registered(void)
{
    /* First poll: searching; second poll (after the delay): registered. */
    lb_on_write("AT+CEREG?", "\r\n+CEREG: 0,2\r\n\r\nOK\r\n");
    lb_on_write("AT+CEREG?", "\r\n+CEREG: 0,1\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_wait_registered(&h, 5000));
}

void test_data_set_pdp_bad_type_and_send_failure(void)
{
    ec200_pdp_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cid  = 1;
    ctx.type = (ec200_pdp_type_t)7;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_data_set_pdp(&h, &ctx));

    ctx.type = EC200_PDP_TYPE_IP;
    lb_on_write("AT+CGDCONT", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_data_set_pdp(&h, &ctx));
}

void test_data_get_ip_error_paths(void)
{
    char ip[EC200_MAX_IP_ADDR_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_data_get_ip(&h, 0, ip, sizeof(ip)));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_data_get_ip(&h, 1, NULL, 0));

    lb_on_write("AT+CGPADDR=1", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_data_get_ip(&h, 1, ip, sizeof(ip)));

    /* No comma */
    lb_on_write("AT+CGPADDR=1", "\r\n+CGPADDR: 1\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_data_get_ip(&h, 1, ip, sizeof(ip)));

    /* Empty address */
    lb_on_write("AT+CGPADDR=1", "\r\n+CGPADDR: 1,\"\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_data_get_ip(&h, 1, ip, sizeof(ip)));

    /* Buffer smaller than the address: truncating copy */
    lb_on_write("AT+CGPADDR=1", "\r\n+CGPADDR: 1,\"10.0.0.1\"\r\n\r\nOK\r\n");
    char tiny[4];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_data_get_ip(&h, 1, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("10.", tiny);
}

void test_data_connect_failure_paths(void)
{
    ec200_pdp_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cid  = 1;
    ctx.type = EC200_PDP_TYPE_IP;

    /* set_pdp fails */
    lb_on_write("AT+CGDCONT", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_data_connect(&h, &ctx));

    /* activate fails */
    lb_on_write("AT+CGDCONT", "\r\nOK\r\n");
    lb_on_write("AT+CGACT=1,1", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_data_connect(&h, &ctx));
}

void test_gnss_status_parse_error(void)
{
    lb_on_write("AT+QGPS?", "\r\n+QGPS: x\r\n\r\nOK\r\n");
    bool enabled;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_gnss_get_status(&h, &enabled));
}

void test_power_cfun_parse_error(void)
{
    lb_on_write("AT+CFUN?", "\r\n+CFUN: x\r\n\r\nOK\r\n");
    ec200_cfun_t level;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_power_get_cfun(&h, &level));
}

/* =========================================================================
 * Branch-permutation round: every arm of every guard
 * ========================================================================= */

void test_branch_zero_size_buffers(void)
{
    char buf[8];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_get_imei(&h, buf, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_get_fw_version(&h, buf, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_get_module_info(&h, buf, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sim_get_imsi(&h, buf, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_sim_get_iccid(&h, buf, 0));
    ec200_set_urc_handler(NULL, NULL); /* NULL handle: must not crash */
}

void test_branch_reg_status_arms(void)
{
    /* URC form without a comma: first field is the status */
    lb_on_write("AT+CREG?", "\r\n+CREG: 1\r\n\r\nOK\r\n");
    ec200_reg_status_t st = EC200_REG_UNKNOWN;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_creg(&h, &st));
    TEST_ASSERT_EQUAL_INT(EC200_REG_REGISTERED_HOME, st);

    /* Field starting below '0' in the ASCII table */
    lb_on_write("AT+CREG?", "\r\n+CREG: 0,!\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_net_get_creg(&h, &st));
}

void test_branch_csq_range_arms(void)
{
    ec200_signal_quality_t sq;
    lb_on_write("AT+CSQ", "\r\n+CSQ: -1,0\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_signal(&h, &sq));
    TEST_ASSERT_EQUAL_INT(0, sq.rssi); /* below range -> unknown */

    lb_on_write("AT+CSQ", "\r\n+CSQ: 45,0\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_signal(&h, &sq));
    TEST_ASSERT_EQUAL_INT(0, sq.rssi); /* above range -> unknown */
}

void test_branch_qcsq_optional_fields(void)
{
    ec200_signal_quality_t sq;
    /* Only RSSI present */
    lb_on_write("AT+QCSQ", "\r\n+QCSQ: \"LTE\",-52\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_signal_ext(&h, &sq));
    TEST_ASSERT_EQUAL_INT(-52, sq.rssi);
    TEST_ASSERT_EQUAL_INT(EC200_SIGNAL_UNKNOWN, sq.rsrp);
    TEST_ASSERT_EQUAL_INT(EC200_SIGNAL_UNKNOWN, sq.sinr);

    /* RSSI + RSRP, no SINR */
    lb_on_write("AT+QCSQ", "\r\n+QCSQ: \"LTE\",-52,-81\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_get_signal_ext(&h, &sq));
    TEST_ASSERT_EQUAL_INT(-81, sq.rsrp);
    TEST_ASSERT_EQUAL_INT(EC200_SIGNAL_UNKNOWN, sq.sinr);
}

void test_branch_set_operator_empty_oper(void)
{
    /* Manual mode with an empty operator string takes the mode+format form */
    lb_on_write("AT+COPS=1,2", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_net_set_operator(&h, EC200_COPS_MODE_MANUAL,
                               EC200_COPS_FMT_NUMERIC, "",
                               EC200_ACT_LTE));
}

void test_branch_wait_registered_arms(void)
{
    /* Poll command itself failing is tolerated until the deadline */
    lb_on_write("AT+CEREG?", "\r\n+CME ERROR: 30\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_net_wait_registered(&h, 0));

    /* Roaming counts as registered */
    lb_on_write("AT+CEREG?", "\r\n+CEREG: 0,5\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_net_wait_registered(&h, 5000));
}

void test_branch_data_param_arms(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_data_set_pdp(&h, NULL));

    ec200_pdp_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cid  = 17;
    ctx.type = EC200_PDP_TYPE_IP;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_data_set_pdp(&h, &ctx));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_data_deactivate(&h, 17));

    char ip[EC200_MAX_IP_ADDR_LEN];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_data_get_ip(&h, 1, ip, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_data_get_ip(&h, 17, ip, sizeof(ip)));
}

void test_branch_data_get_ip_quote_arms(void)
{
    /* Unquoted address (some firmware omits the quotes) */
    char ip[EC200_MAX_IP_ADDR_LEN];
    lb_on_write("AT+CGPADDR=1", "\r\n+CGPADDR: 1,10.0.0.1\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_data_get_ip(&h, 1, ip, sizeof(ip)));
    TEST_ASSERT_EQUAL_STRING("10.0.0.1", ip);

    /* Comma with nothing after it */
    lb_on_write("AT+CGPADDR=1", "\r\n+CGPADDR: 1,\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_data_get_ip(&h, 1, ip, sizeof(ip)));
}

void test_branch_gnss_arms(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_gnss_get_status(&h, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_gnss_get_location(&h, NULL));

    bool enabled;
    lb_on_write("AT+QGPS?", "\r\n+CME ERROR: 505\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_gnss_get_status(&h, &enabled));

    ec200_gnss_location_t loc;
    lb_on_write("AT+QGPSLOC=2", "\r\n+CME ERROR: 516\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_gnss_get_location(&h, &loc));

    /* Header with no comma-separated fields at all */
    lb_on_write("AT+QGPSLOC=2", "\r\n+QGPSLOC: nofix\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_gnss_get_location(&h, &loc));
}

void test_branch_gnss_nmea_arms(void)
{
    /* nmea_types == 0 disables the source */
    lb_on_write("AT+QGPSCFG=\"nmeasrc\",0", "\r\nOK\r\n");
    lb_on_write("AT+QGPSCFG=\"gpsnmeatype\",0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_gnss_set_nmea_output(&h, 0));

    /* First configuration command failing aborts the second */
    lb_on_write("AT+QGPSCFG=\"nmeasrc\",1", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_gnss_set_nmea_output(&h, 3));
}

/* =========================================================================
 * Low power: PSM timer encoding (pure functions, no module traffic)
 * ========================================================================= */

void test_psm_encode_tau_units(void)
{
    char s[EC200_PSM_TIMER_STR_LEN];

    /* One case per unit code in the T3412-extended table. */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_encode_tau(2, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("01100001", s);          /* 011 = 2 s      */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_encode_tau(30, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("10000001", s);          /* 100 = 30 s     */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_encode_tau(60, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("10100001", s);          /* 101 = 1 min    */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_encode_tau(600, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("00000001", s);          /* 000 = 10 min   */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_encode_tau(3600, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("00100001", s);          /* 001 = 1 hour   */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_encode_tau(36000, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("01000001", s);          /* 010 = 10 hours */
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_psm_encode_tau(2U * 320U * 3600U, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("11000010", s);          /* 110 = 320 h, x2 */

    /* Zero means "deactivated", which is unit code 111. */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_encode_tau(0, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("11100000", s);

    /* The coarsest exact unit wins: 2 hours is 2x1h, not 3600x2s. */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_encode_tau(7200, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("00100010", s);
}

void test_psm_encode_active_time_units(void)
{
    char s[EC200_PSM_TIMER_STR_LEN];

    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_psm_encode_active_time(2, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("00000001", s);          /* 000 = 2 s      */
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_psm_encode_active_time(60, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("00100001", s);          /* 001 = 1 min    */
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_psm_encode_active_time(360, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("01000001", s);          /* 010 = decihour */
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_psm_encode_active_time(0, s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("11100000", s);
}

void test_psm_encode_rejects_unrepresentable(void)
{
    char s[EC200_PSM_TIMER_STR_LEN];

    /* Not a whole number of any unit: refused, never rounded, because a
     * silently shortened wake interval is an invisible power bug. */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_encode_tau(3660, s, sizeof(s)));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_encode_active_time(45, s, sizeof(s)));

    /* Divides exactly but needs a multiplier above 31. */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_encode_tau(64, s, sizeof(s)));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_encode_active_time(64, s, sizeof(s)));

    /* Beyond the largest unit's range entirely. */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_encode_tau(33U * 320U * 3600U, s, sizeof(s)));
}

void test_psm_encode_buffer_validation(void)
{
    char s[EC200_PSM_TIMER_STR_LEN];
    char tiny[4];

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_encode_tau(60, NULL, sizeof(s)));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_encode_tau(60, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_encode_active_time(60, NULL, sizeof(s)));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_encode_active_time(60, tiny, sizeof(tiny)));
}

void test_psm_decode_round_trip(void)
{
    static const uint32_t tau_secs[] = {
        0, 2, 30, 60, 600, 3600, 36000, 2U * 320U * 3600U, 7200
    };
    for (size_t i = 0; i < sizeof(tau_secs) / sizeof(tau_secs[0]); i++) {
        char s[EC200_PSM_TIMER_STR_LEN];
        uint32_t back = 12345U;
        TEST_ASSERT_EQUAL_INT(EC200_OK,
            ec200_psm_encode_tau(tau_secs[i], s, sizeof(s)));
        TEST_ASSERT_EQUAL_INT(EC200_OK,
            ec200_psm_decode_timer(s, true, &back));
        TEST_ASSERT_EQUAL_UINT32(tau_secs[i], back);
    }

    static const uint32_t act_secs[] = { 0, 2, 60, 360 };
    for (size_t i = 0; i < sizeof(act_secs) / sizeof(act_secs[0]); i++) {
        char s[EC200_PSM_TIMER_STR_LEN];
        uint32_t back = 12345U;
        TEST_ASSERT_EQUAL_INT(EC200_OK,
            ec200_psm_encode_active_time(act_secs[i], s, sizeof(s)));
        TEST_ASSERT_EQUAL_INT(EC200_OK,
            ec200_psm_decode_timer(s, false, &back));
        TEST_ASSERT_EQUAL_UINT32(act_secs[i], back);
    }
}

void test_psm_decode_failures(void)
{
    uint32_t secs = 0;

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_decode_timer(NULL, true, &secs));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_psm_decode_timer("00100001", true, NULL));

    /* Not eight bits, or not bits at all. */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_psm_decode_timer("0010000", true, &secs));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_psm_decode_timer("0010000x", true, &secs));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_psm_decode_timer("001000010", true, &secs));

    /* Unit code 110 (320 hours) exists for TAU but is reserved for T3324. */
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_psm_decode_timer("11000001", true, &secs));
    TEST_ASSERT_EQUAL_UINT32(320U * 3600U, secs);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_psm_decode_timer("11000001", false, &secs));
}

/* =========================================================================
 * Low power: PSM and eDRX transactions
 * ========================================================================= */

void test_psm_set_get_disable(void)
{
    ec200_psm_config_t cfg = { true, "00100001", "00000001" };
    lb_on_write("AT+CPSMS=1,,,\"00100001\",\"00000001\"", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_set(&h, &cfg));

    lb_on_write("AT+CPSMS?",
                "\r\n+CPSMS: 1,,,\"00100001\",\"00000001\"\r\n\r\nOK\r\n");
    ec200_psm_config_t got;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_get(&h, &got));
    TEST_ASSERT_TRUE(got.enabled);
    TEST_ASSERT_EQUAL_STRING("00100001", got.periodic_tau);
    TEST_ASSERT_EQUAL_STRING("00000001", got.active_time);

    /* Disabled: the module reports mode 0 and leaves the timers empty. */
    lb_on_write("AT+CPSMS?", "\r\n+CPSMS: 0,,,,\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_get(&h, &got));
    TEST_ASSERT_FALSE(got.enabled);
    TEST_ASSERT_EQUAL_STRING("", got.periodic_tau);
    TEST_ASSERT_EQUAL_STRING("", got.active_time);

    lb_on_write("AT+CPSMS=0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_disable(&h));

    /* enabled=false routes through the disable path, timers ignored. */
    ec200_psm_config_t off = { false, "", "" };
    lb_on_write("AT+CPSMS=0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_set(&h, &off));
}

void test_psm_set_validation(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_psm_set(&h, NULL));

    /* A short timer string would go on the wire malformed. */
    ec200_psm_config_t short_tau = { true, "0010", "00000001" };
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_psm_set(&h, &short_tau));
    ec200_psm_config_t short_act = { true, "00100001", "000" };
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_psm_set(&h, &short_act));
}

void test_psm_get_failures(void)
{
    ec200_psm_config_t cfg;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_psm_get(&h, NULL));

    /* Nothing scripted -> timeout. */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT, ec200_psm_get(&h, &cfg));

    /* Mode field missing. */
    lb_on_write("AT+CPSMS?", "\r\n+CPSMS: \r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_psm_get(&h, &cfg));
}

void test_psm_get_reply_shapes(void)
{
    ec200_psm_config_t cfg;

    /* Quotes are optional in the reply; both forms must parse.  The first
     * timer here ends at a comma, the second at the end of the line. */
    lb_on_write("AT+CPSMS?",
                "\r\n+CPSMS: 1,,,00100001,00000001\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_get(&h, &cfg));
    TEST_ASSERT_TRUE(cfg.enabled);
    TEST_ASSERT_EQUAL_STRING("00100001", cfg.periodic_tau);
    TEST_ASSERT_EQUAL_STRING("00000001", cfg.active_time);

    /* A space after the separator is tolerated. */
    lb_on_write("AT+CPSMS?",
                "\r\n+CPSMS: 1,,, \"00100001\", \"00000001\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_get(&h, &cfg));
    TEST_ASSERT_EQUAL_STRING("00100001", cfg.periodic_tau);
    TEST_ASSERT_EQUAL_STRING("00000001", cfg.active_time);

    /* The line stops before the timers: both come back empty, not garbage. */
    lb_on_write("AT+CPSMS?", "\r\n+CPSMS: 1\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_psm_get(&h, &cfg));
    TEST_ASSERT_EQUAL_STRING("", cfg.periodic_tau);
    TEST_ASSERT_EQUAL_STRING("", cfg.active_time);
}

void test_edrx_value_longer_than_buffer_is_truncated(void)
{
    /* Defensive: the spec says four bits.  A firmware that sends more must
     * not be allowed to overrun the caller's buffer. */
    lb_on_write("AT+CEDRXRDP",
                "\r\n+CEDRXRDP: 4,\"010101010101\"\r\n\r\nOK\r\n");
    ec200_edrx_dynamic_t dyn;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_edrx_get_dynamic(&h, &dyn));
    TEST_ASSERT_EQUAL_STRING("0101", dyn.requested);
}

void test_edrx_set_get_disable(void)
{
    ec200_edrx_config_t cfg = { true, EC200_EDRX_ACT_LTE_CAT_M1, "0101" };
    lb_on_write("AT+CEDRXS=1,4,\"0101\"", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_edrx_set(&h, &cfg));

    /* Real reply shape, read off the module: the mode comes FIRST, ahead of
     * the access technology, which 3GPP 27.007's read form does not show. */
    lb_on_write("AT+CEDRXS?", "\r\n+CEDRXS: 1,4,\"0101\"\r\n\r\nOK\r\n");
    ec200_edrx_config_t got;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_edrx_get(&h, &got));
    TEST_ASSERT_TRUE(got.enabled);
    TEST_ASSERT_EQUAL_INT(EC200_EDRX_ACT_LTE_CAT_M1, got.act_type);
    TEST_ASSERT_EQUAL_STRING("0101", got.requested);

    /* Mode 0 with a value still remembered: disabled, but the technology
     * and value are reported.  This is what the module answers after a
     * disable, and reading the mode from field 0 is what gets it right. */
    lb_on_write("AT+CEDRXS?", "\r\n+CEDRXS: 0,4,\"0101\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_edrx_get(&h, &got));
    TEST_ASSERT_FALSE(got.enabled);
    TEST_ASSERT_EQUAL_INT(EC200_EDRX_ACT_LTE_CAT_M1, got.act_type);
    TEST_ASSERT_EQUAL_STRING("0101", got.requested);

    /* Never configured: mode alone, nothing after it. */
    lb_on_write("AT+CEDRXS?", "\r\n+CEDRXS: 0\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_edrx_get(&h, &got));
    TEST_ASSERT_FALSE(got.enabled);
    TEST_ASSERT_EQUAL_STRING("", got.requested);

    lb_on_write("AT+CEDRXS=0,4", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_edrx_disable(&h, EC200_EDRX_ACT_LTE_CAT_M1));

    ec200_edrx_config_t off = { false, EC200_EDRX_ACT_GSM, "" };
    lb_on_write("AT+CEDRXS=0,2", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_edrx_set(&h, &off));
}

void test_edrx_validation(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_edrx_set(&h, NULL));

    /* Every accepted access technology, and one that is not. */
    ec200_edrx_config_t bad_act = { true, (ec200_edrx_act_t)9, "0101" };
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_edrx_set(&h, &bad_act));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_edrx_disable(&h, (ec200_edrx_act_t)9));

    lb_on_write("AT+CEDRXS=0,3", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_edrx_disable(&h, EC200_EDRX_ACT_UTRAN));
    lb_on_write("AT+CEDRXS=0,5", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_edrx_disable(&h, EC200_EDRX_ACT_LTE_NB_S1));

    /* An eDRX value is four bits, not fewer. */
    ec200_edrx_config_t short_val = { true, EC200_EDRX_ACT_GSM, "01" };
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_edrx_set(&h, &short_val));
}

void test_edrx_get_failures(void)
{
    ec200_edrx_config_t cfg;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_edrx_get(&h, NULL));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT, ec200_edrx_get(&h, &cfg));

    lb_on_write("AT+CEDRXS?", "\r\n+CEDRXS: \r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_edrx_get(&h, &cfg));
}

void test_edrx_dynamic(void)
{
    lb_on_write("AT+CEDRXRDP",
                "\r\n+CEDRXRDP: 4,\"0101\",\"0011\",\"1001\"\r\n\r\nOK\r\n");
    ec200_edrx_dynamic_t dyn;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_edrx_get_dynamic(&h, &dyn));
    TEST_ASSERT_EQUAL_INT(EC200_EDRX_ACT_LTE_CAT_M1, dyn.act_type);
    TEST_ASSERT_EQUAL_STRING("0101", dyn.requested);
    TEST_ASSERT_EQUAL_STRING("0011", dyn.granted);
    TEST_ASSERT_EQUAL_STRING("1001", dyn.paging_time_window);

    /* eDRX not in use: the module reports the AcT alone. */
    lb_on_write("AT+CEDRXRDP", "\r\n+CEDRXRDP: 0\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_edrx_get_dynamic(&h, &dyn));
    TEST_ASSERT_EQUAL_STRING("", dyn.requested);
    TEST_ASSERT_EQUAL_STRING("", dyn.granted);
    TEST_ASSERT_EQUAL_STRING("", dyn.paging_time_window);
}

void test_edrx_dynamic_failures(void)
{
    ec200_edrx_dynamic_t dyn;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_edrx_get_dynamic(&h, NULL));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_edrx_get_dynamic(&h, &dyn));

    lb_on_write("AT+CEDRXRDP", "\r\n+CEDRXRDP: \r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_edrx_get_dynamic(&h, &dyn));
}

void test_branch_power_get_cfun_error(void)
{
    lb_on_write("AT+CFUN?", "\r\n+CME ERROR: 3\r\n");
    ec200_cfun_t level;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_power_get_cfun(&h, &level));
}

/* ========================================================================= */

void test_set_operator_rejects_overlong_name(void)
{
    /* An over-long name must be rejected, not silently truncated into a
     * malformed AT+COPS command. */
    static char toolong[EC200_MAX_OPERATOR_LEN + 8];
    memset(toolong, 'o', sizeof(toolong) - 1U);
    toolong[sizeof(toolong) - 1U] = 0;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_net_set_operator(&h, EC200_COPS_MODE_MANUAL,
                               EC200_COPS_FMT_NUMERIC, toolong,
                               EC200_ACT_LTE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_str);
    RUN_TEST(test_init_null_params);
    RUN_TEST(test_init_disables_echo);
    RUN_TEST(test_init_timeout_leaves_handle_unusable);
    RUN_TEST(test_get_imei_ok);
    RUN_TEST(test_get_imei_rejects_truncation);
    RUN_TEST(test_get_imei_null_buf);
    RUN_TEST(test_get_fw_version_ok);
    RUN_TEST(test_set_cmee_validates_mode);
    RUN_TEST(test_sim_status_ready);
    RUN_TEST(test_sim_status_pin);
    RUN_TEST(test_sim_status_puk);
    RUN_TEST(test_sim_status_not_inserted);
    RUN_TEST(test_sim_status_pin2_puk2);
    RUN_TEST(test_sim_enter_pin_validation);
    RUN_TEST(test_sim_get_iccid_prefixed);
    RUN_TEST(test_sim_get_imsi_ok);
    RUN_TEST(test_net_creg_home);
    RUN_TEST(test_net_creg_roaming);
    RUN_TEST(test_net_cereg_urc_mode2_extended_fields);
    RUN_TEST(test_net_signal_ok);
    RUN_TEST(test_net_signal_unknown);
    RUN_TEST(test_net_signal_ext_negative_dbm);
    RUN_TEST(test_net_operator_ok);
    RUN_TEST(test_data_set_pdp_invalid_cid);
    RUN_TEST(test_data_set_pdp_sends_auth_when_credentials_set);
    RUN_TEST(test_data_get_ip_parse);
    RUN_TEST(test_power_get_cfun_ok);
    RUN_TEST(test_power_get_cfun_null);
    RUN_TEST(test_gnss_status_on_off);
    RUN_TEST(test_gnss_location_parse);
    RUN_TEST(test_status_str_remaining_codes);
    RUN_TEST(test_get_module_info);
    RUN_TEST(test_set_echo_on);
    RUN_TEST(test_sim_enter_pin_ok);
    RUN_TEST(test_sim_get_iccid_fallback_without_prefix);
    RUN_TEST(test_net_cgreg);
    RUN_TEST(test_net_signal_ext_falls_back_to_csq);
    RUN_TEST(test_net_set_operator_variants);
    RUN_TEST(test_net_wait_registered_immediate);
    RUN_TEST(test_net_wait_registered_timeout);
    RUN_TEST(test_data_activate_deactivate);
    RUN_TEST(test_data_connect_full_flow);
    RUN_TEST(test_data_connect_already_active);
    RUN_TEST(test_data_connect_zero_ip_falls_through);
    RUN_TEST(test_data_connect_activate_fails_but_address_present);
    RUN_TEST(test_data_connect_never_gets_address);
    RUN_TEST(test_power_set_cfun_variants);
    RUN_TEST(test_power_down_sleep_reset);
    RUN_TEST(test_gnss_start_stop);
    RUN_TEST(test_gnss_set_nmea_output);
    RUN_TEST(test_get_imei_error_and_edge_cases);
    RUN_TEST(test_init_fails_when_ate0_rejected);
    RUN_TEST(test_get_fw_version_null);
    RUN_TEST(test_set_cmee_ok);
    RUN_TEST(test_sim_status_param_error_unknown);
    RUN_TEST(test_sim_imsi_error_paths);
    RUN_TEST(test_sim_iccid_error_paths);
    RUN_TEST(test_net_reg_param_error_malformed);
    RUN_TEST(test_net_signal_param_error_malformed);
    RUN_TEST(test_net_signal_ext_param_and_malformed);
    RUN_TEST(test_net_operator_param_error_malformed);
    RUN_TEST(test_net_wait_registered_null_handle);
    RUN_TEST(test_net_wait_registered_polls_until_registered);
    RUN_TEST(test_data_set_pdp_bad_type_and_send_failure);
    RUN_TEST(test_data_get_ip_error_paths);
    RUN_TEST(test_data_connect_failure_paths);
    RUN_TEST(test_gnss_status_parse_error);
    RUN_TEST(test_power_cfun_parse_error);
    RUN_TEST(test_branch_zero_size_buffers);
    RUN_TEST(test_branch_reg_status_arms);
    RUN_TEST(test_branch_csq_range_arms);
    RUN_TEST(test_branch_qcsq_optional_fields);
    RUN_TEST(test_branch_set_operator_empty_oper);
    RUN_TEST(test_branch_wait_registered_arms);
    RUN_TEST(test_branch_data_param_arms);
    RUN_TEST(test_branch_data_get_ip_quote_arms);
    RUN_TEST(test_branch_gnss_arms);
    RUN_TEST(test_branch_gnss_nmea_arms);
    RUN_TEST(test_branch_power_get_cfun_error);
    RUN_TEST(test_set_operator_rejects_overlong_name);
    RUN_TEST(test_psm_encode_tau_units);
    RUN_TEST(test_psm_encode_active_time_units);
    RUN_TEST(test_psm_encode_rejects_unrepresentable);
    RUN_TEST(test_psm_encode_buffer_validation);
    RUN_TEST(test_psm_decode_round_trip);
    RUN_TEST(test_psm_decode_failures);
    RUN_TEST(test_psm_set_get_disable);
    RUN_TEST(test_psm_set_validation);
    RUN_TEST(test_psm_get_failures);
    RUN_TEST(test_psm_get_reply_shapes);
    RUN_TEST(test_edrx_value_longer_than_buffer_is_truncated);
    RUN_TEST(test_edrx_set_get_disable);
    RUN_TEST(test_edrx_validation);
    RUN_TEST(test_edrx_get_failures);
    RUN_TEST(test_edrx_dynamic);
    RUN_TEST(test_edrx_dynamic_failures);
    return UNITY_END();
}
