/**
 * @file ec200_ssl.c
 * @brief TLS/SSL context configuration implementation (AT+QSSLCFG).
 */

#include "ec200_ssl.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>

ec200_status_t ec200_ssl_set_seclevel(ec200_handle_t      *h,
                                      uint8_t              ctx_id,
                                      ec200_ssl_seclevel_t level)
{
    if (ctx_id > 5U || (int)level < 0 || (int)level > 2) {
        return EC200_ERR_PARAM;
    }
    char cmd[48];
    (void)snprintf(cmd, sizeof(cmd), "AT+QSSLCFG=\"seclevel\",%u,%u",
                   (unsigned)ctx_id, (unsigned)level);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

/** Issue one `AT+QSSLCFG="key",ctx,num` and return its status. */
static ec200_status_t cfg_num(ec200_handle_t *h, uint8_t ctx,
                              const char *key, unsigned val)
{
    char cmd[64];
    (void)snprintf(cmd, sizeof(cmd), "AT+QSSLCFG=\"%s\",%u,%u",
                   key, (unsigned)ctx, val);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

/** Issue one `AT+QSSLCFG="key",ctx,"file"` and return its status. */
static ec200_status_t cfg_file(ec200_handle_t *h, uint8_t ctx,
                               const char *key, const char *file)
{
    char cmd[EC200_MAX_FILENAME_LEN + 48];
    (void)snprintf(cmd, sizeof(cmd), "AT+QSSLCFG=\"%s\",%u,\"%s\"",
                   key, (unsigned)ctx, file);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_ssl_configure(ec200_handle_t          *h,
                                   const ec200_ssl_config_t *cfg)
{
    if (!cfg || cfg->ctx_id > 5U) {
        return EC200_ERR_PARAM;
    }
    if ((int)cfg->version < 0 || (int)cfg->version > 4) {
        return EC200_ERR_PARAM;
    }
    if ((int)cfg->seclevel < 0 || (int)cfg->seclevel > 2) {
        return EC200_ERR_PARAM;
    }
    /* Required certs per level. */
    if (cfg->seclevel >= EC200_SSL_SECLEVEL_SERVER &&
        cfg->cacert[0] == '\0') {
        return EC200_ERR_PARAM;
    }
    if (cfg->seclevel == EC200_SSL_SECLEVEL_MUTUAL &&
        (cfg->clientcert[0] == '\0' || cfg->clientkey[0] == '\0')) {
        return EC200_ERR_PARAM;
    }

    ec200_status_t st;
    const uint8_t ctx = cfg->ctx_id;

    st = cfg_num(h, ctx, "sslversion", (unsigned)cfg->version);
    if (st != EC200_OK) { return st; }

    /* ciphersuite is a hex value; 0xFFFF selects "support all". */
    {
        char cmd[64];
        (void)snprintf(cmd, sizeof(cmd),
                       "AT+QSSLCFG=\"ciphersuite\",%u,0X%04X",
                       (unsigned)ctx, (unsigned)cfg->ciphersuite);
        st = ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
        if (st != EC200_OK) { return st; }
    }

    st = cfg_num(h, ctx, "seclevel", (unsigned)cfg->seclevel);
    if (st != EC200_OK) { return st; }

    if (cfg->cacert[0] != '\0') {
        st = cfg_file(h, ctx, "cacert", cfg->cacert);
        if (st != EC200_OK) { return st; }
    }
    if (cfg->clientcert[0] != '\0') {
        st = cfg_file(h, ctx, "clientcert", cfg->clientcert);
        if (st != EC200_OK) { return st; }
    }
    if (cfg->clientkey[0] != '\0') {
        st = cfg_file(h, ctx, "clientkey", cfg->clientkey);
        if (st != EC200_OK) { return st; }
    }

    st = cfg_num(h, ctx, "ignorelocaltime", cfg->ignore_localtime ? 1U : 0U);
    if (st != EC200_OK) { return st; }

    st = cfg_num(h, ctx, "sni", cfg->enable_sni ? 1U : 0U);
    return st;
}
