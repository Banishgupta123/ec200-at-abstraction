/**
 * @file ec200_time.h
 * @brief Module clock, network time, and NTP synchronisation.
 *
 * The module keeps a real-time clock that can be read/set directly
 * (AT+CCLK), synchronised from the network (AT+CTZU / AT+QLTS) or from an
 * NTP server over an activated PDP context (AT+QNTP).  A valid clock
 * matters for TLS: certificate validity checking needs the current date
 * unless ec200_ssl_config_t::ignore_localtime is set.
 */

#ifndef EC200_TIME_H
#define EC200_TIME_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_Time Clock and Time Sync
 *  @brief Module RTC, network time, and NTP synchronisation.
 *  @{
 */

/**
 * @brief Read the module clock (AT+CCLK?).
 *
 * @param h   Initialised handle.
 * @param dt  Output: date, time, and time-zone offset in quarter-hours.
 * @return EC200_OK, EC200_ERR_PARAM, or EC200_ERR_PARSE.
 */
ec200_status_t ec200_time_get(ec200_handle_t *h, ec200_datetime_t *dt);

/**
 * @brief Set the module clock (AT+CCLK=).
 *
 * @param h   Initialised handle.
 * @param dt  Date/time to apply (year >= 2000).
 * @return EC200_OK, EC200_ERR_PARAM, or an error code.
 */
ec200_status_t ec200_time_set(ec200_handle_t *h, const ec200_datetime_t *dt);

/**
 * @brief Enable or disable automatic network time-zone update (AT+CTZU).
 *
 * @param h       Initialised handle.
 * @param enable  true = update the clock from the network automatically.
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_time_set_auto_update(ec200_handle_t *h, bool enable);

/**
 * @brief Read the latest network time reported by the operator (AT+QLTS).
 *
 * @param h   Initialised handle.
 * @param dt  Output: network date/time.
 * @return EC200_OK, or EC200_ERR_PARSE when the network has not supplied
 *         time yet.
 */
ec200_status_t ec200_time_get_network(ec200_handle_t *h,
                                      ec200_datetime_t *dt);

/**
 * @brief Synchronise the module clock from an NTP server (AT+QNTP).
 *
 * Requires an activated PDP context.
 *
 * @param h           Initialised handle.
 * @param pdp_ctx     PDP context id (1-16), already activated.
 * @param server      NTP server hostname (e.g. "pool.ntp.org").
 * @param port        NTP port (123 when 0 is passed).
 * @param timeout_ms  Deadline for the synchronisation.
 *
 * @return EC200_OK, EC200_ERR_PARAM, EC200_ERR_MODULE (sync failed), or
 *         EC200_ERR_TIMEOUT.
 */
ec200_status_t ec200_time_sync_ntp(ec200_handle_t *h,
                                   uint8_t         pdp_ctx,
                                   const char     *server,
                                   uint16_t        port,
                                   uint32_t        timeout_ms);

/** @} */ /* EC200_Time */

#ifdef __cplusplus
}
#endif

#endif /* EC200_TIME_H */
