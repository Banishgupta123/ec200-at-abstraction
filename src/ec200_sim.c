/**
 * @file ec200_sim.c
 * @brief SIM card management implementation.
 */

#include "ec200_sim.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>

ec200_status_t ec200_sim_get_status(ec200_handle_t     *h,
                                    ec200_sim_status_t *status)
{
    if (!status) return EC200_ERR_PARAM;

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CPIN?", "+CPIN:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /* resp: "+CPIN: READY" or "+CPIN: SIM PIN" etc. */
    const char *val = resp + 7; /* skip "+CPIN: " */
    while (*val == ' ') val++;

    if (strncmp(val, "READY",           5) == 0) { *status = EC200_SIM_READY;         }
    else if (strncmp(val, "SIM PIN",    7) == 0) { *status = EC200_SIM_PIN_REQUIRED;  }
    else if (strncmp(val, "SIM PUK",    7) == 0) { *status = EC200_SIM_PUK_REQUIRED;  }
    else if (strncmp(val, "SIM PIN2",   8) == 0) { *status = EC200_SIM_PIN2_REQUIRED; }
    else if (strncmp(val, "SIM PUK2",   8) == 0) { *status = EC200_SIM_PUK2_REQUIRED; }
    else if (strncmp(val, "NOT INSERT", 10)== 0) { *status = EC200_SIM_NOT_INSERTED;  }
    else                                          { *status = EC200_SIM_UNKNOWN;       }

    return EC200_OK;
}

ec200_status_t ec200_sim_enter_pin(ec200_handle_t *h, const char *pin)
{
    if (!pin) return EC200_ERR_PARAM;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CPIN=\"%s\"", pin);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_sim_get_imsi(ec200_handle_t *h,
                                  char           *imsi,
                                  size_t          imsi_sz)
{
    if (!imsi || imsi_sz == 0) return EC200_ERR_PARAM;

    char resp[64];
    ec200_status_t st = ec200_at_send(h, "AT+CIMI",
                                      resp, sizeof(resp),
                                      EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    strncpy(imsi, resp, imsi_sz - 1U);
    imsi[imsi_sz - 1U] = '\0';
    return EC200_OK;
}

ec200_status_t ec200_sim_get_iccid(ec200_handle_t *h,
                                   char           *iccid,
                                   size_t          iccid_sz)
{
    if (!iccid || iccid_sz == 0) return EC200_ERR_PARAM;

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CCID", "+ICCID:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        /* Some firmware returns the ICCID without prefix, try plain send */
        st = ec200_at_send(h, "AT+CCID", resp, sizeof(resp),
                           EC200_AT_TIMEOUT_DEFAULT);
        if (st != EC200_OK) return st;
    } else {
        /* Strip "+ICCID: " prefix */
        const char *val = resp + 7;
        while (*val == ' ') val++;
        strncpy(iccid, val, iccid_sz - 1U);
        iccid[iccid_sz - 1U] = '\0';
        return EC200_OK;
    }

    strncpy(iccid, resp, iccid_sz - 1U);
    iccid[iccid_sz - 1U] = '\0';
    return EC200_OK;
}
