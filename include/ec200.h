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

/**
 * @mainpage EC200 AT Abstraction Library
 *
 * @section intro_sec Introduction
 *
 * A platform-independent C99 abstraction layer for **Quectel EC200U** cellular
 * modules.  The library wraps the AT command set into a clean, type-safe API
 * with no dependency on any specific MCU or RTOS, no dynamic allocation, and
 * shallow stack frames (~2.6 KB RAM per handle).
 *
 * @section arch_sec Architecture
 *
 * @code
 *  ┌────────────────────────────────────────┐
 *  │         Your Application Code          │
 *  └──────────────────┬─────────────────────┘
 *                     │  ec200_*.h API
 *  ┌──────────────────▼─────────────────────┐
 *  │      EC200 AT Abstraction Library      │
 *  │  (ec200.h / sim / network / sms / …)  │
 *  └──────────────────┬─────────────────────┘
 *                     │  write / read / delay callbacks
 *  ┌──────────────────▼─────────────────────┐
 *  │  Your Platform Wrapper (HAL / RTOS)    │
 *  └──────────────────┬─────────────────────┘
 *                     │  UART hardware
 *  ┌──────────────────▼─────────────────────┐
 *  │         Quectel EC200U Module          │
 *  └────────────────────────────────────────┘
 * @endcode
 *
 * The AT engine (@ref EC200_AT) models the response shapes the module
 * actually produces — plain `OK` transactions, prefixed responses, raw-data
 * phases, asynchronous OK-then-URC commands, and `>` data prompts — as
 * explicit primitives, so the domain modules never re-implement transport
 * logic.  Only complete lines ever reach parsers (partial reads stay
 * buffered), every wait is bounded by a deadline budget plus a line cap, and
 * URC lines registered with ec200_at_register_urc() are dispatched even when
 * they arrive in the middle of another command.
 *
 * @section qs_sec Quick Start
 *
 * @subsection qs1 1. Implement the platform wrapper
 * @code
 * static int my_uart_write(const uint8_t *data, uint16_t len, void *ctx) {
 *     return HAL_UART_Transmit(&huart2, data, len, 1000) == HAL_OK ? len : -1;
 * }
 * // Contract: >0 = bytes read, 0 = timeout (no data), <0 = fatal fault only
 * static int my_uart_read(uint8_t *data, uint16_t len,
 *                         uint32_t timeout_ms, void *ctx) {
 *     return uart_ring_buf_read(data, len, timeout_ms);
 * }
 * static void my_delay_ms(uint32_t ms, void *ctx) { HAL_Delay(ms); }
 * @endcode
 *
 * @subsection qs2 2. Initialise the library
 * @code
 * ec200_handle_t modem;
 * ec200_status_t st = ec200_init(&modem, my_uart_write, my_uart_read,
 *                                my_delay_ms, NULL);
 * // init probes the module and disables command echo (ATE0)
 * @endcode
 *
 * @subsection qs3 3. Use the API
 * @code
 * ec200_net_wait_registered(&modem, 60000);
 * ec200_pdp_context_t pdp = { .cid=1, .type=EC200_PDP_TYPE_IP, .apn="internet" };
 * ec200_data_connect(&modem, &pdp);
 * // ...and poll for unsolicited events from your main loop:
 * ec200_at_poll_urc(&modem, 0);
 * @endcode
 *
 * @section modules_sec API Modules
 *
 * | Module             | Header              | Description                        |
 * |--------------------|---------------------|------------------------------------|
 * | @ref EC200_Core    | ec200.h             | Init, IMEI, firmware, echo, CMEE   |
 * | @ref EC200_Types   | ec200_types.h       | Types, enums, structs, constants   |
 * | @ref EC200_AT      | ec200_at.h          | AT engine: primitives, URC registry |
 * | @ref EC200_SIM     | ec200_sim.h         | SIM PIN/IMSI/ICCID                 |
 * | @ref EC200_Network | ec200_network.h     | Registration, signal, operator     |
 * | @ref EC200_SMS     | ec200_sms.h         | Send, read, list, delete SMS       |
 * | @ref EC200_Data    | ec200_data.h        | PDP context / auth / data connection |
 * | @ref EC200_TCPIP   | ec200_tcpip.h       | TCP/UDP sockets                    |
 * | @ref EC200_HTTP    | ec200_http.h        | HTTP client                        |
 * | @ref EC200_MQTT    | ec200_mqtt.h        | MQTT client (binary-safe publish)  |
 * | @ref EC200_GNSS    | ec200_gnss.h        | GNSS/GPS location                  |
 * | @ref EC200_Power   | ec200_power.h       | Power management                   |
 * | @ref EC200_PPP     | ec200_ppp.h         | PPP dial-up control plane          |
 * | @ref EC200_File    | ec200_file.h        | Modem filesystem (cert storage)    |
 * | @ref EC200_SSL     | ec200_ssl.h         | TLS context configuration          |
 * | @ref EC200_SSLSock | ec200_ssl_socket.h  | TLS client sockets                 |
 *
 * @section err_sec Error Handling
 *
 * Every API function returns ::ec200_status_t.  Use ec200_status_str() to
 * convert a code to a human-readable string.  After EC200_ERR_CME or
 * EC200_ERR_CMS, call ec200_at_last_cme_error() / ec200_at_last_cms_error()
 * to retrieve the raw numeric error code from the module; the error state is
 * reset at the start of every command.  EC200_ERR_MODULE indicates a plain
 * `ERROR` line or a nonzero command-specific result code.
 *
 * @section thread_sec Threading Model
 *
 * The library is not thread-safe: all calls on one handle — URC polling
 * included — must come from a single task/thread.  In RTOS designs, dedicate
 * a modem task that owns the handle and serialise requests to it.
 *
 * @section build_sec Building & Testing
 * @code
 * cmake -S . -B build
 * cmake --build build
 * ctest --test-dir build                  // Unity/CMock host tests
 * cmake --build build --target coverage   // gcov/gcovr report
 * cmake --build build --target cppcheck   // static analysis
 * @endcode
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
#include "ec200_ppp.h"
#include "ec200_file.h"
#include "ec200_ssl.h"
#include "ec200_ssl_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_Core Core API
 *  @brief Library initialisation, module identification, and utility functions.
 *  @{
 */

/* -------------------------------------------------------------------------
 * Library version
 * ------------------------------------------------------------------------- */
#define EC200_LIB_VERSION_MAJOR  1
#define EC200_LIB_VERSION_MINOR  1
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

/** @} */ /* EC200_Core */

#ifdef __cplusplus
}
#endif

#endif /* EC200_H */
