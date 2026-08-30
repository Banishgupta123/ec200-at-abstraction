/**
 * @file test_secure.c
 * @brief Unit tests for the secure-transport modules: file, SSL, TLS
 *        sockets, and the HTTPS/MQTTS wiring.
 */

#include <string.h>
#include <stdio.h>

#include "test_helpers.h"

static ec200_handle_t h;

void setUp(void)  { SETUP_MODEM(&h); }
void tearDown(void) { }

/* =========================================================================
 * File system
 * ========================================================================= */

void test_file_upload_ok(void)
{
    lb_on_write("AT+QFUPL=\"ca.pem\",5,60", "\r\nCONNECT\r\n");
    lb_on_write("hello", "\r\n+QFUPL: 5,4660\r\nOK\r\n");
    uint16_t crc = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_file_upload(&h, "ca.pem", (const uint8_t *)"hello", 5, &crc));
    TEST_ASSERT_EQUAL_UINT16(4660, crc);
}

void test_file_upload_no_checksum_arg(void)
{
    lb_on_write("AT+QFUPL", "\r\nCONNECT\r\n");
    lb_on_write("abc", "\r\n+QFUPL: 3,1\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_file_upload(&h, "f", (const uint8_t *)"abc", 3, NULL));
}

void test_file_upload_bad_params(void)
{
    const uint8_t d[4] = "abc";
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_file_upload(&h, NULL, d, 3, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_file_upload(&h, "", d, 3, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_file_upload(&h, "f", NULL, 3, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_file_upload(&h, "f", d, 0, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_file_upload(&h, "f", d, 200000, NULL));
    char longname[EC200_MAX_FILENAME_LEN + 4];
    memset(longname, 'n', sizeof(longname) - 1U);
    longname[sizeof(longname) - 1U] = '\0';
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_file_upload(&h, longname, d, 3, NULL));
}

void test_file_upload_failures(void)
{
    /* CONNECT refused */
    lb_on_write("AT+QFUPL", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_file_upload(&h, "f", (const uint8_t *)"abc", 3, NULL));

    /* write fails after CONNECT */
    SETUP_MODEM(&h);
    lb_on_write("AT+QFUPL", "\r\nCONNECT\r\n");
    lb_fail_write_after(1);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_file_upload(&h, "f", (const uint8_t *)"abc", 3, NULL));

    /* no +QFUPL result */
    SETUP_MODEM(&h);
    lb_on_write("AT+QFUPL", "\r\nCONNECT\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_file_upload(&h, "f", (const uint8_t *)"abc", 3, NULL));

    /* +QFUPL with no checksum field: crc defaults to 0 */
    SETUP_MODEM(&h);
    lb_on_write("AT+QFUPL", "\r\nCONNECT\r\n");
    lb_on_write("abc", "\r\n+QFUPL: 3\r\nOK\r\n");
    uint16_t crc = 0xFFFF;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_file_upload(&h, "f", (const uint8_t *)"abc", 3, &crc));
    TEST_ASSERT_EQUAL_UINT16(0, crc);
}

void test_file_delete(void)
{
    lb_on_write("AT+QFDEL=\"ca.pem\"", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_file_delete(&h, "ca.pem"));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_file_delete(&h, NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_file_delete(&h, ""));
    char longname[EC200_MAX_FILENAME_LEN + 4];
    memset(longname, 'n', sizeof(longname) - 1U);
    longname[sizeof(longname) - 1U] = '\0';
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_file_delete(&h, longname));
}

void test_file_exists(void)
{
    lb_on_write("AT+QFLST=\"ca.pem\"",
                "\r\n+QFLST: \"ca.pem\",1200\r\n\r\nOK\r\n");
    bool ex = false;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_file_exists(&h, "ca.pem", &ex));
    TEST_ASSERT_TRUE(ex);

    /* not found -> +CME ERROR -> exists=false, still EC200_OK */
    lb_on_write("AT+QFLST=\"none\"", "\r\n+CME ERROR: 417\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_file_exists(&h, "none", &ex));
    TEST_ASSERT_FALSE(ex);

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_file_exists(&h, "f", NULL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_file_exists(&h, NULL, &ex));
}

void test_file_exists_io_error_propagates(void)
{
    bool ex = false;
    lb_set_read_io_error(true);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_file_exists(&h, "f", &ex));
}

