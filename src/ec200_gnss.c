/**
 * @file ec200_gnss.c
 * @brief GNSS/GPS implementation (AT+QGPS*).
 */

#include "ec200_gnss.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

ec200_status_t ec200_gnss_start(ec200_handle_t *h)
{
    return ec200_at_send(h, "AT+QGPS=1", NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_gnss_stop(ec200_handle_t *h)
{
    return ec200_at_send(h, "AT+QGPSEND", NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_gnss_get_status(ec200_handle_t *h, bool *enabled)
{
    if (!enabled) return EC200_ERR_PARAM;

    char resp[32];
    ec200_status_t st = ec200_at_send_wait(h, "AT+QGPS?", "+QGPS:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    int state = 0;
    if (ec200_at_parse_int_field(resp, 0U, &state) != EC200_OK) {
        return EC200_ERR_PARSE;
    }
    *enabled = (state == 1);
    return EC200_OK;
}

ec200_status_t ec200_gnss_get_location(ec200_handle_t       *h,
                                       ec200_gnss_location_t *loc)
{
    if (!loc) return EC200_ERR_PARAM;
    memset(loc, 0, sizeof(*loc));

    char resp[256];
    /* AT+QGPSLOC=2 requests decimal degrees directly */
    ec200_status_t st = ec200_at_send_wait(h, "AT+QGPSLOC=2", "+QGPSLOC:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /*
     * +QGPSLOC: <UTC>,<lat>,<lon>,<hdop>,<alt>,<fix_stat>,
     *           <cog>,<spkm>,<spkn>,<date>,<nsat>
     */
    const char *p = strchr(resp, ':');
    if (!p) return EC200_ERR_PARSE; /* GCOVR_EXCL_BR_LINE: prefix has ':' */
    p++; /* skip ':' */
    while (*p == ' ') p++;

    char utc[24]  = {0};
    float lat = 0.0f, lon = 0.0f, hdop = 0.0f, alt = 0.0f;
    float cog = 0.0f, spkm = 0.0f;
    int   fix_stat = 0, nsat = 0;
    char  date[12] = {0};

    /* Parse comma-separated fields */
    int fields = sscanf(p,
        "%23[^,],%f,%f,%f,%f,%d,%f,%f,%*f,%11[^,],%d",
        utc, &lat, &lon, &hdop, &alt, &fix_stat, &cog, &spkm, date, &nsat);

    if (fields < 2) return EC200_ERR_PARSE;

    /* AT+QGPSLOC=2 returns decimal degrees directly (no hemisphere char) */
    loc->latitude        = lat;
    loc->longitude       = lon;
    loc->altitude        = alt;
    loc->speed_kmh       = spkm;
    loc->course          = cog;
    loc->hdop            = (uint8_t)(hdop * 10.0f); /* store as tenths */
    loc->satellites_used = (uint8_t)nsat;
    loc->fix_valid       = (fix_stat > 0);

    /* Build a UTC timestamp string: combine date + utc */
    snprintf(loc->utc_time, sizeof(loc->utc_time), "%s %s", date, utc);

    return EC200_OK;
}

ec200_status_t ec200_gnss_set_nmea_output(ec200_handle_t *h,
                                          uint8_t         nmea_types)
{
    /* AT+QGPSCFG="nmeasrc",<enable> -- enable NMEA URC output */
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+QGPSCFG=\"nmeasrc\",%u",
             (nmea_types != 0) ? 1U : 0U);
    ec200_status_t st = ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /* Configure specific sentence types via AT+QGPSCFG="gpsnmeatype",<mask> */
    snprintf(cmd, sizeof(cmd), "AT+QGPSCFG=\"gpsnmeatype\",%u",
             (unsigned)nmea_types);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}
