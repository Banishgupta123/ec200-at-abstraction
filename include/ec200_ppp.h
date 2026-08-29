/**
 * @file ec200_ppp.h
 * @brief PPP dial-up control plane (data-mode entry/exit).
 *
 * The library deliberately does NOT implement the PPP protocol — that is the
 * host network stack's job (lwIP PPPoS on ESP-IDF, for example).  What it
 * provides is the AT-command control plane around a PPP session:
 *
 * 1. ec200_ppp_dial() configures nothing by itself — set up the PDP context
 *    first (ec200_data_set_pdp()) — then dials `ATD*99***<cid>#` and returns
 *    as soon as the modem answers `CONNECT`.  From that moment the UART
 *    carries raw PPP frames: hand it to your PPP stack.
 * 2. While the session is up the handle is in *data mode*: every AT
 *    transaction API returns ::EC200_ERR_BUSY so a stray command cannot
 *    corrupt the PPP byte stream.
 * 3. ec200_ppp_escape() suspends the session with the `+++` escape (with the
 *    required guard silence) and returns the modem to command mode;
 *    ec200_ppp_resume() re-enters data mode with `ATO`;
 *    ec200_ppp_hangup() terminates the call with `ATH`.
 *
 * Quiesce your PPP stack before calling ec200_ppp_escape(): the escape
 * requires ~1 s of line silence, and any response bytes still in flight are
 * discarded while re-synchronising on the modem's "OK".
 *
 * CMUX (`AT+CMUX`, simultaneous AT + PPP channels) is not supported.
 */

#ifndef EC200_PPP_H
#define EC200_PPP_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_PPP PPP Control Plane
 *  @brief Dial-up entry/exit for host-stack PPP (lwIP PPPoS etc.).
 *  @{
 */

/** Guard silence before and after the "+++" escape sequence (ms). */
#define EC200_PPP_ESCAPE_GUARD_MS  (1100U)

/**
 * @brief Dial a PPP data call on PDP context @p cid.
 *
 * Sends `ATD*99***<cid>#` and waits for `CONNECT`.  On success the handle
 * enters data mode: the UART now carries PPP frames for your network stack,
 * and all AT transaction APIs return ::EC200_ERR_BUSY until
 * ec200_ppp_escape() succeeds.
 *
 * @param h    Initialised library handle (must be in command mode).
 * @param cid  PDP context ID (1-16), previously configured via
 *             ec200_data_set_pdp().
 *
 * @return EC200_OK on CONNECT; EC200_ERR_MODULE on `NO CARRIER`/`ERROR`;
 *         EC200_ERR_BUSY if already in data mode; EC200_ERR_PARAM /
 *         EC200_ERR_TIMEOUT / EC200_ERR_IO otherwise.
 */
ec200_status_t ec200_ppp_dial(ec200_handle_t *h, uint8_t cid);

/**
 * @brief Suspend the PPP session and return to command mode (`+++`).
 *
 * Performs guard-silence / `+++` / guard-silence, then waits for the
 * modem's `OK`.  Blocks for at least 2 × ::EC200_PPP_ESCAPE_GUARD_MS.
 * On success the handle leaves data mode (the PPP session itself stays
 * alive on the modem — use ec200_ppp_resume() or ec200_ppp_hangup()).
 *
 * @return EC200_OK; EC200_ERR_PARAM if not in data mode; EC200_ERR_TIMEOUT
 *         (still in data mode) / EC200_ERR_IO otherwise.
 */
ec200_status_t ec200_ppp_escape(ec200_handle_t *h);

/**
 * @brief Re-enter data mode of a suspended PPP session (`ATO`).
 *
 * @return EC200_OK on CONNECT (handle back in data mode);
 *         EC200_ERR_MODULE if the session is gone; other codes as
 *         ec200_ppp_dial().
 */
ec200_status_t ec200_ppp_resume(ec200_handle_t *h);

/**
 * @brief Terminate the data call (`ATH`).  Command mode only — call
 *        ec200_ppp_escape() first if the session is active.
 *
 * @return EC200_OK; EC200_ERR_PARAM if in data mode; other codes as
 *         ec200_at_send().
 */
ec200_status_t ec200_ppp_hangup(ec200_handle_t *h);

/**
 * @brief Convenience: escape (when in data mode) then hang up.
 *
 * @return EC200_OK, or the first failing step's status.
 */
ec200_status_t ec200_ppp_disconnect(ec200_handle_t *h);

/**
 * @brief Whether the handle is currently in PPP data mode.
 */
bool ec200_ppp_in_data_mode(const ec200_handle_t *h);

/** @} */ /* EC200_PPP */

#ifdef __cplusplus
}
#endif

#endif /* EC200_PPP_H */
