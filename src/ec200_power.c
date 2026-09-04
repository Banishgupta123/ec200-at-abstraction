/**
 * @file ec200_power.c
 * @brief Power management implementation (AT+CFUN, AT+QPOWD, AT+QSCLK).
 */

#include "ec200_power.h"
#include "ec200_at.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

ec200_status_t ec200_power_set_cfun(ec200_handle_t *h,
                                    ec200_cfun_t    level,
                                    bool            reset)
{
    char cmd[24];
    if (reset) {
        (void)snprintf(cmd, sizeof(cmd), "AT+CFUN=%d,1", (int)level);
    } else {
        (void)snprintf(cmd, sizeof(cmd), "AT+CFUN=%d", (int)level);
    }
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_LONG);
}

ec200_status_t ec200_power_get_cfun(ec200_handle_t *h, ec200_cfun_t *level)
{
    if (!level) return EC200_ERR_PARAM;

    char resp[32];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CFUN?", "+CFUN:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    int val = 0;
    if (ec200_at_parse_int_field(resp, 0U, &val) != EC200_OK) {
        return EC200_ERR_PARSE;
    }
    *level = (ec200_cfun_t)val;
    return EC200_OK;
}

ec200_status_t ec200_power_down(ec200_handle_t *h, bool normal)
{
    char cmd[24];
    (void)snprintf(cmd, sizeof(cmd), "AT+QPOWD=%d", normal ? 1 : 0);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_LONG);
}