void test_file_size(void)
{
    lb_on_write("AT+QFLST=\"ca.pem\"",
                "\r\n+QFLST: \"ca.pem\",1200\r\n\r\nOK\r\n");
    uint32_t sz = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_file_size(&h, "ca.pem", &sz));
    TEST_ASSERT_EQUAL_UINT32(1200, sz);

    lb_on_write("AT+QFLST=\"x\"", "\r\n+QFLST: \"x\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_file_size(&h, "x", &sz));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_file_size(&h, "f", NULL));
}

void test_file_storage(void)
{
    lb_on_write("AT+QFLDS=\"UFS\"",
                "\r\n+QFLDS: 1048576,2097152\r\n\r\nOK\r\n");
    ec200_file_storage_t s;
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_file_storage(&h, &s));
    TEST_ASSERT_EQUAL_UINT32(1048576, s.free_bytes);
    TEST_ASSERT_EQUAL_UINT32(2097152, s.total_bytes);

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_file_storage(&h, NULL));

    lb_on_write("AT+QFLDS", "\r\n+QFLDS: 5\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_file_storage(&h, &s));
}

/* =========================================================================
 * SSL context configuration
 * ========================================================================= */

static ec200_ssl_config_t base_cfg(void)
{
    ec200_ssl_config_t c;
    memset(&c, 0, sizeof(c));
    c.ctx_id = 2;
    c.version = EC200_SSL_VER_TLS1_2;
    c.ciphersuite = EC200_SSL_CIPHER_ALL;
    c.seclevel = EC200_SSL_SECLEVEL_NONE;
    return c;
}

void test_ssl_configure_none(void)
{
    ec200_ssl_config_t c = base_cfg();
    lb_on_write("\"sslversion\",2,3", "\r\nOK\r\n");
    lb_on_write("\"ciphersuite\",2,0XFFFF", "\r\nOK\r\n");
    lb_on_write("\"seclevel\",2,0", "\r\nOK\r\n");
    lb_on_write("\"ignorelocaltime\",2,0", "\r\nOK\r\n");
    lb_on_write("\"sni\",2,0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ssl_configure(&h, &c));
}

void test_ssl_configure_server_auth(void)
{
    ec200_ssl_config_t c = base_cfg();
    c.seclevel = EC200_SSL_SECLEVEL_SERVER;
    c.ignore_localtime = true;
    c.enable_sni = true;
    snprintf(c.cacert, sizeof(c.cacert), "%s", "ca.pem");

    lb_on_write("\"sslversion\",2,3", "\r\nOK\r\n");
    lb_on_write("\"ciphersuite\"", "\r\nOK\r\n");
    lb_on_write("\"seclevel\",2,1", "\r\nOK\r\n");
    lb_on_write("\"cacert\",2,\"ca.pem\"", "\r\nOK\r\n");
    lb_on_write("\"ignorelocaltime\",2,1", "\r\nOK\r\n");
    lb_on_write("\"sni\",2,1", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ssl_configure(&h, &c));
}

void test_ssl_configure_mutual(void)
{
    ec200_ssl_config_t c = base_cfg();
    c.seclevel = EC200_SSL_SECLEVEL_MUTUAL;
    snprintf(c.cacert, sizeof(c.cacert), "%s", "ca.pem");
    snprintf(c.clientcert, sizeof(c.clientcert), "%s", "cl.pem");
    snprintf(c.clientkey, sizeof(c.clientkey), "%s", "cl.key");

    lb_on_write("\"sslversion\"", "\r\nOK\r\n");
    lb_on_write("\"ciphersuite\"", "\r\nOK\r\n");
    lb_on_write("\"seclevel\",2,2", "\r\nOK\r\n");
    lb_on_write("\"cacert\",2,\"ca.pem\"", "\r\nOK\r\n");
    lb_on_write("\"clientcert\",2,\"cl.pem\"", "\r\nOK\r\n");
    lb_on_write("\"clientkey\",2,\"cl.key\"", "\r\nOK\r\n");
    lb_on_write("\"ignorelocaltime\"", "\r\nOK\r\n");
    lb_on_write("\"sni\"", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ssl_configure(&h, &c));
}

void test_ssl_configure_validation(void)
{
    ec200_ssl_config_t c = base_cfg();
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ssl_configure(&h, NULL));

    c.ctx_id = 6;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ssl_configure(&h, &c));

    c = base_cfg();
    c.version = (ec200_ssl_version_t)9;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ssl_configure(&h, &c));

    c = base_cfg();
    c.seclevel = (ec200_ssl_seclevel_t)9;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ssl_configure(&h, &c));

    /* server auth without a CA cert */
    c = base_cfg();
    c.seclevel = EC200_SSL_SECLEVEL_SERVER;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ssl_configure(&h, &c));

    /* mutual without client cert/key */
    c = base_cfg();
    c.seclevel = EC200_SSL_SECLEVEL_MUTUAL;
    snprintf(c.cacert, sizeof(c.cacert), "%s", "ca.pem");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ssl_configure(&h, &c));
}

