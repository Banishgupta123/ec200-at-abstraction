/**
 * @file test_at_engine.c
 * @brief Unit tests for the AT transport engine (src/ec200_at.c).
 */

#include <string.h>
#include <stdio.h>

#include "test_helpers.h"
#include "ec200_at.h"

static ec200_handle_t h;

void setUp(void)
{
    SETUP_MODEM(&h);
}

void tearDown(void)
{
}

/* =========================================================================
 * Basic transactions
 * ========================================================================= */

void test_send_ok(void)
{
    lb_on_write("AT+TEST", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send(&h, "AT+TEST", NULL, 0, 1000));
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "AT+TEST\r"));
}

void test_send_collects_body_lines(void)
{
    lb_on_write("ATI", "\r\nQuectel\r\nEC200U\r\nRevision: 1\r\nOK\r\n");
    char resp[128];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send(&h, "ATI", resp, sizeof(resp), 1000));
    TEST_ASSERT_EQUAL_STRING("Quectel\nEC200U\nRevision: 1", resp);
}

void test_send_timeout_when_no_response(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_at_send(&h, "AT+NORESP", NULL, 0, 300));
}

void test_send_plain_error_maps_to_module_error(void)
{
    lb_on_write("AT+BAD", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_at_send(&h, "AT+BAD", NULL, 0, 1000));
    TEST_ASSERT_EQUAL_INT(-1, ec200_at_last_cme_error(&h));
    TEST_ASSERT_EQUAL_INT(-1, ec200_at_last_cms_error(&h));
}

void test_send_not_ready(void)
{
    ec200_handle_t raw;
    memset(&raw, 0, sizeof(raw));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send(&raw, "AT", NULL, 0, 100));
}

/* =========================================================================
 * CME / CMS error handling
 * ========================================================================= */

void test_cme_error_code(void)
{
    lb_on_write("AT+CMD1", "\r\n+CME ERROR: 10\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_at_send(&h, "AT+CMD1", NULL, 0, 1000));
    TEST_ASSERT_EQUAL_INT(10, ec200_at_last_cme_error(&h));
}

void test_cms_after_cme_not_misclassified(void)
{
    /* Regression: stale CME state must not shadow a later CMS error. */
    lb_on_write("AT+CMD1", "\r\n+CME ERROR: 10\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_at_send(&h, "AT+CMD1", NULL, 0, 1000));

    lb_on_write("AT+CMD2", "\r\n+CMS ERROR: 302\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CMS,
        ec200_at_send(&h, "AT+CMD2", NULL, 0, 1000));
    TEST_ASSERT_EQUAL_INT(302, ec200_at_last_cms_error(&h));
    TEST_ASSERT_EQUAL_INT(-1,  ec200_at_last_cme_error(&h));
}

/* =========================================================================
 * send_wait: prefix capture + stream stays synchronised
 * ========================================================================= */

void test_send_wait_reads_through_ok(void)
{
    lb_on_write("AT+CPIN?", "\r\n+CPIN: READY\r\n\r\nOK\r\n");
    char resp[64];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send_wait(&h, "AT+CPIN?", "+CPIN:",
                           resp, sizeof(resp), 1000));
    TEST_ASSERT_EQUAL_STRING("+CPIN: READY", resp);
    /* Regression: the trailing OK must be consumed — nothing pending. */
    TEST_ASSERT_EQUAL_size_t(0, lb_rx_pending());

    /* And the NEXT command still works (no off-by-one response). */
    lb_on_write("AT+NEXT", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send(&h, "AT+NEXT", NULL, 0, 1000));
}

void test_send_wait_ok_without_prefix_is_parse_error(void)
{
    lb_on_write("AT+X", "\r\nOK\r\n");
    char resp[64];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_at_send_wait(&h, "AT+X", "+X:", resp, sizeof(resp), 1000));
}

/* =========================================================================
 * send_expect: raw data after the header line is untouched
 * ========================================================================= */

