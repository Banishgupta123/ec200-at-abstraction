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
        snprintf(cmd, sizeof(cmd), "AT+CFUN=%d,1", (int)level);
    } else {
        snprintf(cmd, sizeof(cmd), "AT+CFUN=%d", (int)level);
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

    const char *p = strchr(resp, ':');
    if (!p) return EC200_ERR_PARSE;
    *level = (ec200_cfun_t)atoi(p + 1);
    return EC200_OK;
}

ec200_status_t ec200_power_down(ec200_handle_t *h, bool normal)
{
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+QPOWD=%d", normal ? 1 : 0);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_LONG);
}

ec200_status_t ec200_power_set_sleep(ec200_handle_t *h, bool enable)
{
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+QSCLK=%d", enable ? 1 : 0);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_power_reset(ec200_handle_t *h)
{
    return ec200_power_set_cfun(h, EC200_CFUN_FULL, true);
}