void test_ssl_configure_step_failures(void)
{
    ec200_ssl_config_t c = base_cfg();
    c.seclevel = EC200_SSL_SECLEVEL_MUTUAL;
    snprintf(c.cacert, sizeof(c.cacert), "%s", "ca.pem");
    snprintf(c.clientcert, sizeof(c.clientcert), "%s", "cl.pem");
    snprintf(c.clientkey, sizeof(c.clientkey), "%s", "cl.key");

    /* version step fails */
    lb_on_write("\"sslversion\"", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_ssl_configure(&h, &c));

    /* ciphersuite step fails */
    SETUP_MODEM(&h);
    lb_on_write("\"sslversion\"", "\r\nOK\r\n");
    lb_on_write("\"ciphersuite\"", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_ssl_configure(&h, &c));

    /* seclevel step fails */
    SETUP_MODEM(&h);
    lb_on_write("\"sslversion\"", "\r\nOK\r\n");
    lb_on_write("\"ciphersuite\"", "\r\nOK\r\n");
    lb_on_write("\"seclevel\"", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_ssl_configure(&h, &c));

    /* cacert step fails */
    SETUP_MODEM(&h);
    lb_on_write("\"sslversion\"", "\r\nOK\r\n");
    lb_on_write("\"ciphersuite\"", "\r\nOK\r\n");
    lb_on_write("\"seclevel\"", "\r\nOK\r\n");
    lb_on_write("\"cacert\"", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_ssl_configure(&h, &c));

    /* clientcert step fails */
    SETUP_MODEM(&h);
    lb_on_write("\"sslversion\"", "\r\nOK\r\n");
    lb_on_write("\"ciphersuite\"", "\r\nOK\r\n");
    lb_on_write("\"seclevel\"", "\r\nOK\r\n");
    lb_on_write("\"cacert\"", "\r\nOK\r\n");
    lb_on_write("\"clientcert\"", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_ssl_configure(&h, &c));

    /* clientkey step fails */
    SETUP_MODEM(&h);
    lb_on_write("\"sslversion\"", "\r\nOK\r\n");
    lb_on_write("\"ciphersuite\"", "\r\nOK\r\n");
    lb_on_write("\"seclevel\"", "\r\nOK\r\n");
    lb_on_write("\"cacert\"", "\r\nOK\r\n");
    lb_on_write("\"clientcert\"", "\r\nOK\r\n");
    lb_on_write("\"clientkey\"", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_ssl_configure(&h, &c));

    /* ignorelocaltime step fails */
    SETUP_MODEM(&h);
    lb_on_write("\"sslversion\"", "\r\nOK\r\n");
    lb_on_write("\"ciphersuite\"", "\r\nOK\r\n");
    lb_on_write("\"seclevel\"", "\r\nOK\r\n");
    lb_on_write("\"cacert\"", "\r\nOK\r\n");
    lb_on_write("\"clientcert\"", "\r\nOK\r\n");
    lb_on_write("\"clientkey\"", "\r\nOK\r\n");
    lb_on_write("\"ignorelocaltime\"", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_ssl_configure(&h, &c));
}

