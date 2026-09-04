/**
 * @file ec200_file.c
 * @brief Modem filesystem access implementation (AT+QF*).
 */

#include "ec200_file.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Bounded by the single raw write (uint16_t); certs/keys are far smaller. */
#define FILE_MAX_UPLOAD  (65535U)

ec200_status_t ec200_file_upload(ec200_handle_t *h,
                                 const char     *name,
                                 const uint8_t  *data,
                                 uint32_t        len,
                                 uint16_t       *checksum)
{
    if (!name || name[0] == '\0' || !data ||
        len == 0U || len > FILE_MAX_UPLOAD ||
        strlen(name) >= EC200_MAX_FILENAME_LEN) {
        return EC200_ERR_PARAM;
    }

    /* AT+QFUPL=<name>,<size>,<timeout> -> CONNECT -> data -> +QFUPL: len,crc */
    char cmd[EC200_MAX_FILENAME_LEN + 32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QFUPL=\"%s\",%u,60",
                   name, (unsigned)len);

    char resp[48];
    ec200_status_t st = ec200_at_send_expect(h, cmd, "CONNECT",
                                             resp, sizeof(resp),
                                             EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    st = ec200_at_write_raw(h, data, (uint16_t)len);
    if (st != EC200_OK) {
        return st;
    }

    st = ec200_at_wait_prefix(h, "+QFUPL:", resp, sizeof(resp),
                              EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }

    if (checksum != NULL) {
        int crc = 0;
        if (ec200_at_parse_int_field(resp, 1U, &crc) == EC200_OK) {
            *checksum = (uint16_t)crc;
        } else {
            *checksum = 0U;
        }
    }
    return ec200_at_wait_final(h, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_file_delete(ec200_handle_t *h, const char *name)
{
    if (!name || name[0] == '\0' ||
        strlen(name) >= EC200_MAX_FILENAME_LEN) {
        return EC200_ERR_PARAM;
    }
    char cmd[EC200_MAX_FILENAME_LEN + 16];
    (void)snprintf(cmd, sizeof(cmd), "AT+QFDEL=\"%s\"", name);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

/**
 * @brief Shared AT+QFLST="<name>" query returning the matched size.
 * @return EC200_OK (found, *size set), EC200_ERR_MODULE (not found via
 *         +CME ERROR), or a lower-level error.
 */
static ec200_status_t qflst_size(ec200_handle_t *h, const char *name,
                                 uint32_t *size)
{
    char cmd[EC200_MAX_FILENAME_LEN + 16];
    (void)snprintf(cmd, sizeof(cmd), "AT+QFLST=\"%s\"", name);

    char resp[96];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+QFLST:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }
    /* +QFLST: "<name>",<size> */
    int sz = 0;
    if (ec200_at_parse_int_field(resp, 1U, &sz) != EC200_OK || sz < 0) {
        return EC200_ERR_PARSE;
    }
    *size = (uint32_t)sz;
    return EC200_OK;
}

ec200_status_t ec200_file_exists(ec200_handle_t *h, const char *name,
                                 bool *exists)
{
    if (!name || name[0] == '\0' || !exists ||
        strlen(name) >= EC200_MAX_FILENAME_LEN) {
        return EC200_ERR_PARAM;
    }
    uint32_t size = 0;
    ec200_status_t st = qflst_size(h, name, &size);
    if (st == EC200_OK) {
        *exists = true;
        return EC200_OK;
    }
    if (st == EC200_ERR_CME || st == EC200_ERR_MODULE ||
        st == EC200_ERR_PARSE) {
        /* No such file: the module answers +CME ERROR / no +QFLST line. */
        *exists = false;
        return EC200_OK;
    }
    return st;
}

ec200_status_t ec200_file_size(ec200_handle_t *h, const char *name,
                               uint32_t *size)
{
    if (!name || name[0] == '\0' || !size ||
        strlen(name) >= EC200_MAX_FILENAME_LEN) {
        return EC200_ERR_PARAM;
    }
    return qflst_size(h, name, size);
}

ec200_status_t ec200_file_storage(ec200_handle_t *h,
                                  ec200_file_storage_t *st_out)
{
    if (!st_out) {
        return EC200_ERR_PARAM;
    }
    char resp[96];
    ec200_status_t st = ec200_at_send_wait(h, "AT+QFLDS=\"UFS\"", "+QFLDS:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }
    /* +QFLDS: <freesize>,<total_size> */
    int freeb = 0, total = 0;
    if (ec200_at_parse_int_field(resp, 0U, &freeb) != EC200_OK ||
        ec200_at_parse_int_field(resp, 1U, &total) != EC200_OK ||
        freeb < 0 || total < 0) {
        return EC200_ERR_PARSE;
    }
    st_out->free_bytes  = (uint32_t)freeb;
    st_out->total_bytes = (uint32_t)total;
    return EC200_OK;
}
