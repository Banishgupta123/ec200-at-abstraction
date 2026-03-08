/**
 * @file ec200.c
 * @brief Main library initialisation and core AT command implementations.
 */

#include "ec200.h"

#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_init(ec200_handle_t *h,
                          ec200_write_fn  write_fn,
                          ec200_read_fn   read_fn,
                          ec200_delay_fn  delay_fn,
                          void           *user_ctx)
{
    if (!h || !write_fn || !read_fn || !delay_fn) {
        return EC200_ERR_PARAM;
    }

    memset(h, 0, sizeof(*h));

    h->write      = write_fn;
    h->read       = read_fn;
    h->delay_ms   = delay_fn;
    h->user_ctx   = user_ctx;

    h->_last_cme_error = -1;
    h->_last_cms_error = -1;
    h->_initialised    = true;

    /* Probe the module — try up to 3 times to handle any ongoing reset */
    for (int i = 0; i < 3; i++) {
        if (ec200_check_at(h) == EC200_OK) {
            return EC200_OK;
        }
        h->delay_ms(500U, h->user_ctx);
    }

    return EC200_ERR_TIMEOUT;
}

ec200_status_t ec200_check_at(ec200_handle_t *h)
{
    return ec200_at_send(h, "AT", NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_get_imei(ec200_handle_t *h,
                              char           *imei,
                              size_t          imei_sz)
{
    if (!imei || imei_sz == 0) return EC200_ERR_PARAM;

    char resp[64];
    ec200_status_t st = ec200_at_send(h, "AT+GSN",
                                      resp, sizeof(resp),
                                      EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    strncpy(imei, resp, imei_sz - 1U);
    imei[imei_sz - 1U] = '\0';
    return EC200_OK;
}

ec200_status_t ec200_get_fw_version(ec200_handle_t *h,
                                    char           *ver,
                                    size_t          ver_sz)
{
    if (!ver || ver_sz == 0) return EC200_ERR_PARAM;

    char resp[EC200_MAX_FW_VER_LEN];
    ec200_status_t st = ec200_at_send(h, "AT+GMR",
                                      resp, sizeof(resp),
                                      EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    strncpy(ver, resp, ver_sz - 1U);
    ver[ver_sz - 1U] = '\0';
    return EC200_OK;
}

ec200_status_t ec200_get_module_info(ec200_handle_t *h,
                                     char           *info,
                                     size_t          info_sz)
{
    if (!info || info_sz == 0) return EC200_ERR_PARAM;

    ec200_status_t st = ec200_at_send(h, "ATI",
                                      info, info_sz,
                                      EC200_AT_TIMEOUT_DEFAULT);
    return st;
}

ec200_status_t ec200_set_echo(ec200_handle_t *h, bool enable)
{
    return ec200_at_send(h, enable ? "ATE1" : "ATE0",
                         NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_set_cmee(ec200_handle_t *h, uint8_t mode)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+CMEE=%u", (unsigned)mode);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

void ec200_set_urc_handler(ec200_handle_t       *h,
                           ec200_urc_handler_fn  handler)
{
    if (h) {
        h->urc_handler = handler;
    }
}

const char *ec200_status_str(ec200_status_t status)
{
    switch (status) {
        case EC200_OK:              return "OK";
        case EC200_ERR_TIMEOUT:     return "Timeout";
        case EC200_ERR_IO:          return "I/O error";
        case EC200_ERR_PARSE:       return "Parse error";
        case EC200_ERR_CME:         return "+CME ERROR";
        case EC200_ERR_CMS:         return "+CMS ERROR";
        case EC200_ERR_BUSY:        return "Busy";
        case EC200_ERR_PARAM:       return "Invalid parameter";
        case EC200_ERR_NOT_READY:   return "Not ready";
        case EC200_ERR_OVERFLOW:    return "Buffer overflow";
        case EC200_ERR_UNSUPPORTED: return "Unsupported";
        default:                    return "Unknown error";
    }
}