void test_ssl_set_seclevel(void)
{
    lb_on_write("AT+QSSLCFG=\"seclevel\",1,2", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_ssl_set_seclevel(&h, 1, EC200_SSL_SECLEVEL_MUTUAL));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_set_seclevel(&h, 6, EC200_SSL_SECLEVEL_NONE));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_set_seclevel(&h, 1, (ec200_ssl_seclevel_t)9));
}

/* =========================================================================
 * HTTPS / MQTTS wiring
 * ========================================================================= */

void test_http_set_ssl_context(void)
{
    lb_on_write("AT+QHTTPCFG=\"sslctxid\",3", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_http_set_ssl_context(&h, 3));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_http_set_ssl_context(&h, 6));
}

void test_mqtts_open_enables_ssl(void)
{
    ec200_mqtt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.host, sizeof(cfg.host), "%s", "broker");
    cfg.port = 8883;
    cfg.use_tls = true;
    cfg.ssl_ctx_id = 4;

    lb_on_write("AT+QMTCFG=\"ssl\",0,1,4", "\r\nOK\r\n");
    lb_on_write("AT+QMTOPEN", "\r\nOK\r\n+QMTOPEN: 0,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_mqtt_open(&h, &cfg));
    TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "AT+QMTCFG=\"ssl\",0,1,4"));
}

void test_mqtts_bad_ctx_and_cfg_failure(void)
{
    ec200_mqtt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.host, sizeof(cfg.host), "%s", "broker");
    cfg.use_tls = true;
    cfg.ssl_ctx_id = 9;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_mqtt_open(&h, &cfg));

    cfg.ssl_ctx_id = 0;
    lb_on_write("AT+QMTCFG=\"ssl\"", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE, ec200_mqtt_open(&h, &cfg));
}

/* =========================================================================
 * TLS sockets
 * ========================================================================= */

void test_ssl_socket_open_ok(void)
{
    lb_on_write("AT+QSSLOPEN=1,2,0,\"host\",8883,0",
                "\r\nOK\r\n+QSSLOPEN: 0,0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_ssl_socket_open(&h, 1, 2, 0, "host", 8883));
}

void test_ssl_socket_open_failures(void)
{
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_open(&h, 1, 2, 0, NULL, 443));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_open(&h, 1, 2, 12, "h", 443));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_open(&h, 0, 2, 0, "h", 443));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_open(&h, 1, 6, 0, "h", 443));

    lb_on_write("AT+QSSLOPEN", "\r\nOK\r\n+QSSLOPEN: 0,559\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_ssl_socket_open(&h, 1, 2, 0, "h", 443));

    lb_on_write("AT+QSSLOPEN", "\r\nOK\r\n+QSSLOPEN: 0\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_ssl_socket_open(&h, 1, 2, 0, "h", 443));
}

void test_ssl_socket_send(void)
{
    lb_on_write("AT+QSSLSEND=0,5", "\r\n> ");
    lb_on_write("hello", "\r\nSEND OK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_ssl_socket_send(&h, 0, (const uint8_t *)"hello", 5));

    lb_on_write("AT+QSSLSEND=0,2", "\r\n> ");
    lb_on_write("XY", "\r\nSEND FAIL\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_ssl_socket_send(&h, 0, (const uint8_t *)"XY", 2));

    uint8_t d[4] = {0};
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_send(&h, 0, NULL, 4));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_send(&h, 12, d, 4));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_send(&h, 0, d, EC200_MAX_PAYLOAD_LEN + 1U));
}

void test_ssl_socket_recv(void)
{
    lb_on_write("AT+QSSLRECV=0,64", "\r\n+QSSLRECV: 5\r\nHELLO\r\nOK\r\n");
    uint8_t buf[64];
    uint16_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_ssl_socket_recv(&h, 0, buf, sizeof(buf), &got, 1000));
    TEST_ASSERT_EQUAL_UINT16(5, got);
    TEST_ASSERT_EQUAL_MEMORY("HELLO", buf, 5);

    /* no data */
    lb_on_write("AT+QSSLRECV=0,64", "\r\n+QSSLRECV: 0\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK,
        ec200_ssl_socket_recv(&h, 0, buf, sizeof(buf), &got, 1000));
    TEST_ASSERT_EQUAL_UINT16(0, got);

    /* parse error / oversize */
    lb_on_write("AT+QSSLRECV=0,64", "\r\n+QSSLRECV: 999\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_ssl_socket_recv(&h, 0, buf, sizeof(buf), &got, 1000));

    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_recv(&h, 0, NULL, 64, &got, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_recv(&h, 12, buf, 64, &got, 100));
}