ec200_status_t ec200_power_set_sleep(ec200_handle_t *h, bool enable)
{
    char cmd[24];
    (void)snprintf(cmd, sizeof(cmd), "AT+QSCLK=%d", enable ? 1 : 0);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_power_reset(ec200_handle_t *h)
{
    return ec200_power_set_cfun(h, EC200_CFUN_FULL, true);
}

/* =========================================================================
 * Low power: 3GPP timer encoding
 * ========================================================================= */

/*
 * A 3GPP 24.008 timer octet is three unit bits then a five-bit multiplier.
 * Unit code 111 means "deactivated" in both tables, so it never appears
 * here; it is emitted directly for a zero duration.
 *
 * Ordered coarsest-first so the encoder prefers the unit that keeps the
 * multiplier smallest, which is what the tables intend.
 */
#define PSM_UNIT_DEACTIVATED  (7U)
#define PSM_MAX_MULTIPLIER    (31U)

typedef struct {
    uint8_t  code;     /**< The three unit bits          */
    uint32_t seconds;  /**< Seconds one multiplier step represents */
} psm_unit_t;

/* T3412 extended (periodic TAU), 3GPP 24.008 10.5.7.4a. */
static const psm_unit_t tau_units[] = {
    { 6U, 320U * 3600U },  /* 320 hours */
    { 2U,  10U * 3600U },  /*  10 hours */
    { 1U,        3600U },  /*   1 hour  */
    { 0U,         600U },  /*  10 min   */
    { 5U,          60U },  /*   1 min   */
    { 4U,          30U },  /*  30 s     */
    { 3U,           2U },  /*   2 s     */
};

/* T3324 (active time), 3GPP 24.008 10.5.7.3. */
static const psm_unit_t active_units[] = {
    { 2U, 6U * 60U },  /* 1 decihour = 6 min */
    { 1U,     60U },   /* 1 minute           */
    { 0U,      2U },   /* 2 seconds          */
};

/* Render unit+multiplier as the eight '0'/'1' characters the module wants. */
static void psm_write_bits(uint8_t unit, uint8_t mult, char *out)
{
    uint8_t octet = (uint8_t)(((unit & 0x07U) << 5) | (mult & 0x1FU));
    for (unsigned i = 0; i < 8U; i++) {
        out[i] = ((octet & (uint8_t)(0x80U >> i)) != 0U) ? '1' : '0';
    }
    out[8] = '\0';
}

static ec200_status_t psm_encode(uint32_t          seconds,
                                 const psm_unit_t *units,
                                 size_t            n_units,
                                 char             *out,
                                 size_t            out_sz)
{
    if (out == NULL || out_sz < EC200_PSM_TIMER_STR_LEN) {
        return EC200_ERR_PARAM;
    }
    if (seconds == 0U) {
        psm_write_bits(PSM_UNIT_DEACTIVATED, 0U, out);
        return EC200_OK;
    }

    for (size_t i = 0; i < n_units; i++) {
        uint32_t step = units[i].seconds;
        if ((seconds % step) != 0U) {
            continue;                     /* not representable in this unit */
        }
        /* seconds is non-zero and divides exactly, so mult is at least 1;
         * only the upper bound needs checking. */
        uint32_t mult = seconds / step;
        if (mult <= PSM_MAX_MULTIPLIER) {
            psm_write_bits(units[i].code, (uint8_t)mult, out);
            return EC200_OK;
        }
    }
    /* Refuse rather than round: a silently shortened wake interval is a
     * power bug the caller would never see. */
    return EC200_ERR_PARAM;
}

ec200_status_t ec200_psm_encode_tau(uint32_t seconds, char *out, size_t out_sz)
{
    return psm_encode(seconds, tau_units,
                      sizeof(tau_units) / sizeof(tau_units[0]), out, out_sz);
}

ec200_status_t ec200_psm_encode_active_time(uint32_t seconds,
                                            char    *out,
                                            size_t   out_sz)
{
    return psm_encode(seconds, active_units,
                      sizeof(active_units) / sizeof(active_units[0]),
                      out, out_sz);
}

ec200_status_t ec200_psm_decode_timer(const char *str,
                                      bool        is_tau,
                                      uint32_t   *seconds_out)
{
    if (str == NULL || seconds_out == NULL) {
        return EC200_ERR_PARAM;
    }

    uint8_t octet = 0U;
    for (unsigned i = 0; i < 8U; i++) {
        if (str[i] != '0' && str[i] != '1') {
            return EC200_ERR_PARSE;
        }
        octet = (uint8_t)((octet << 1) | (uint8_t)(str[i] - '0'));
    }
    if (str[8] != '\0') {
        return EC200_ERR_PARSE;
    }

    uint8_t unit = (uint8_t)((octet >> 5) & 0x07U);
    uint8_t mult = (uint8_t)(octet & 0x1FU);

    if (unit == PSM_UNIT_DEACTIVATED) {
        *seconds_out = 0U;
        return EC200_OK;
    }

    const psm_unit_t *units   = is_tau ? tau_units : active_units;
    size_t            n_units = is_tau
                              ? sizeof(tau_units) / sizeof(tau_units[0])
                              : sizeof(active_units) / sizeof(active_units[0]);
    for (size_t i = 0; i < n_units; i++) {
        if (units[i].code == unit) {
            *seconds_out = units[i].seconds * mult;
            return EC200_OK;
        }
    }
    return EC200_ERR_PARSE;  /* reserved unit code for this timer */
}

/* =========================================================================
 * Power Saving Mode (AT+CPSMS)
 * ========================================================================= */

/*
 * Copy the index-th comma-separated field into @p out, without its quotes.
 * An absent or empty field yields an empty string — which is exactly what
 * the module sends for a timer it has not been given.
 *
 * Plain comma counting is enough here: every value these commands return is
 * a bit string, so a comma can never appear inside one and there is no need
 * to track quoting. Quotes are optional in the reply, so both forms are
 * accepted. Over-long values are truncated rather than rejected; the caller
 * buffers are sized from the spec, so this can only bite on a firmware that
 * invents a longer field, and a short read beats overrunning.
 */
static void copy_field(const char *line,
                       unsigned    index,
                       char       *out,
                       size_t      out_sz)
{
    out[0] = '\0';

    /* Callers only pass lines that already matched a "+PREFIX:". */
    const char *p = strchr(line, ':');
    p = (p != NULL) ? (p + 1) : line; /* GCOVR_EXCL_BR_LINE */

    for (unsigned field = 0U; field < index; field++) {
        p = strchr(p, ',');
        if (p == NULL) {
            return; /* the module omitted this field and everything after */
        }
        p++;
    }
    while (*p == ' ') {
        p++;
    }
    if (*p == '"') {
        p++;
    }

    size_t n = 0;
    while (*p != '\0' && *p != '"' && *p != ',' && n + 1U < out_sz) {
        out[n] = *p;
        n++;
        p++;
    }
    out[n] = '\0';
}

ec200_status_t ec200_psm_set(ec200_handle_t           *h,
                             const ec200_psm_config_t *cfg)
{
    if (cfg == NULL) {
        return EC200_ERR_PARAM;
    }
    if (!cfg->enabled) {
        return ec200_psm_disable(h);
    }
    /* Both timers must be full 8-bit strings; a short one would be sent
     * unquoted-but-malformed and the module would reject the whole line. */
    if (strlen(cfg->periodic_tau) != 8U || strlen(cfg->active_time) != 8U) {
        return EC200_ERR_PARAM;
    }

    char cmd[64];
    (void)snprintf(cmd, sizeof(cmd), "AT+CPSMS=1,,,\"%s\",\"%s\"",
                   cfg->periodic_tau, cfg->active_time);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_psm_get(ec200_handle_t *h, ec200_psm_config_t *cfg)
{
    if (cfg == NULL) {
        return EC200_ERR_PARAM;
    }
    memset(cfg, 0, sizeof(*cfg));

    char resp[96];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CPSMS?", "+CPSMS:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    int mode = 0;
    if (ec200_at_parse_int_field(resp, 0U, &mode) != EC200_OK) {
        return EC200_ERR_PARSE;
    }
    cfg->enabled = (mode != 0);

    /* +CPSMS: <mode>,[<RAU>],[<GPRS-READY>],[<TAU>],[<Active>] */
    copy_field(resp, 3U, cfg->periodic_tau, sizeof(cfg->periodic_tau));
    copy_field(resp, 4U, cfg->active_time,  sizeof(cfg->active_time));
    return EC200_OK;
}

ec200_status_t ec200_psm_disable(ec200_handle_t *h)
{
    return ec200_at_send(h, "AT+CPSMS=0", NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

/* =========================================================================
 * Extended DRX (AT+CEDRXS / AT+CEDRXRDP)
 * ========================================================================= */

static bool edrx_act_valid(ec200_edrx_act_t act)
{
    return act == EC200_EDRX_ACT_GSM ||
           act == EC200_EDRX_ACT_UTRAN ||
           act == EC200_EDRX_ACT_LTE_CAT_M1 ||
           act == EC200_EDRX_ACT_LTE_NB_S1;
}

ec200_status_t ec200_edrx_set(ec200_handle_t            *h,
                              const ec200_edrx_config_t *cfg)
{
    if (cfg == NULL || !edrx_act_valid(cfg->act_type)) {
        return EC200_ERR_PARAM;
    }
    if (!cfg->enabled) {
        return ec200_edrx_disable(h, cfg->act_type);
    }
    if (strlen(cfg->requested) != 4U) {
        return EC200_ERR_PARAM;
    }

    char cmd[48];
    (void)snprintf(cmd, sizeof(cmd), "AT+CEDRXS=1,%d,\"%s\"",
                   (int)cfg->act_type, cfg->requested);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_edrx_get(ec200_handle_t *h, ec200_edrx_config_t *cfg)
{
    if (cfg == NULL) {
        return EC200_ERR_PARAM;
    }
    memset(cfg, 0, sizeof(*cfg));

    char resp[96];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CEDRXS?", "+CEDRXS:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    /*
     * Observed on this firmware:  +CEDRXS: <mode>,<AcT-type>,"<requested>"
     *
     * 3GPP 27.007 documents the read form *without* the leading <mode>, but
     * the module sends it. Taking the AcT-type from field 0 therefore reads
     * the mode instead, and does so silently — both are small integers, so
     * nothing looks wrong until the values are printed.
     */
    int mode = 0;
    if (ec200_at_parse_int_field(resp, 0U, &mode) != EC200_OK) {
        return EC200_ERR_PARSE;
    }
    cfg->enabled = (mode != 0);

    /* Technology and value are absent until eDRX has been configured once;
     * that is not an error, there is simply nothing to report. */
    int act = 0;
    if (ec200_at_parse_int_field(resp, 1U, &act) == EC200_OK) {
        cfg->act_type = (ec200_edrx_act_t)act;
        copy_field(resp, 2U, cfg->requested, sizeof(cfg->requested));
    }
    return EC200_OK;
}

ec200_status_t ec200_edrx_disable(ec200_handle_t   *h,
                                  ec200_edrx_act_t  act_type)
{
    if (!edrx_act_valid(act_type)) {
        return EC200_ERR_PARAM;
    }
    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+CEDRXS=0,%d", (int)act_type);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_edrx_get_dynamic(ec200_handle_t       *h,
                                      ec200_edrx_dynamic_t *out)
{
    if (out == NULL) {
        return EC200_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));

    char resp[96];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CEDRXRDP", "+CEDRXRDP:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    int act = 0;
    if (ec200_at_parse_int_field(resp, 0U, &act) != EC200_OK) {
        return EC200_ERR_PARSE;
    }
    out->act_type = (ec200_edrx_act_t)act;

    /* Trailing fields are omitted when eDRX is not in use on this cell. */
    copy_field(resp, 1U, out->requested, sizeof(out->requested));
    copy_field(resp, 2U, out->granted,   sizeof(out->granted));
    copy_field(resp, 3U, out->paging_time_window,
                      sizeof(out->paging_time_window));
    return EC200_OK;
}
