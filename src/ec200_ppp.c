/**
 * @file ec200_ppp.c
 * @brief PPP dial-up control plane implementation.
 */

#include "ec200_ppp.h"
#include "ec200_at.h"

#include <stdio.h>
#include <string.h>

ec200_status_t ec200_ppp_dial(ec200_handle_t *h, uint8_t cid)
{
    if (!h || cid < 1 || cid > 16) {
        return EC200_ERR_PARAM;
    }
    if (h->_ppp_data_mode) {
        return EC200_ERR_BUSY;
    }

    char cmd[24];
    (void)snprintf(cmd, sizeof(cmd), "ATD*99***%u#", (unsigned)cid);

    /* CONNECT is the handover point: send_expect stops consuming bytes the
     * moment the line is matched, so the PPP frames that follow are left
     * untouched for the host network stack. */
    char resp[32];
    ec200_status_t st = ec200_at_send_expect(h, cmd, "CONNECT",
                                             resp, sizeof(resp),
                                             EC200_AT_TIMEOUT_LONG);
    if (st != EC200_OK) {
        return st;
    }

    h->_ppp_data_mode = true;
    return EC200_OK;
}

ec200_status_t ec200_ppp_escape(ec200_handle_t *h)
{
    if (!h) {
        return EC200_ERR_PARAM;
    }
    if (!h->_ppp_data_mode) {
        /* "+++" typed in command mode would sit in the modem's command
         * buffer and corrupt the next command — refuse instead. */
        return EC200_ERR_PARAM;
    }

    h->delay_ms(EC200_PPP_ESCAPE_GUARD_MS, h->user_ctx);
    ec200_status_t st = ec200_at_write_raw(h, (const uint8_t *)"+++", 3);
    if (st != EC200_OK) {
        return st;
    }
    h->delay_ms(EC200_PPP_ESCAPE_GUARD_MS, h->user_ctx);

    /* Any residual PPP bytes are discarded while re-synchronising on OK. */
    st = ec200_at_wait_final(h, EC200_AT_TIMEOUT_DEFAULT);
    if (st == EC200_ERR_MODULE || st == EC200_ERR_CME ||
        st == EC200_ERR_CMS) {
        /* A terminal error answer - typically NO CARRIER because the
         * data call has already ended (the peer hung up, or no PPP
         * stack kept the link alive).  The module is in command mode
         * either way, so the session flag MUST be cleared: leaving it
         * set would make every later AT call return EC200_ERR_BUSY and
         * wedge the handle permanently.  The status still reports that
         * the session was lost rather than cleanly suspended. */
        h->_ppp_data_mode = false;
        return st;
    }
    if (st != EC200_OK) {
        /* Timeout or I/O error: the module's state is genuinely unknown,
         * so stay in data mode rather than guess. */
        return st;
    }

    h->_ppp_data_mode = false;
    return EC200_OK;
}

ec200_status_t ec200_ppp_resume(ec200_handle_t *h)
{
    if (!h) {
        return EC200_ERR_PARAM;
    }
    if (h->_ppp_data_mode) {
        return EC200_ERR_BUSY;
    }

    char resp[32];
    ec200_status_t st = ec200_at_send_expect(h, "ATO", "CONNECT",
                                             resp, sizeof(resp),
                                             EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) {
        return st;
    }

    h->_ppp_data_mode = true;
    return EC200_OK;
}

ec200_status_t ec200_ppp_hangup(ec200_handle_t *h)
{
    if (!h) {
        return EC200_ERR_PARAM;
    }
    if (h->_ppp_data_mode) {
        return EC200_ERR_PARAM; /* escape first */
    }
    return ec200_at_send(h, "ATH", NULL, 0, EC200_AT_TIMEOUT_LONG);
}

ec200_status_t ec200_ppp_disconnect(ec200_handle_t *h)
{
    if (!h) {
        return EC200_ERR_PARAM;
    }
    if (h->_ppp_data_mode) {
        ec200_status_t st = ec200_ppp_escape(h);
        if (st != EC200_OK && h->_ppp_data_mode) {
            return st; /* genuinely still in data mode - cannot hang up */
        }
        /* Otherwise the session is already gone (NO CARRIER) and we are
         * back in command mode: still issue ATH to tidy up. */
    }
    return ec200_ppp_hangup(h);
}

bool ec200_ppp_in_data_mode(const ec200_handle_t *h)
{
    return (h != NULL) && h->_ppp_data_mode;
}
