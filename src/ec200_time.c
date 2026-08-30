/**
 * @file ec200_time.c
 * @brief Module clock, network time, and NTP synchronisation.
 */

#include "ec200_time.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Parse a "yy/MM/dd,hh:mm:ss+zz" timestamp into @p dt.
 *
 * Used by both AT+CCLK? and AT+QLTS responses.
 */
static ec200_status_t parse_timestamp(const char *s, ec200_datetime_t *dt)
{
    const char *q = strchr(s, '"');
    if (q != NULL) {
        s = q + 1;
    }

    int yy = 0, mo = 0, dd = 0, hh = 0, mi = 0, ss = 0, tz = 0;
    /* The zone sign is part of the field, so %d picks it up. */
    if (sscanf(s, "%d/%d/%d,%d:%d:%d%d",
               &yy, &mo, &dd, &hh, &mi, &ss, &tz) < 6) {
        return EC200_ERR_PARSE;
    }
    if (mo < 1 || mo > 12 || dd < 1 || dd > 31 ||
        hh < 0 || hh > 23 || mi < 0 || mi > 59 || ss < 0 || ss > 60) {
        return EC200_ERR_PARSE;
    }

    dt->year        = (uint16_t)(2000 + yy);
    dt->month       = (uint8_t)mo;
    dt->day         = (uint8_t)dd;
    dt->hour        = (uint8_t)hh;
    dt->minute      = (uint8_t)mi;
    dt->second      = (uint8_t)ss;
    dt->tz_quarters = (int8_t)tz;
    return EC200_OK;
}

ec200_status_t ec200_time_get(ec200_handle_t *h, ec200_datetime_t *dt)
{
    if (!dt) {
        return EC200_ERR_PARAM;
    }
    memset(dt, 0, sizeof(*dt));

    char resp[80];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CCLK?", "+CCLK:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }
    const char *p = strchr(resp, ':');
    p = (p != NULL) ? (p + 1) : resp; /* GCOVR_EXCL_BR_LINE */
    return parse_timestamp(p, dt);
}

ec200_status_t ec200_time_set(ec200_handle_t *h, const ec200_datetime_t *dt)
{
    if (!dt || dt->year < 2000U || dt->year > 2099U ||
        dt->month < 1U || dt->month > 12U ||
        dt->day < 1U || dt->day > 31U ||
        dt->hour > 23U || dt->minute > 59U || dt->second > 59U) {
        return EC200_ERR_PARAM;
    }

    char cmd[64];
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+CCLK=\"%02u/%02u/%02u,%02u:%02u:%02u%+03d\"",
                   (unsigned)(dt->year - 2000U), (unsigned)dt->month,
                   (unsigned)dt->day, (unsigned)dt->hour,
                   (unsigned)dt->minute, (unsigned)dt->second,
                   (int)dt->tz_quarters);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_time_set_auto_update(ec200_handle_t *h, bool enable)
{
    char cmd[24];
    (void)snprintf(cmd, sizeof(cmd), "AT+CTZU=%d", enable ? 1 : 0);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_time_get_network(ec200_handle_t *h,
                                      ec200_datetime_t *dt)
{
    if (!dt) {
        return EC200_ERR_PARAM;
    }
    memset(dt, 0, sizeof(*dt));

    char resp[96];
    ec200_status_t st = ec200_at_send_wait(h, "AT+QLTS=2", "+QLTS:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }
    const char *p = strchr(resp, ':');
    p = (p != NULL) ? (p + 1) : resp; /* GCOVR_EXCL_BR_LINE */
    return parse_timestamp(p, dt);
}

ec200_status_t ec200_time_sync_ntp(ec200_handle_t *h,
                                   uint8_t         pdp_ctx,
                                   const char     *server,
                                   uint16_t        port,
                                   uint32_t        timeout_ms)
{
    if (!server || server[0] == '\0' || pdp_ctx < 1U || pdp_ctx > 16U) {
        return EC200_ERR_PARAM;
    }

    /* Reject over-long names: they would silently truncate into a
     * malformed AT command. */
    if (strlen(server) >= EC200_MAX_URL_LEN) {
        return EC200_ERR_PARAM;
    }

    char cmd[EC200_MAX_URL_LEN + 32];
    (void)snprintf(cmd, sizeof(cmd), "AT+QNTP=%u,\"%s\",%u",
                   (unsigned)pdp_ctx, server,
                   (unsigned)((port != 0U) ? port : 123U));

    /* "OK" first, then "+QNTP: <err>[,\"<time>\"]". */
    char resp[96];
    ec200_status_t st = ec200_at_send_await_urc(h, cmd, "+QNTP:",
                                                resp, sizeof(resp),
                                                EC200_AT_TIMEOUT_DEFAULT,
                                                timeout_ms);
    if (st != EC200_OK) {
        return st;
    }
    int err = 0;
    if (ec200_at_parse_int_field(resp, 0U, &err) != EC200_OK) {
        return EC200_ERR_PARSE;
    }
    return (err == 0) ? EC200_OK : EC200_ERR_MODULE;
}