void test_send_expect_leaves_raw_data(void)
{
    lb_on_write("AT+QIRD=0,16", "\r\n+QIRD: 5\r\nHELLO\r\nOK\r\n");
    char hdr[64];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send_expect(&h, "AT+QIRD=0,16", "+QIRD:",
                             hdr, sizeof(hdr), 1000));
    TEST_ASSERT_EQUAL_STRING("+QIRD: 5", hdr);

    /* Regression: the payload bytes must still be readable in full. */
    uint8_t buf[8];
    uint16_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_read_exact(&h, buf, 5, 1000, &got));
    TEST_ASSERT_EQUAL_UINT16(5, got);
    TEST_ASSERT_EQUAL_MEMORY("HELLO", buf, 5);

    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_wait_final(&h, 1000));
}

/* =========================================================================
 * send_await_urc: asynchronous OK-then-URC commands
 * ========================================================================= */

void test_await_urc_ok_then_urc(void)
{
    lb_on_write("AT+QIOPEN", "\r\nOK\r\n+QIOPEN: 0,0\r\n");
    char resp[64];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send_await_urc(&h, "AT+QIOPEN", "+QIOPEN:",
                                resp, sizeof(resp), 1000, 1000));
    TEST_ASSERT_EQUAL_STRING("+QIOPEN: 0,0", resp);
}

void test_await_urc_cme_error(void)
{
    lb_on_write("AT+QIOPEN", "\r\n+CME ERROR: 3\r\n");
    char resp[64];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_at_send_await_urc(&h, "AT+QIOPEN", "+QIOPEN:",
                                resp, sizeof(resp), 1000, 1000));
}

void test_await_urc_timeout_waiting_for_result(void)
{
    lb_on_write("AT+QIOPEN", "\r\nOK\r\n");
    char resp[64];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_at_send_await_urc(&h, "AT+QIOPEN", "+QIOPEN:",
                                resp, sizeof(resp), 1000, 300));
}

/* =========================================================================
 * send_prompt: ">" prompt with early-error detection
 * ========================================================================= */

void test_send_prompt_ok(void)
{
    lb_on_write("AT+QISEND", "\r\n> ");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send_prompt(&h, "AT+QISEND=0,5", 1000));
}

void test_send_prompt_error_aborts_immediately(void)
{
    /* Regression: an ERROR while waiting for ">" must abort, not spin. */
    lb_on_write("AT+QISEND", "\r\n+CME ERROR: 3\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_at_send_prompt(&h, "AT+QISEND=99,5", 1000));
}

/* =========================================================================
 * Receive-only waits
 * ========================================================================= */

void test_wait_prefix_skips_stray_ok(void)
{
    lb_feed("\r\nOK\r\n+CMGS: 4\r\n");
    char resp[64];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_wait_prefix(&h, "+CMGS:", resp, sizeof(resp), 1000));
    TEST_ASSERT_EQUAL_STRING("+CMGS: 4", resp);
}

void test_wait_final_consumes_until_ok(void)
{
    lb_feed("\r\nleftover\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_wait_final(&h, 1000));
    TEST_ASSERT_EQUAL_size_t(0, lb_rx_pending());
}

/* =========================================================================
 * Partial lines and line-storm guard
 * ========================================================================= */

/* The URC string is only valid during the callback — snapshot it. */
static char        g_urc_copy[128];
static const char *g_urc_line;
static void urc_capture(const char *urc, void *ctx)
{
    (void)ctx;
    (void)snprintf(g_urc_copy, sizeof(g_urc_copy), "%s", urc);
    g_urc_line = g_urc_copy;
}

void test_partial_line_is_never_surfaced(void)
{
    ec200_set_urc_handler(&h, urc_capture);
    g_urc_line = NULL;

    lb_feed("+CRE"); /* incomplete line */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_NULL(g_urc_line); /* nothing dispatched yet */

    lb_feed("G: 1\r\n"); /* completion arrives later */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_NOT_NULL(g_urc_line);
    TEST_ASSERT_EQUAL_STRING("+CREG: 1", g_urc_line);
}

void test_line_storm_guard(void)
{
    for (int i = 0; i < 70; i++) {
        lb_feed("JUNKLINE\r\n");
    }
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW,
        ec200_at_send(&h, "AT+STORM", NULL, 0, 1000));
}

