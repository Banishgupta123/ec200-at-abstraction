/**
 * @file ec200_network.h
 * @brief Network registration and operator management API.
 */

#ifndef EC200_NETWORK_H
#define EC200_NETWORK_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_Network Network Registration and Operators
 *  @brief CS/GPRS/LTE registration, signal quality, and operator selection.
 *  @{
 */

/**
 * @brief Query CS (circuit-switched) network registration (AT+CREG?).
 *
 * @param h       Initialised library handle.
 * @param status  Output: registration status.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_net_get_creg(ec200_handle_t    *h,
                                  ec200_reg_status_t *status);

/**
 * @brief Query GPRS network registration (AT+CGREG?).
 *
 * @param h       Initialised library handle.
 * @param status  Output: GPRS registration status.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_net_get_cgreg(ec200_handle_t    *h,
                                   ec200_reg_status_t *status);

/**
 * @brief Query LTE/EPS network registration (AT+CEREG?).
 *
 * @param h       Initialised library handle.
 * @param status  Output: EPS registration status.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_net_get_cereg(ec200_handle_t    *h,
                                   ec200_reg_status_t *status);

/**
 * @brief Query current signal quality (AT+CSQ).
 *
 * Populates the @p sq->rssi and @p sq->ber fields.
 * For extended LTE metrics use ec200_net_get_signal_ext().
 *
 * @param h   Initialised library handle.
 * @param sq  Output: signal quality structure.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_net_get_signal(ec200_handle_t        *h,
                                    ec200_signal_quality_t *sq);

/**
 * @brief Query extended signal quality (AT+QCSQ) including LTE RSRP and SINR.
 *
 * @param h   Initialised library handle.
 * @param sq  Output: signal quality structure (all fields populated).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_net_get_signal_ext(ec200_handle_t        *h,
                                        ec200_signal_quality_t *sq);

/**
 * @brief Query the current operator (AT+COPS?).
 *
 * @param h     Initialised library handle.
 * @param info  Output: operator information structure.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_net_get_operator(ec200_handle_t       *h,
                                      ec200_operator_info_t *info);

/**
 * @brief Select operator manually or set automatic selection (AT+COPS=…).
 *
 * @param h       Initialised library handle.
 * @param mode    Operator selection mode.
 * @param format  Operator name format (ignored when mode = AUTOMATIC).
 * @param oper    Operator string (numeric MCC+MNC or name; ignored for AUTO).
 * @param act     Access technology (ignored when mode = AUTOMATIC).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_net_set_operator(ec200_handle_t    *h,
                                      ec200_cops_mode_t  mode,
                                      ec200_cops_fmt_t   format,
                                      const char        *oper,
                                      ec200_act_t        act);

/**
 * @brief Wait (block) until the device is registered on the network.
 *
 * Polls AT+CEREG every 2 s.  Returns as soon as HOME or ROAMING is reported.
 *
 * @param h           Initialised library handle.
 * @param timeout_ms  Maximum time to wait (milliseconds).
 *
 * @return EC200_OK when registered, EC200_ERR_TIMEOUT if not registered within
 *         @p timeout_ms.
 */
ec200_status_t ec200_net_wait_registered(ec200_handle_t *h,
                                         uint32_t        timeout_ms);

/** @} */ /* EC200_Network */

#ifdef __cplusplus
}
#endif

#endif /* EC200_NETWORK_H */
