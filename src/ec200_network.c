/**
 * @file ec200_network.c
 * @brief Network registration and operator management implementation.
 */

#include "ec200_network.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Internal helper: parse registration status from +CREG/+CGREG/+CEREG line
 * e.g. "+CREG: 0,1" or "+CREG: 1"
 * ------------------------------------------------------------------------- */
static ec200_status_t parse_reg_status(const char       *line,
                                       ec200_reg_status_t *status)
{
    /* Find the last comma; if present, stat is the digit after it.
     * Otherwise stat is the digit after ": ". */
    const char *comma = strrchr(line, ',');
    const char *p;
    if (comma) {
        p = comma + 1;
    } else {
        p = strchr(line, ':');
        if (!p) return EC200_ERR_PARSE;
        p++;
        while (*p == ' ') p++;
    }
    *status = (ec200_reg_status_t)atoi(p);
    return EC200_OK;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_net_get_creg(ec200_handle_t     *h,
                                  ec200_reg_status_t *status)
{
    if (!status) return EC200_ERR_PARAM;
    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CREG?", "+CREG:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;
    return parse_reg_status(resp, status);
}

ec200_status_t ec200_net_get_cgreg(ec200_handle_t     *h,
                                   ec200_reg_status_t *status)
{
    if (!status) return EC200_ERR_PARAM;
    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CGREG?", "+CGREG:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;
    return parse_reg_status(resp, status);
}

ec200_status_t ec200_net_get_cereg(ec200_handle_t     *h,
                                   ec200_reg_status_t *status)
{
    if (!status) return EC200_ERR_PARAM;
    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CEREG?", "+CEREG:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;
    return parse_reg_status(resp, status);
}

ec200_status_t ec200_net_get_signal(ec200_handle_t        *h,
                                    ec200_signal_quality_t *sq)
{
    if (!sq) return EC200_ERR_PARAM;

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CSQ", "+CSQ:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /* +CSQ: <rssi>,<ber> */
    int rssi_raw = 0, ber = 0;
    const char *p = strchr(resp, ':');
    if (!p) return EC200_ERR_PARSE;
    if (sscanf(p + 1, " %d,%d", &rssi_raw, &ber) != 2) {
        return EC200_ERR_PARSE;
    }

    /* Convert CSQ value (0-31, 99) to dBm */
    if (rssi_raw == 99) {
        sq->rssi = 0;
    } else {
        sq->rssi = (int8_t)(-113 + rssi_raw * 2);
    }
    sq->ber  = (uint8_t)ber;
    sq->rsrp = 255; /* not available via AT+CSQ */
    sq->sinr = 0;
    return EC200_OK;
}

ec200_status_t ec200_net_get_signal_ext(ec200_handle_t        *h,
                                        ec200_signal_quality_t *sq)
{
    if (!sq) return EC200_ERR_PARAM;

    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, "AT+QCSQ", "+QCSQ:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        /* Fall back to basic CSQ */
        return ec200_net_get_signal(h, sq);
    }

    /* +QCSQ: "LTE",<rssi>,<rsrp>,<sinr>,<rsrq> */
    const char *p = strchr(resp, ',');
    if (!p) return EC200_ERR_PARSE;

    int rssi_raw = 0, rsrp = 0, sinr = 0;
    if (sscanf(p + 1, "%d,%d,%d", &rssi_raw, &rsrp, &sinr) < 1) {
        return EC200_ERR_PARSE;
    }
    sq->rssi = (int8_t)rssi_raw;
    sq->rsrp = (rsrp >= 0 && rsrp <= 255) ? (uint8_t)rsrp : 255U;
    sq->sinr = (int8_t)sinr;
    sq->ber  = 99;
    return EC200_OK;
}

ec200_status_t ec200_net_get_operator(ec200_handle_t       *h,
                                      ec200_operator_info_t *info)
{
    if (!info) return EC200_ERR_PARAM;

    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, "AT+COPS?", "+COPS:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /* +COPS: <mode>[,<format>,<oper>[,<AcT>]] */
    const char *p = strchr(resp, ':');
    if (!p) return EC200_ERR_PARSE;
    p++;
    while (*p == ' ') p++;

    int mode = 0, fmt = 0, act = EC200_ACT_UNKNOWN;
    char oper[EC200_MAX_OPERATOR_LEN] = {0};

    /* Mode only? */
    int parsed = sscanf(p, "%d,%d,\"%31[^\"]\",%d",
                        &mode, &fmt, oper, &act);
    if (parsed < 1) return EC200_ERR_PARSE;

    info->mode   = (ec200_cops_mode_t)mode;
    info->format = (ec200_cops_fmt_t)fmt;
    info->act    = (ec200_act_t)act;
    strncpy(info->oper, oper, sizeof(info->oper) - 1U);
    info->oper[sizeof(info->oper) - 1U] = '\0';
    return EC200_OK;
}

ec200_status_t ec200_net_set_operator(ec200_handle_t    *h,
                                      ec200_cops_mode_t  mode,
                                      ec200_cops_fmt_t   format,
                                      const char        *oper,
                                      ec200_act_t        act)
{
    char cmd[64];
    if (mode == EC200_COPS_MODE_AUTOMATIC) {
        snprintf(cmd, sizeof(cmd), "AT+COPS=%d", (int)mode);
    } else if (oper && oper[0] != '\0') {
        snprintf(cmd, sizeof(cmd), "AT+COPS=%d,%d,\"%s\",%d",
                 (int)mode, (int)format, oper, (int)act);
    } else {
        snprintf(cmd, sizeof(cmd), "AT+COPS=%d,%d", (int)mode, (int)format);
    }
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_COPS);
}

ec200_status_t ec200_net_wait_registered(ec200_handle_t *h,
                                         uint32_t        timeout_ms)
{
    if (!h) return EC200_ERR_PARAM;

    uint32_t elapsed = 0;
    const uint32_t poll_interval = 2000U;

    while (elapsed < timeout_ms) {
        ec200_reg_status_t st = EC200_REG_UNKNOWN;
        if (ec200_net_get_cereg(h, &st) == EC200_OK) {
            if (st == EC200_REG_REGISTERED_HOME ||
                st == EC200_REG_REGISTERED_ROAMING) {
                return EC200_OK;
            }
        }
        h->delay_ms(poll_interval, h->user_ctx);
        elapsed += poll_interval;
    }

    return EC200_ERR_TIMEOUT;
}