/* =========================================================================
 * URC dispatch
 * ========================================================================= */

void test_poll_urc_idle_returns_ok(void)
{
    /* Regression: idle poll must be EC200_OK, not a timeout error. */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
}

void test_registered_urc_dispatched_mid_command(void)
{
    /* Regression: a URC arriving during a command must reach its handler
     * instead of being mis-filed as response data. */
    g_urc_line = NULL;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_register_urc(&h, "+FOO:", urc_capture, NULL));

    lb_on_write("AT+BAR", "\r\n+FOO: 7\r\nOK\r\n");
    char resp[64];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send(&h, "AT+BAR", resp, sizeof(resp), 1000));
    TEST_ASSERT_EQUAL_STRING("+FOO: 7", g_urc_line);
    TEST_ASSERT_EQUAL_STRING("", resp); /* not treated as response data */

    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_unregister_urc(&h, "+FOO:"));
}

void test_register_urc_validation_and_capacity(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_at_register_urc(&h, NULL, urc_capture, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_at_register_urc(&h, "+X:", NULL, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_at_unregister_urc(&h, "+NOTREG:"));

    static const char *prefixes[] = {
        "+A:", "+B:", "+C:", "+D:", "+E:", "+F:", "+G:", "+H:"
    };
    for (size_t i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_INT(EC200_OK,
            ec200_at_register_urc(&h, prefixes[i], urc_capture, NULL));
    }
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW,
        ec200_at_register_urc(&h, "+I:", urc_capture, NULL));
}

/* =========================================================================
 * Raw I/O
 * ========================================================================= */

void test_write_raw_retries_short_writes(void)
{
    lb_set_short_write(3);
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_write_raw(&h, (const uint8_t *)"ABCDEFGH", 8));
    /* Regression: all 8 bytes must reach the wire despite the short write. */
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "ABCDEFGH"));
}

void test_write_raw_io_error(void)
{
    lb_set_io_error(true);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_write_raw(&h, (const uint8_t *)"X", 1));
}

void test_read_raw_timeout_vs_io_error(void)
{
    uint8_t buf[4];
    uint16_t got = 0xFFFF;

    /* Regression: empty line must be a TIMEOUT, not an I/O error. */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_at_read_raw(&h, buf, sizeof(buf), 100, &got));
    TEST_ASSERT_EQUAL_UINT16(0, got);

    lb_set_io_error(true);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_read_raw(&h, buf, sizeof(buf), 100, &got));
}

void test_read_exact_short_read_reports_progress(void)
{
    lb_feed("ABC");
    uint8_t buf[8];
    uint16_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_at_read_exact(&h, buf, 5, 200, &got));
    TEST_ASSERT_EQUAL_UINT16(3, got);
    TEST_ASSERT_EQUAL_MEMORY("ABC", buf, 3);
}

/* =========================================================================
 * Field parsing
 * ========================================================================= */

void test_parse_int_field(void)
{
    int v = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_parse_int_field("+QMTOPEN: 0,3", 1, &v));
    TEST_ASSERT_EQUAL_INT(3, v);

    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_parse_int_field("+QMTSUB: 0,1,0,2", 2, &v));
    TEST_ASSERT_EQUAL_INT(0, v);

    /* Commas inside quoted strings must not count as separators. */
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_parse_int_field("+X: \"a,b\",7", 1, &v));
    TEST_ASSERT_EQUAL_INT(7, v);

    /* Negative values (dBm readings). */
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_parse_int_field("+QCSQ: \"LTE\",-52,-81", 1, &v));
    TEST_ASSERT_EQUAL_INT(-52, v);

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_at_parse_int_field("+X: 1,2", 5, &v));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_at_parse_int_field("+X: 1,\"abc\"", 1, &v));
}

