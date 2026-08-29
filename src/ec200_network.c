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
 * Internal helpers
 * ------------------------------------------------------------------------- */

/**
 * @brief Parse the registration status from a +CREG / +CGREG / +CEREG line.
 *
 * Read-command responses have the shape `+CxREG: <n>,<stat>[,<tac>,<ci>,
 * <AcT>]` — the status is the field after the FIRST comma (never the last:
 * with URC mode 2 the trailing fields would otherwise be mistaken for it).
 * The URC form `+CxREG: <stat>` has no comma; then the first field is used.
 */
static ec200_status_t parse_reg_status(const char        *line,
                                       ec200_reg_status_t *status)
{
    /* The upstream prefix match guarantees a ':' is present. */
    const char *p = strchr(line, ':');
    p = (p != NULL) ? (p + 1) : line; /* GCOVR_EXCL_BR_LINE */
    while (*p == ' ') {
        p++;
    }

    const char *comma = strchr(p, ',');
    const char *field = (comma != NULL) ? (comma + 1) : p;

    if (*field < '0' || *field > '9') {
        return EC200_ERR_PARSE;
    }
    /* field starts with a digit, so atoi() cannot return a negative. */
    int stat = atoi(field);
    if (stat > (int)EC200_REG_REGISTERED_ROAMING) {
        return EC200_ERR_PARSE;
    }
    *status = (ec200_reg_status_t)stat;
    return EC200_OK;
}

/** Shared implementation of the three registration queries. */
static ec200_status_t get_reg(ec200_handle_t     *h,
                              const char         *cmd,
                              const char         *prefix,
                              ec200_reg_status_t *status)
{
    if (!status) {
        return EC200_ERR_PARAM;
    }
    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, cmd, prefix,
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }
    return parse_reg_status(resp, status);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

ec200_status_t ec200_net_get_creg(ec200_handle_t     *h,
                                  ec200_reg_status_t *status)
{
    return get_reg(h, "AT+CREG?", "+CREG:", status);
}

ec200_status_t ec200_net_get_cgreg(ec200_handle_t     *h,
                                   ec200_reg_status_t *status)
{
    return get_reg(h, "AT+CGREG?", "+CGREG:", status);
}

ec200_status_t ec200_net_get_cereg(ec200_handle_t     *h,
                                   ec200_reg_status_t *status)
{
    return get_reg(h, "AT+CEREG?", "+CEREG:", status);
}

ec200_status_t ec200_net_get_signal(ec200_handle_t        *h,
                                    ec200_signal_quality_t *sq)
{
    if (!sq) {
        return EC200_ERR_PARAM;
    }

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CSQ", "+CSQ:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    /* +CSQ: <rssi>,<ber>  (the prefix match guarantees the ':') */
    int rssi_raw = 0, ber = 0;
    const char *p = strchr(resp, ':');
    p = (p != NULL) ? (p + 1) : resp; /* GCOVR_EXCL_BR_LINE */
    if (sscanf(p, " %d,%d", &rssi_raw, &ber) != 2) {
        return EC200_ERR_PARSE;
    }

    /* Convert CSQ value (0-31, 99) to dBm */
    if (rssi_raw == 99 || rssi_raw < 0 || rssi_raw > 31) {
        sq->rssi = 0;
    } else {
        sq->rssi = (int16_t)(-113 + rssi_raw * 2);
    }
    sq->ber  = (uint8_t)ber;
    sq->rsrp = EC200_SIGNAL_UNKNOWN; /* not available via AT+CSQ */
    sq->sinr = EC200_SIGNAL_UNKNOWN;
    return EC200_OK;
}

ec200_status_t ec200_net_get_signal_ext(ec200_handle_t        *h,
                                        ec200_signal_quality_t *sq)
{
    if (!sq) {
        return EC200_ERR_PARAM;
    }

    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, "AT+QCSQ", "+QCSQ:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        /* Fall back to basic CSQ */
        return ec200_net_get_signal(h, sq);
    }

    /* +QCSQ: "<sysmode>",<rssi>,<rsrp>,<sinr>,<rsrq> — values are dBm and
     * therefore negative; do NOT reject them for being < 0. */
    const char *p = strchr(resp, ',');
    if (!p) {
        return EC200_ERR_PARSE;
    }

    int rssi_raw = 0, rsrp = 0, sinr = 0;
    int fields = sscanf(p + 1, "%d,%d,%d", &rssi_raw, &rsrp, &sinr);
    if (fields < 1) {
        return EC200_ERR_PARSE;
    }
    sq->rssi = (int16_t)rssi_raw;
    sq->rsrp = (fields >= 2) ? (int16_t)rsrp : EC200_SIGNAL_UNKNOWN;
    sq->sinr = (fields >= 3) ? (int16_t)sinr : EC200_SIGNAL_UNKNOWN;
    sq->ber  = 99U; /* not reported by AT+QCSQ */
    return EC200_OK;
}

ec200_status_t ec200_net_get_operator(ec200_handle_t       *h,
                                      ec200_operator_info_t *info)
{
    if (!info) {
        return EC200_ERR_PARAM;
    }

    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, "AT+COPS?", "+COPS:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    /* +COPS: <mode>[,<format>,<oper>[,<AcT>]]  (prefix guarantees ':') */
    const char *p = strchr(resp, ':');
    p = (p != NULL) ? (p + 1) : resp; /* GCOVR_EXCL_BR_LINE */
    while (*p == ' ') {
        p++;
    }

    int mode = 0, fmt = 0, act = (int)EC200_ACT_UNKNOWN;
    char oper[EC200_MAX_OPERATOR_LEN] = {0};

    int parsed = sscanf(p, "%d,%d,\"%31[^\"]\",%d",
                        &mode, &fmt, oper, &act);
    if (parsed < 1) {
        return EC200_ERR_PARSE;
    }

    info->mode   = (ec200_cops_mode_t)mode;
    info->format = (ec200_cops_fmt_t)fmt;
    info->act    = (ec200_act_t)act;
    /* sscanf caps oper at 31 chars, which always fits the 32-byte field. */
    memcpy(info->oper, oper, strlen(oper) + 1U);
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
        (void)snprintf(cmd, sizeof(cmd), "AT+COPS=%d", (int)mode);
    } else if (oper && oper[0] != '\0') {
        (void)snprintf(cmd, sizeof(cmd), "AT+COPS=%d,%d,\"%s\",%d",
                       (int)mode, (int)format, oper, (int)act);
    } else {
        (void)snprintf(cmd, sizeof(cmd), "AT+COPS=%d,%d",
                       (int)mode, (int)format);
    }
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_COPS);
}

ec200_status_t ec200_net_wait_registered(ec200_handle_t *h,
                                         uint32_t        timeout_ms)
{
    if (!h) {
        return EC200_ERR_PARAM;
    }

    uint32_t elapsed = 0;
    const uint32_t poll_interval = 2000U;

    for (;;) {
        ec200_reg_status_t st = EC200_REG_UNKNOWN;
        if (ec200_net_get_cereg(h, &st) == EC200_OK) {
            if (st == EC200_REG_REGISTERED_HOME ||
                st == EC200_REG_REGISTERED_ROAMING) {
                return EC200_OK;
            }
        }
        if (elapsed >= timeout_ms) {
            return EC200_ERR_TIMEOUT;
        }
        h->delay_ms(poll_interval, h->user_ctx);
        elapsed += poll_interval;
    }
}
