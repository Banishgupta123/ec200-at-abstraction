/**
 * @file ec200.h
 * @brief Main public header for the EC200 AT abstraction library.
 *
 * Include this single header to pull in all sub-module APIs:
 *
 * @code
 *   #include "ec200.h"
 * @endcode
 *
 * ## Quick-start
 *
 * 1. Implement the three transport callbacks (write, read, delay_ms) for your
 *    MCU platform (see examples/wrapper_template.c).
 * 2. Declare an ::ec200_handle_t and call ec200_init().
 * 3. Call ec200_check_at() to verify communication.
 * 4. Use the domain API (ec200_sim_*, ec200_network_*, ec200_sms_*, …).
 */

#ifndef EC200_H
#define EC200_H

#include "ec200_types.h"
#include "ec200_at.h"
#include "ec200_sim.h"
#include "ec200_network.h"
#include "ec200_sms.h"
#include "ec200_data.h"
#include "ec200_tcpip.h"
#include "ec200_http.h"
#include "ec200_mqtt.h"
#include "ec200_gnss.h"
#include "ec200_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Library version
 * ------------------------------------------------------------------------- */
#define EC200_LIB_VERSION_MAJOR  1
#define EC200_LIB_VERSION_MINOR  0
#define EC200_LIB_VERSION_PATCH  0

/* -------------------------------------------------------------------------
 * Core API
 * ------------------------------------------------------------------------- */

/**
 * @brief Initialise the library handle and verify module communication.
 *
 * This function must be called once before any other library function.
 * It writes default values into the handle's internal fields and optionally
 * sends a basic "AT" probe (ec200_check_at()) to confirm the module is alive.
 *
 * @param h           Pointer to a caller-allocated ::ec200_handle_t.
 * @param write_fn    Platform UART write callback (must not be NULL).
 * @param read_fn     Platform UART read callback  (must not be NULL).
 * @param delay_fn    Platform delay callback      (must not be NULL).
 * @param user_ctx    Opaque pointer forwarded to every callback (may be NULL).
 *
 * @return EC200_OK on success, EC200_ERR_PARAM if any required callback is
 *         NULL, or EC200_ERR_TIMEOUT if the module does not respond.
 */
ec200_status_t ec200_init(ec200_handle_t *h,
                          ec200_write_fn  write_fn,
                          ec200_read_fn   read_fn,
                          ec200_delay_fn  delay_fn,
                          void           *user_ctx);

/**
 * @brief Send a plain "AT" command and verify the module replies with "OK".
 *
 * Useful as a keep-alive or post-reset check.
 *
 * @param h  Initialised library handle.
 * @return   EC200_OK or EC200_ERR_TIMEOUT.
 */
ec200_status_t ec200_check_at(ec200_handle_t *h);

/**
 * @brief Read the module IMEI string.
 *
 * Executes AT+GSN.
 *
 * @param h        Initialised library handle.
 * @param imei     Output buffer (minimum EC200_MAX_IMEI_LEN bytes).
 * @param imei_sz  Size of @p imei buffer.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_get_imei(ec200_handle_t *h,
                              char           *imei,
                              size_t          imei_sz);

/**
 * @brief Read the module firmware revision string.
 *
 * Executes AT+GMR.
 *
 * @param h       Initialised library handle.
 * @param ver     Output buffer (minimum EC200_MAX_FW_VER_LEN bytes).
 * @param ver_sz  Size of @p ver buffer.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_get_fw_version(ec200_handle_t *h,
                                    char           *ver,
                                    size_t          ver_sz);

/**
 * @brief Read the module manufacturer identification.
 *
 * Executes ATI.
 *
 * @param h      Initialised library handle.
 * @param info   Output buffer for the identification string.
 * @param info_sz Size of @p info in bytes.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_get_module_info(ec200_handle_t *h,
                                     char           *info,
                                     size_t          info_sz);

/**
 * @brief Enable or disable command echo (ATE0 / ATE1).
 *
 * @param h      Initialised library handle.
 * @param enable true = echo on (ATE1), false = echo off (ATE0).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_set_echo(ec200_handle_t *h, bool enable);

/**
 * @brief Enable verbose error reporting (+CME ERROR numeric vs. text).
 *
 * Executes AT+CMEE=@p mode.
 *
 * @param h    Initialised library handle.
 * @param mode 0 = disabled, 1 = numeric, 2 = verbose text.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_set_cmee(ec200_handle_t *h, uint8_t mode);

/**
 * @brief Register an unsolicited result code (URC) handler.
 *
 * The callback is invoked from ec200_at_poll_urc() whenever the module sends
 * a line that does not correspond to an active command transaction.
 *
 * @param h          Initialised library handle.
 * @param handler    Callback function (NULL to unregister).
 */
void ec200_set_urc_handler(ec200_handle_t       *h,
                           ec200_urc_handler_fn  handler);

/**
 * @brief Return a human-readable string for an ec200_status_t code.
 *
 * @param status  Status code.
 * @return        Pointer to a static string (never NULL).
 */
const char *ec200_status_str(ec200_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* EC200_H */