/* =========================================================================
 * Coverage round: guard clauses, I/O failures, edge cases
 * ========================================================================= */

void test_not_ready_battery(void)
{
    ec200_handle_t raw;
    memset(&raw, 0, sizeof(raw));
    char buf[8];
    uint8_t b[8];
    uint16_t got;

    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_wait(&raw, "AT", "+X:", buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_expect(&raw, "AT", "+X:", buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_await_urc(&raw, "AT", "+X:", buf, sizeof(buf),
                                100, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_prompt(&raw, "AT", 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_wait_prefix(&raw, "+X:", buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_wait_final(&raw, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_write_raw(&raw, b, 1));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_read_raw(&raw, b, sizeof(b), 100, &got));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_read_exact(&raw, b, sizeof(b), 100, &got));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_poll_urc(&raw, 100));

    /* NULL-argument guards on an initialised handle */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_write_raw(&h, NULL, 1));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_read_raw(&h, NULL, 8, 100, &got));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_read_exact(&h, NULL, 8, 100, &got));
}

void test_transmit_failure_battery(void)
{
    lb_set_io_error(true);
    char buf[8];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_send(&h, "AT", NULL, 0, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_send_wait(&h, "AT", "+X:", buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_send_expect(&h, "AT", "+X:", buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_send_await_urc(&h, "AT", "+X:", buf, sizeof(buf),
                                100, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_send_prompt(&h, "AT", 100));
}

void test_read_io_error_paths(void)
{
    char buf[8];
    lb_set_read_io_error(true);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_send(&h, "AT+X", NULL, 0, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_wait_prefix(&h, "+X:", buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_wait_final(&h, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_poll_urc(&h, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_send_prompt(&h, "AT+X", 100));

    uint16_t got = 0;
    uint8_t b[4];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_at_read_exact(&h, b, sizeof(b), 100, &got));
}

void test_command_too_long_is_overflow(void)
{
    char big[600];
    memset(big, 'A', sizeof(big) - 1U);
    big[sizeof(big) - 1U] = '\0';
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW,
        ec200_at_send(&h, big, NULL, 0, 100));
}

void test_overlong_response_line_truncated(void)
{
    static char line[2200 + 8];
    memset(line, 'B', 2200);
    line[2200] = '\0';
    lb_feed(line);
    lb_feed("\r\nOK\r\n");
    /* The oversized line is truncated but the transaction still completes. */
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send(&h, "AT+BIG", NULL, 0, 1000));
}

void test_send_wait_null_and_tiny_resp_buf(void)
{
    lb_on_write("AT+CPIN?", "\r\n+CPIN: READY\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send_wait(&h, "AT+CPIN?", "+CPIN:", NULL, 0, 1000));

    lb_on_write("AT+CPIN?", "\r\n+CPIN: READY\r\n\r\nOK\r\n");
    char tiny[4];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send_wait(&h, "AT+CPIN?", "+CPIN:", tiny, sizeof(tiny),
                           1000));
    TEST_ASSERT_EQUAL_STRING("+CP", tiny);
}

void test_await_urc_result_before_ok(void)
{
    /* Non-standard order: result line first, trailing OK is drained. */
    lb_on_write("AT+ASYNC", "\r\n+QRES: 0,0\r\nOK\r\n");
    char resp[32];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send_await_urc(&h, "AT+ASYNC", "+QRES:",
                                resp, sizeof(resp), 1000, 1000));
    TEST_ASSERT_EQUAL_STRING("+QRES: 0,0", resp);
    TEST_ASSERT_EQUAL_size_t(0, lb_rx_pending());
}

void test_send_prompt_timeout(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_at_send_prompt(&h, "AT+NOPROMPT", 300));
}

void test_send_prompt_keeps_unexpected_byte(void)
{
    /* No space after '>': the peeked byte must be kept for later parsing. */
    lb_on_write("AT+CMGS", "\r\n>+CMGS: 9\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send_prompt(&h, "AT+CMGS=\"+1\"", 1000));
    char resp[32];
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_wait_prefix(&h, "+CMGS:", resp, sizeof(resp), 1000));
    TEST_ASSERT_EQUAL_STRING("+CMGS: 9", resp);
}

