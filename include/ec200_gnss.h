/**
 * @file ec200_gnss.h
 * @brief GNSS/GPS API (Quectel AT+QGPS*).
 */

#ifndef EC200_GNSS_H
#define EC200_GNSS_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Turn the GNSS engine on (AT+QGPS=1).
 *
 * @param h  Initialised library handle.
 * @return   EC200_OK or an error code.
 */
ec200_status_t ec200_gnss_start(ec200_handle_t *h);

/**
 * @brief Turn the GNSS engine off (AT+QGPSEND).
 *
 * @param h  Initialised library handle.
 * @return   EC200_OK or an error code.
 */
ec200_status_t ec200_gnss_stop(ec200_handle_t *h);

/**
 * @brief Query the current GNSS fix status (AT+QGPS?).
 *
 * @param h        Initialised library handle.
 * @param enabled  Output: true if GNSS engine is running.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_gnss_get_status(ec200_handle_t *h, bool *enabled);

/**
 * @brief Read the current location (AT+QGPSLOC).
 *
 * @param h     Initialised library handle.
 * @param loc   Output: parsed location data.
 *
 * @return EC200_OK on a valid fix, EC200_ERR_CME if no fix yet, or another
 *         error code on failure.
 */
ec200_status_t ec200_gnss_get_location(ec200_handle_t       *h,
                                       ec200_gnss_location_t *loc);

/**
 * @brief Configure which NMEA sentence types are output (AT+QGPSCFG).
 *
 * @param h            Initialised library handle.
 * @param nmea_types   Bitmask of NMEA sentences to enable
 *                     (0x01=GGA, 0x02=RMC, 0x04=GSV, 0x08=GSA, 0x10=VTG).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_gnss_set_nmea_output(ec200_handle_t *h,
                                          uint8_t         nmea_types);

#ifdef __cplusplus
}
#endif

#endif /* EC200_GNSS_H */
