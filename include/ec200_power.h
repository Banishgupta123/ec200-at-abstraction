/**
 * @file ec200_power.h
 * @brief Power management API (AT+CFUN, AT+QPOWD, AT+QSCLK).
 */

#ifndef EC200_POWER_H
#define EC200_POWER_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set module functional level (AT+CFUN).
 *
 * @param h       Initialised library handle.
 * @param level   Desired functional level.
 * @param reset   true = reset the module before applying the new level.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_power_set_cfun(ec200_handle_t *h,
                                    ec200_cfun_t    level,
                                    bool            reset);

/**
 * @brief Get the current functional level (AT+CFUN?).
 *
 * @param h      Initialised library handle.
 * @param level  Output: current functional level.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_power_get_cfun(ec200_handle_t *h, ec200_cfun_t *level);

/**
 * @brief Power the module down (AT+QPOWD).
 *
 * @param h        Initialised library handle.
 * @param normal   true = normal power-down (AT+QPOWD=1),
 *                 false = immediate power-down (AT+QPOWD=0).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_power_down(ec200_handle_t *h, bool normal);

/**
 * @brief Configure slow-clock / sleep mode (AT+QSCLK).
 *
 * @param h      Initialised library handle.
 * @param enable true = enable slow clock (low-power sleep allowed),
 *               false = disable.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_power_set_sleep(ec200_handle_t *h, bool enable);

/**
 * @brief Reset the module using AT+CFUN=1,1.
 *
 * This is a convenience wrapper around ec200_power_set_cfun() with reset=true.
 *
 * @param h  Initialised library handle.
 * @return   EC200_OK (the module will reset; the caller must wait and
 *           re-initialise).
 */
ec200_status_t ec200_power_reset(ec200_handle_t *h);

#ifdef __cplusplus
}
#endif

#endif /* EC200_POWER_H */