void test_send_prompt_ok_instead_is_parse_error(void)
{
    lb_on_write("AT+X", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_at_send_prompt(&h, "AT+X", 1000));
}

void test_send_prompt_dispatches_urc(void)
{
    g_urc_line = NULL;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_register_urc(&h, "+FOO:", urc_capture, NULL));
    lb_on_write("AT+X", "\r\n+FOO: 2\r\n> ");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_send_prompt(&h, "AT+X", 1000));
    TEST_ASSERT_EQUAL_STRING("+FOO: 2", g_urc_line);
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_unregister_urc(&h, "+FOO:"));
}

void test_wait_prefix_error_junk_and_storm(void)
{
    lb_feed("\r\n+CME ERROR: 5\r\n");
    char resp[32];
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_at_wait_prefix(&h, "+X:", resp, sizeof(resp), 1000));

    lb_feed("\r\nJUNK\r\n+CMGS: 1\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_wait_prefix(&h, "+CMGS:", resp, sizeof(resp), 1000));

    for (int i = 0; i < 70; i++) {
        lb_feed("NOISE\r\n");
    }
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW,
        ec200_at_wait_prefix(&h, "+ZZZ:", resp, sizeof(resp), 1000));
}

void test_wait_final_timeout_error_and_storm(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT, ec200_at_wait_final(&h, 200));

    lb_feed("\r\n+CME ERROR: 4\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_at_wait_final(&h, 1000));

    for (int i = 0; i < 70; i++) {
        lb_feed("NOISE\r\n");
    }
    TEST_ASSERT_EQUAL_INT(EC200_ERR_OVERFLOW, ec200_at_wait_final(&h, 1000));
}

void test_buffered_partial_feeds_raw_reads(void)
{
    /* A partial line pulled into the assembler must reach raw readers. */
    lb_feed("ABC");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));

    uint8_t buf[8];
    uint16_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_read_raw(&h, buf, sizeof(buf), 100, &got));
    TEST_ASSERT_EQUAL_UINT16(3, got);
    TEST_ASSERT_EQUAL_MEMORY("ABC", buf, 3);

    lb_feed("XY");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_read_exact(&h, buf, 2, 100, &got));
    TEST_ASSERT_EQUAL_UINT16(2, got);
    TEST_ASSERT_EQUAL_MEMORY("XY", buf, 2);
}

void test_register_urc_replaces_existing(void)
{
    g_urc_line = NULL;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_register_urc(&h, "+RPL:", urc_capture, NULL));
    /* Second registration for the same prefix replaces the first. */
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_register_urc(&h, "+RPL:", urc_capture, NULL));
    lb_feed("+RPL: 1\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_EQUAL_STRING("+RPL: 1", g_urc_line);
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_unregister_urc(&h, "+RPL:"));
}

void test_urc_api_null_handle_and_empty_prefix(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_at_register_urc(NULL, "+X:", urc_capture, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_at_register_urc(&h, "", urc_capture, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_at_unregister_urc(NULL, "+X:"));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_at_unregister_urc(&h, NULL));
}

