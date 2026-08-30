/**
 * @file ec200.c
 * @brief Main library initialisation and core AT command implementations.
 */

#include "ec200.h"

#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/**
 * @brief Run a plain-response query and copy the first response line out.
 *
 * Shared by the IMEI / firmware-version / IMSI style getters: the module
 * answers with a single body line followed by "OK".
 */
static ec200_status_t get_string_response(ec200_handle_t *h,
                                          const char     *cmd,
                                          char           *out,
                                          size_t          out_sz)
{
    char resp[EC200_MAX_FW_VER_LEN];
    ec200_status_t st = ec200_at_send(h, cmd, resp, sizeof(resp),
                                      EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    /* Use only the first line of the body. */
    char *nl = strchr(resp, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
    if (resp[0] == '\0') {
        return EC200_ERR_PARSE;
    }

    size_t len = strlen(resp);
    if (len >= out_sz) {
        return EC200_ERR_OVERFLOW; /* refuse to silently truncate an ID */
    }
    memcpy(out, resp, len + 1U);
    return EC200_OK;
}

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

    /* Probe the module — try up to 3 times to handle any ongoing reset. */
    ec200_status_t st = EC200_ERR_TIMEOUT;
    for (int i = 0; i < 3; i++) {
        st = ec200_check_at(h);
        if (st == EC200_OK) {
            break;
        }
        h->delay_ms(500U, h->user_ctx);
    }
    if (st != EC200_OK) {
        h->_initialised = false;
        return EC200_ERR_TIMEOUT;
    }

    /*
     * Disable command echo.  The module's factory default is ATE1; with echo
     * on, every echoed command line would be mis-parsed as response data.
     */
    st = ec200_set_echo(h, false);
    if (st != EC200_OK) {
        h->_initialised = false;
        return st;
    }

    return EC200_OK;
}

ec200_status_t ec200_check_at(ec200_handle_t *h)
{
    return ec200_at_send(h, "AT", NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_get_imei(ec200_handle_t *h,
                              char           *imei,
                              size_t          imei_sz)
{
    if (!imei || imei_sz == 0) {
        return EC200_ERR_PARAM;
    }
    return get_string_response(h, "AT+GSN", imei, imei_sz);
}

ec200_status_t ec200_get_fw_version(ec200_handle_t *h,
                                    char           *ver,
                                    size_t          ver_sz)
{
    if (!ver || ver_sz == 0) {
        return EC200_ERR_PARAM;
    }
    return get_string_response(h, "AT+GMR", ver, ver_sz);
}

ec200_status_t ec200_get_module_info(ec200_handle_t *h,
                                     char           *info,
                                     size_t          info_sz)
{
    if (!info || info_sz == 0) {
        return EC200_ERR_PARAM;
    }
    return ec200_at_send(h, "ATI", info, info_sz, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_set_echo(ec200_handle_t *h, bool enable)
{
    return ec200_at_send(h, enable ? "ATE1" : "ATE0",
                         NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_set_cmee(ec200_handle_t *h, uint8_t mode)
{
    if (mode > 2U) {
        return EC200_ERR_PARAM;
    }
    char cmd[16];
    (void)snprintf(cmd, sizeof(cmd), "AT+CMEE=%u", (unsigned)mode);
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
        case EC200_ERR_MODULE:      return "Module error";
        /* Listed explicitly so every enumerator is documented here; the
         * default also catches out-of-range integer values.
         * NOLINTNEXTLINE(bugprone-branch-clone) */
        case EC200_ERR_UNKNOWN:     return "Unknown error";
        default:                    return "Unknown error";
    }
}
