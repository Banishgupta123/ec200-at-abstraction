/**
 * @file ec200_ssl.h
 * @brief TLS/SSL context configuration (AT+QSSLCFG).
 *
 * An SSL context bundles a TLS version, cipher-suite selection, an
 * authentication level, and (for authenticated levels) certificate/key
 * files previously uploaded with ec200_file_upload().  Once configured, a
 * context id is referenced by:
 *   - ec200_http_set_ssl_context() for HTTPS,
 *   - ec200_mqtt_config_t::ssl_ctx_id / ::use_tls for MQTTS,
 *   - ec200_ssl_socket_open() for TLS TCP sockets.
 */

#ifndef EC200_SSL_H
#define EC200_SSL_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_SSL TLS/SSL
 *  @brief SSL context configuration shared by HTTPS, MQTTS, and TLS sockets.
 *  @{
 */

/**
 * @brief Apply a full SSL context configuration (a sequence of AT+QSSLCFG).
 *
 * Issues sslversion, ciphersuite, and seclevel; then, according to
 * @p cfg->seclevel, binds the CA cert (>=SERVER) and client cert/key
 * (==MUTUAL).  A non-empty filename must already exist in the modem UFS.
 *
 * @param h    Initialised handle.
 * @param cfg  Context configuration (ctx_id 0-5).
 *
 * @return EC200_OK, EC200_ERR_PARAM (bad ctx / missing required cert name),
 *         or the first failing AT command's status.
 */
ec200_status_t ec200_ssl_configure(ec200_handle_t          *h,
                                   const ec200_ssl_config_t *cfg);

/**
 * @brief Set just the authentication level of a context
 *        (AT+QSSLCFG="seclevel").
 *
 * @param h       Initialised handle.
 * @param ctx_id  SSL context id (0-5).
 * @param level   Authentication level.
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_ssl_set_seclevel(ec200_handle_t      *h,
                                      uint8_t              ctx_id,
                                      ec200_ssl_seclevel_t level);

/** @} */ /* EC200_SSL */

#ifdef __cplusplus
}
#endif

#endif /* EC200_SSL_H */
