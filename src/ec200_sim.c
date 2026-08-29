/**
 * @file ec200_sim.c
 * @brief SIM card management implementation.
 */

#include "ec200_sim.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>

/** Skip the "+PREFIX: " header of a response line; returns the value part. */
static const char *resp_value(const char *resp)
{
    /* The upstream prefix match guarantees a ':' is present. */
    const char *p = strchr(resp, ':');
    p = (p != NULL) ? (p + 1) : resp; /* GCOVR_EXCL_BR_LINE */
    while (*p == ' ') {
        p++;
    }
    return p;
}

ec200_status_t ec200_sim_get_status(ec200_handle_t     *h,
                                    ec200_sim_status_t *status)
{
    if (!status) {
        return EC200_ERR_PARAM;
    }

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CPIN?", "+CPIN:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    const char *val = resp_value(resp);

    /* Longest prefixes first: "SIM PIN2" must win over "SIM PIN". */
    if      (strncmp(val, "READY",      5) == 0) { *status = EC200_SIM_READY;         }
    else if (strncmp(val, "SIM PIN2",   8) == 0) { *status = EC200_SIM_PIN2_REQUIRED; }
    else if (strncmp(val, "SIM PUK2",   8) == 0) { *status = EC200_SIM_PUK2_REQUIRED; }
    else if (strncmp(val, "SIM PIN",    7) == 0) { *status = EC200_SIM_PIN_REQUIRED;  }
    else if (strncmp(val, "SIM PUK",    7) == 0) { *status = EC200_SIM_PUK_REQUIRED;  }
    else if (strncmp(val, "NOT INSERT",10) == 0) { *status = EC200_SIM_NOT_INSERTED;  }
    else                                          { *status = EC200_SIM_UNKNOWN;       }

    return EC200_OK;
}

ec200_status_t ec200_sim_enter_pin(ec200_handle_t *h, const char *pin)
{
    if (!pin || pin[0] == '\0' || strlen(pin) > 8U) {
        return EC200_ERR_PARAM;
    }

    char cmd[32];
    (void)snprintf(cmd, sizeof(cmd), "AT+CPIN=\"%s\"", pin);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_sim_get_imsi(ec200_handle_t *h,
                                  char           *imsi,
                                  size_t          imsi_sz)
{
    if (!imsi || imsi_sz == 0) {
        return EC200_ERR_PARAM;
    }

    char resp[64];
    ec200_status_t st = ec200_at_send(h, "AT+CIMI",
                                      resp, sizeof(resp),
                                      EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    /* Body is a single line holding the IMSI digits. */
    char *nl = strchr(resp, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
    size_t len = strlen(resp);
    if (len == 0U) {
        return EC200_ERR_PARSE;
    }
    if (len >= imsi_sz) {
        return EC200_ERR_OVERFLOW;
    }
    memcpy(imsi, resp, len + 1U);
    return EC200_OK;
}

ec200_status_t ec200_sim_get_iccid(ec200_handle_t *h,
                                   char           *iccid,
                                   size_t          iccid_sz)
{
    if (!iccid || iccid_sz == 0) {
        return EC200_ERR_PARAM;
    }

    char resp[64];
    ec200_status_t st = ec200_at_send_wait(h, "AT+CCID", "+ICCID:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    const char *val;
    if (st == EC200_OK) {
        val = resp_value(resp);
    } else {
        /* Some firmware returns the ICCID without a prefix line. */
        st = ec200_at_send(h, "AT+CCID", resp, sizeof(resp),
                           EC200_AT_TIMEOUT_DEFAULT);
        if (st != EC200_OK) {
            return st;
        }
        char *nl = strchr(resp, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        val = resp;
    }

    size_t len = strlen(val);
    if (len == 0U) {
        return EC200_ERR_PARSE;
    }
    if (len >= iccid_sz) {
        return EC200_ERR_OVERFLOW;
    }
    memcpy(iccid, val, len + 1U);
    return EC200_OK;
}