void test_ssl_socket_close(void)
{
    lb_on_write("AT+QSSLCLOSE=0", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_ssl_socket_close(&h, 0));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ssl_socket_close(&h, 12));
}

void test_ssl_socket_failure_propagation(void)
{
    /* open: no result URC -> timeout */
    lb_on_write("AT+QSSLOPEN", "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_ssl_socket_open(&h, 1, 2, 0, "h", 443));

    /* send: ERROR instead of prompt */
    SETUP_MODEM(&h);
    lb_on_write("AT+QSSLSEND", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_MODULE,
        ec200_ssl_socket_send(&h, 0, (const uint8_t *)"x", 1));

    /* send: payload write fails after the prompt */
    SETUP_MODEM(&h);
    lb_on_write("AT+QSSLSEND", "\r\n> ");
    lb_fail_write_after(1);
    TEST_ASSERT_EQUAL_INT(EC200_ERR_IO,
        ec200_ssl_socket_send(&h, 0, (const uint8_t *)"x", 1));

    /* send: no SEND OK/FAIL after payload */
    SETUP_MODEM(&h);
    lb_on_write("AT+QSSLSEND", "\r\n> ");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_ssl_socket_send(&h, 0, (const uint8_t *)"x", 1));

    /* recv: command error */
    SETUP_MODEM(&h);
    lb_on_write("AT+QSSLRECV", "\r\n+CME ERROR: 3\r\n");
    uint8_t buf[16];
    uint16_t got = 0;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME,
        ec200_ssl_socket_recv(&h, 0, buf, sizeof(buf), &got, 1000));

    /* recv: announced length but data never arrives (short read) */
    SETUP_MODEM(&h);
    lb_on_write("AT+QSSLRECV", "\r\n+QSSLRECV: 5\r\nAB");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_TIMEOUT,
        ec200_ssl_socket_recv(&h, 0, buf, sizeof(buf), &got, 300));
    TEST_ASSERT_EQUAL_UINT16(2, got);
}

void test_file_storage_command_fails(void)
{
    lb_on_write("AT+QFLDS", "\r\n+CME ERROR: 3\r\n");
    ec200_file_storage_t s;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_CME, ec200_file_storage(&h, &s));
}

/* =========================================================================
 * Branch-permutation battery: every arm of every compound guard
 * ========================================================================= */

