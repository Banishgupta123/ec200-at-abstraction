/**
 * @file ec200_sim.h
 * @brief SIM card management API.
 */

#ifndef EC200_SIM_H
#define EC200_SIM_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Query the SIM PIN status (AT+CPIN?).
 *
 * @param h       Initialised library handle.
 * @param status  Output: current SIM state.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sim_get_status(ec200_handle_t  *h,
                                    ec200_sim_status_t *status);

/**
 * @brief Enter the SIM PIN (AT+CPIN=<pin>).
 *
 * @param h    Initialised library handle.
 * @param pin  NUL-terminated PIN string.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sim_enter_pin(ec200_handle_t *h, const char *pin);

/**
 * @brief Read the IMSI from the SIM (AT+CIMI).
 *
 * @param h        Initialised library handle.
 * @param imsi     Output buffer (minimum EC200_MAX_IMSI_LEN bytes).
 * @param imsi_sz  Size of @p imsi buffer.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sim_get_imsi(ec200_handle_t *h,
                                  char           *imsi,
                                  size_t          imsi_sz);

/**
 * @brief Read the ICCID from the SIM (AT+CCID).
 *
 * @param h         Initialised library handle.
 * @param iccid     Output buffer (minimum EC200_MAX_ICCID_LEN bytes).
 * @param iccid_sz  Size of @p iccid buffer.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sim_get_iccid(ec200_handle_t *h,
                                   char           *iccid,
                                   size_t          iccid_sz);

#ifdef __cplusplus
}
#endif

#endif /* EC200_SIM_H */