void test_poll_urc_empty_lines_and_storm(void)
{
    lb_feed("\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));

    ec200_set_urc_handler(&h, urc_capture);
    for (int i = 0; i < 70; i++) {
        lb_feed("+Z: 1\r\n");
    }
    /* Line-cap guard: poll returns OK after processing its budget. */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    ec200_set_urc_handler(&h, NULL);
}

void test_parse_int_field_null_and_spaces(void)
{
    int v = 0;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_at_parse_int_field(NULL, 0, &v));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_at_parse_int_field("+X: 1", 0, NULL));
    /* Spaces after the separating comma are tolerated. */
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_parse_int_field("+X: 1, 7", 1, &v));
    TEST_ASSERT_EQUAL_INT(7, v);
}

/* =========================================================================
 * Branch-permutation round: every arm of every guard
 * ========================================================================= */

void test_branch_null_handle_and_null_args(void)
{
    char buf[8];
    uint8_t b[8];
    uint16_t got;

    /* NULL handle (the h != NULL arm of handle_ready) */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send(NULL, "AT", NULL, 0, 100));
    TEST_ASSERT_EQUAL_INT(-1, ec200_at_last_cme_error(NULL));
    TEST_ASSERT_EQUAL_INT(-1, ec200_at_last_cms_error(NULL));

    /* NULL command / prefix arguments on a live handle */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send(&h, NULL, NULL, 0, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_wait(&h, NULL, "+X:", buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_expect(&h, NULL, "+X:", buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_await_urc(&h, NULL, "+X:", buf, sizeof(buf),
                                100, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_wait(&h, "AT", NULL, buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_expect(&h, "AT", NULL, buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_await_urc(&h, "AT", NULL, buf, sizeof(buf),
                                100, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_send_prompt(&h, NULL, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_wait_prefix(&h, NULL, buf, sizeof(buf), 100));

    /* Remaining raw-I/O argument arms */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_read_raw(&h, b, 0, 100, &got));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_read_raw(&h, b, sizeof(b), 100, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_NOT_READY,
        ec200_at_read_exact(&h, b, sizeof(b), 100, NULL));
}

void test_branch_zero_sized_response_buffers(void)
{
    /* resp_buf != NULL with size 0: both clear-guard and copy-guard arms */
    char buf[8];
    lb_on_write("AT+A", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send(&h, "AT+A", buf, 0, 1000));

    lb_on_write("AT+CPIN?", "\r\n+CPIN: READY\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send_wait(&h, "AT+CPIN?", "+CPIN:", buf, 0, 1000));
}

void test_branch_accumulation_guard_arms(void)
{
    /* Junk line during a PREFIX transaction: not accumulated, not fatal */
    char resp[64];
    lb_on_write("AT+CPIN?", "\r\nJUNK\r\n+CPIN: READY\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send_wait(&h, "AT+CPIN?", "+CPIN:", resp, sizeof(resp),
                           1000));
    TEST_ASSERT_EQUAL_STRING("+CPIN: READY", resp);

    /* Junk line with a NULL response buffer */
    lb_on_write("AT+B", "\r\nJUNK\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send(&h, "AT+B", NULL, 0, 1000));

    /* Body line too large for the response buffer: dropped, not fatal */
    char tiny[8];
    lb_on_write("AT+C", "\r\nTHISLINEISWAYTOOLONG\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_send(&h, "AT+C", tiny, sizeof(tiny), 1000));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

void test_branch_prompt_arms(void)
{
    /* '>' inside a line is data, not the prompt */
    lb_on_write("AT+P1", "\r\nA>B\r\n> ");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_send_prompt(&h, "AT+P1", 1000));

    /* Bare '>' with nothing after it: the peek times out harmlessly */
    lb_on_write("AT+P2", "\r\n>");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_send_prompt(&h, "AT+P2", 1000));
}

void test_branch_urc_table_arms(void)
{
    /* Two entries: dispatching the second walks past the first (mismatch) */
    g_urc_line = NULL;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_register_urc(&h, "+AA:", urc_capture, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_register_urc(&h, "+BB:", urc_capture, NULL));
    lb_feed("+BB: 1\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
    TEST_ASSERT_EQUAL_STRING("+BB: 1", g_urc_line);
    /* Unregistering the second walks past the first (strcmp mismatch) */
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_unregister_urc(&h, "+BB:"));
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_unregister_urc(&h, "+AA:"));

    /* Unmatched line with NO fallback handler installed */
    lb_feed("UNMATCHED\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_at_poll_urc(&h, 0));
}

void test_branch_parse_int_field_no_colon(void)
{
    /* Documented: without a colon, field 0 starts at the line start. */
    int v = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_at_parse_int_field("5,7", 1, &v));
    TEST_ASSERT_EQUAL_INT(7, v);
}

/* ========================================================================= */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_send_ok);
    RUN_TEST(test_send_collects_body_lines);
    RUN_TEST(test_send_timeout_when_no_response);
    RUN_TEST(test_send_plain_error_maps_to_module_error);
    RUN_TEST(test_send_not_ready);
    RUN_TEST(test_cme_error_code);
    RUN_TEST(test_cms_after_cme_not_misclassified);
    RUN_TEST(test_send_wait_reads_through_ok);
    RUN_TEST(test_send_wait_ok_without_prefix_is_parse_error);
    RUN_TEST(test_send_expect_leaves_raw_data);
    RUN_TEST(test_await_urc_ok_then_urc);
    RUN_TEST(test_await_urc_cme_error);
    RUN_TEST(test_await_urc_timeout_waiting_for_result);
    RUN_TEST(test_send_prompt_ok);
    RUN_TEST(test_send_prompt_error_aborts_immediately);
    RUN_TEST(test_wait_prefix_skips_stray_ok);
    RUN_TEST(test_wait_final_consumes_until_ok);
    RUN_TEST(test_partial_line_is_never_surfaced);
    RUN_TEST(test_line_storm_guard);
    RUN_TEST(test_poll_urc_idle_returns_ok);
    RUN_TEST(test_registered_urc_dispatched_mid_command);
    RUN_TEST(test_register_urc_validation_and_capacity);
    RUN_TEST(test_write_raw_retries_short_writes);
    RUN_TEST(test_write_raw_io_error);
    RUN_TEST(test_read_raw_timeout_vs_io_error);
    RUN_TEST(test_read_exact_short_read_reports_progress);
    RUN_TEST(test_parse_int_field);
    RUN_TEST(test_not_ready_battery);
    RUN_TEST(test_transmit_failure_battery);
    RUN_TEST(test_read_io_error_paths);
    RUN_TEST(test_command_too_long_is_overflow);
    RUN_TEST(test_overlong_response_line_truncated);
    RUN_TEST(test_send_wait_null_and_tiny_resp_buf);
    RUN_TEST(test_await_urc_result_before_ok);
    RUN_TEST(test_send_prompt_timeout);
    RUN_TEST(test_send_prompt_keeps_unexpected_byte);
    RUN_TEST(test_send_prompt_ok_instead_is_parse_error);
    RUN_TEST(test_send_prompt_dispatches_urc);
    RUN_TEST(test_wait_prefix_error_junk_and_storm);
    RUN_TEST(test_wait_final_timeout_error_and_storm);
    RUN_TEST(test_buffered_partial_feeds_raw_reads);
    RUN_TEST(test_register_urc_replaces_existing);
    RUN_TEST(test_urc_api_null_handle_and_empty_prefix);
    RUN_TEST(test_poll_urc_empty_lines_and_storm);
    RUN_TEST(test_parse_int_field_null_and_spaces);
    RUN_TEST(test_branch_null_handle_and_null_args);
    RUN_TEST(test_branch_zero_sized_response_buffers);
    RUN_TEST(test_branch_accumulation_guard_arms);
    RUN_TEST(test_branch_prompt_arms);
    RUN_TEST(test_branch_urc_table_arms);
    RUN_TEST(test_branch_parse_int_field_no_colon);
    return UNITY_END();
}