void test_branch_file_guard_arms(void)
{
    const uint8_t d[4] = "abc";
    uint32_t sz; bool ex;
    ec200_file_storage_t st;

    /* size/exists: empty name, NULL out, long name arms */
    char longname[EC200_MAX_FILENAME_LEN + 4];
    memset(longname, 'n', sizeof(longname) - 1U);
    longname[sizeof(longname) - 1U] = '\0';
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_file_size(&h, "", &sz));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_file_size(&h, NULL, &sz));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_file_size(&h, longname, &sz));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_file_exists(&h, "", &ex));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_file_exists(&h, longname, &ex));

    /* upload data==NULL vs len==0 vs len too big already covered; add the
     * empty-data-string form via len path */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_file_upload(&h, "f", d, 70000, NULL));

    /* exists: not-found reported as EC200_ERR_MODULE (bare ERROR) */
    lb_on_write("AT+QFLST", "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_file_exists(&h, "f", &ex));
    TEST_ASSERT_FALSE(ex);

    /* size: negative size field -> parse error (sz<0 arm) */
    lb_on_write("AT+QFLST", "\r\n+QFLST: \"f\",-3\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_file_size(&h, "f", &sz));

    /* storage: total negative, free-parse-fail, and free-negative arms */
    lb_on_write("AT+QFLDS", "\r\n+QFLDS: 10,-1\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_file_storage(&h, &st));
    lb_on_write("AT+QFLDS", "\r\n+QFLDS: x,5\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_file_storage(&h, &st));
    lb_on_write("AT+QFLDS", "\r\n+QFLDS: -1,10\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE, ec200_file_storage(&h, &st));

    /* exists: prefix matched but size field missing -> PARSE arm -> false */
    lb_on_write("AT+QFLST", "\r\n+QFLST: \"f\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_OK, ec200_file_exists(&h, "f", &ex));
    TEST_ASSERT_FALSE(ex);
}

void test_branch_ssl_guard_arms(void)
{
    /* set_seclevel: negative level arm */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_set_seclevel(&h, 1, (ec200_ssl_seclevel_t)-1));

    /* configure: negative version / seclevel arms */
    ec200_ssl_config_t c = base_cfg();
    c.version = (ec200_ssl_version_t)-1;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ssl_configure(&h, &c));
    c = base_cfg();
    c.seclevel = (ec200_ssl_seclevel_t)-1;
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ssl_configure(&h, &c));

    /* mutual: clientcert present but clientkey empty (the clientkey arm) */
    c = base_cfg();
    c.seclevel = EC200_SSL_SECLEVEL_MUTUAL;
    snprintf(c.cacert, sizeof(c.cacert), "%s", "ca.pem");
    snprintf(c.clientcert, sizeof(c.clientcert), "%s", "cl.pem");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM, ec200_ssl_configure(&h, &c));
}

void test_branch_ssl_socket_guard_arms(void)
{
    uint8_t buf[16]; uint16_t got;
    const uint8_t d[4] = "abc";

    /* open: empty host, pdp>16 arms */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_open(&h, 1, 2, 0, "", 443));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_open(&h, 17, 2, 0, "h", 443));

    /* send: len==0 arm */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_send(&h, 0, d, 0));

    /* recv: NULL bytes_read, max_len==0 arms */
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_recv(&h, 0, buf, 16, NULL, 100));
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARAM,
        ec200_ssl_socket_recv(&h, 0, buf, 0, &got, 100));

    /* recv: negative announced length (actual<0 arm) */
    lb_on_write("AT+QSSLRECV", "\r\n+QSSLRECV: -1\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_ssl_socket_recv(&h, 0, buf, 16, &got, 1000));

    /* recv: non-numeric length field (parse!=OK arm) */
    SETUP_MODEM(&h);
    lb_on_write("AT+QSSLRECV", "\r\n+QSSLRECV: x\r\nOK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200_ERR_PARSE,
        ec200_ssl_socket_recv(&h, 0, buf, 16, &got, 1000));
}

/* ========================================================================= */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_file_upload_ok);
    RUN_TEST(test_file_upload_no_checksum_arg);
    RUN_TEST(test_file_upload_bad_params);
    RUN_TEST(test_file_upload_failures);
    RUN_TEST(test_file_delete);
    RUN_TEST(test_file_exists);
    RUN_TEST(test_file_exists_io_error_propagates);
    RUN_TEST(test_file_size);
    RUN_TEST(test_file_storage);
    RUN_TEST(test_ssl_configure_none);
    RUN_TEST(test_ssl_configure_server_auth);
    RUN_TEST(test_ssl_configure_mutual);
    RUN_TEST(test_ssl_configure_validation);
    RUN_TEST(test_ssl_configure_step_failures);
    RUN_TEST(test_ssl_set_seclevel);
    RUN_TEST(test_http_set_ssl_context);
    RUN_TEST(test_mqtts_open_enables_ssl);
    RUN_TEST(test_mqtts_bad_ctx_and_cfg_failure);
    RUN_TEST(test_ssl_socket_open_ok);
    RUN_TEST(test_ssl_socket_open_failures);
    RUN_TEST(test_ssl_socket_send);
    RUN_TEST(test_ssl_socket_recv);
    RUN_TEST(test_ssl_socket_close);
    RUN_TEST(test_ssl_socket_failure_propagation);
    RUN_TEST(test_file_storage_command_fails);
    RUN_TEST(test_branch_file_guard_arms);
    RUN_TEST(test_branch_ssl_guard_arms);
    RUN_TEST(test_branch_ssl_socket_guard_arms);
    return UNITY_END();
}
