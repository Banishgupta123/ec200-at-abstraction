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

/** @defgroup EC200_Power Power Management
 *  @brief Set/get functional level, power down, sleep mode, and module reset (AT+CFUN/QPOWD/QSCLK).
 *  @{
 */

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

/* -------------------------------------------------------------------------
 * Power Saving Mode (AT+CPSMS)
 * ------------------------------------------------------------------------- */

/**
 * @brief Encode a periodic-TAU (T3412-extended) duration as a timer string.
 *
 * Picks the coarsest unit that represents @p seconds exactly, so the value
 * is never silently rounded. Representable units are 2 s, 30 s, 1 min,
 * 10 min, 1 h, 10 h and 320 h, each with a 0-31 multiplier — so 3600 s
 * (1 h) works, and 3660 s does not.
 *
 * @param seconds  Requested duration; 0 requests "timer deactivated".
 * @param out      Buffer for the 8-character string plus NUL.
 * @param out_sz   Size of @p out; must be >= ::EC200_PSM_TIMER_STR_LEN.
 *
 * @return EC200_OK, or EC200_ERR_PARAM on a NULL/short buffer or when no
 *         unit represents @p seconds exactly.
 */
ec200_status_t ec200_psm_encode_tau(uint32_t seconds,
                                    char    *out,
                                    size_t   out_sz);

/**
 * @brief Encode an active-time (T3324) duration as a timer string.
 *
 * As ec200_psm_encode_tau(), but T3324 offers only 2 s, 1 min and 6 min
 * (decihour) units.
 *
 * @param seconds  Requested duration; 0 requests "timer deactivated".
 * @param out      Buffer for the 8-character string plus NUL.
 * @param out_sz   Size of @p out; must be >= ::EC200_PSM_TIMER_STR_LEN.
 *
 * @return EC200_OK or EC200_ERR_PARAM.
 */
ec200_status_t ec200_psm_encode_active_time(uint32_t seconds,
                                            char    *out,
                                            size_t   out_sz);

/**
 * @brief Decode a 3GPP timer string back to seconds.
 *
 * Accepts either encoding: T3412-extended when @p is_tau is true, T3324
 * otherwise. A "deactivated" string decodes to 0 seconds.
 *
 * @param str          8-character bit string.
 * @param is_tau       true for a periodic-TAU value, false for active time.
 * @param seconds_out  Output: duration in seconds.
 *
 * @return EC200_OK, EC200_ERR_PARAM on a NULL argument, or EC200_ERR_PARSE
 *         when @p str is not eight '0'/'1' characters.
 */
ec200_status_t ec200_psm_decode_timer(const char *str,
                                      bool        is_tau,
                                      uint32_t   *seconds_out);

/**
 * @brief Request Power Saving Mode (AT+CPSMS).
 *
 * PSM only takes effect once the network agrees; call ec200_psm_get()
 * afterwards to see what was actually granted.
 *
 * @param h    Initialised library handle.
 * @param cfg  Settings to request. When @c enabled is false the timers are
 *             ignored and PSM is switched off.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_psm_set(ec200_handle_t           *h,
                             const ec200_psm_config_t *cfg);

/**
 * @brief Query the current PSM settings (AT+CPSMS?).
 *
 * Timer fields the module leaves empty come back as empty strings.
 *
 * @param h    Initialised library handle.
 * @param cfg  Output: current settings.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_psm_get(ec200_handle_t *h, ec200_psm_config_t *cfg);

/**
 * @brief Disable Power Saving Mode (AT+CPSMS=0).
 *
 * @param h  Initialised library handle.
 * @return   EC200_OK or an error code.
 */
ec200_status_t ec200_psm_disable(ec200_handle_t *h);

/* -------------------------------------------------------------------------
 * Extended DRX (AT+CEDRXS / AT+CEDRXRDP)
 * ------------------------------------------------------------------------- */

/**
 * @brief Request extended DRX (AT+CEDRXS).
 *
 * @param h    Initialised library handle.
 * @param cfg  Settings to request. When @c enabled is false eDRX is
 *             switched off for @c act_type.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_edrx_set(ec200_handle_t            *h,
                              const ec200_edrx_config_t *cfg);

/**
 * @brief Query the configured eDRX settings (AT+CEDRXS?).
 *
 * The module may list one line per access technology; the first is
 * returned. Use ec200_edrx_get_dynamic() for what is actually in force.
 *
 * @c act_type and @c requested are only populated once eDRX has been
 * configured at least once; before that they read 0 and "" respectively.
 * @c enabled comes from the reply's mode field, so it stays accurate even
 * when a previously requested value is still remembered.
 *
 * @param h    Initialised library handle.
 * @param cfg  Output: configured settings.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_edrx_get(ec200_handle_t *h, ec200_edrx_config_t *cfg);

/**
 * @brief Disable extended DRX for one access technology (AT+CEDRXS=0,...).
 *
 * @param h         Initialised library handle.
 * @param act_type  Access technology to disable eDRX for.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_edrx_disable(ec200_handle_t   *h,
                                  ec200_edrx_act_t  act_type);

/**
 * @brief Read the eDRX parameters the network granted (AT+CEDRXRDP).
 *
 * @param h    Initialised library handle.
 * @param out  Output: granted values. Fields the module omits are returned
 *             as empty strings.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_edrx_get_dynamic(ec200_handle_t       *h,
                                      ec200_edrx_dynamic_t *out);

/** @} */ /* EC200_Power */

#ifdef __cplusplus
}
#endif

#endif /* EC200_POWER_H */
