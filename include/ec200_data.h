/**
 * @file ec200_data.h
 * @brief PDP context / packet data connection API.
 */

#ifndef EC200_DATA_H
#define EC200_DATA_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_Data PDP Context / Data Connection
 *  @brief Configure and activate packet-data (PDP) contexts.
 *  @{
 */

/**
 * @brief Configure a PDP context (AT+CGDCONT).
 *
 * @param h    Initialised library handle.
 * @param ctx  PDP context parameters.  @c ctx->cid must be 1-16.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_data_set_pdp(ec200_handle_t          *h,
                                  const ec200_pdp_context_t *ctx);

/**
 * @brief Activate a PDP context (AT+CGACT=1,\<cid\>).
 *
 * @param h    Initialised library handle.
 * @param cid  Context ID to activate (1-16).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_data_activate(ec200_handle_t *h, uint8_t cid);

/**
 * @brief Deactivate a PDP context (AT+CGACT=0,\<cid\>).
 *
 * @param h    Initialised library handle.
 * @param cid  Context ID to deactivate.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_data_deactivate(ec200_handle_t *h, uint8_t cid);

/**
 * @brief Query the IP address assigned to a PDP context (AT+CGPADDR).
 *
 * @param h          Initialised library handle.
 * @param cid        Context ID to query.
 * @param ip_buf     Output buffer for the assigned IP address string.
 * @param ip_buf_sz  Size of @p ip_buf (minimum EC200_MAX_IP_ADDR_LEN).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_data_get_ip(ec200_handle_t *h,
                                 uint8_t         cid,
                                 char           *ip_buf,
                                 size_t          ip_buf_sz);

/**
 * @brief Convenience helper: configure, activate and retrieve IP in one call.
 *
 * Calls ec200_data_set_pdp(), ec200_data_activate(), and ec200_data_get_ip()
 * in sequence.  On success @p ctx->ip_addr is populated.
 *
 * @param h    Initialised library handle.
 * @param ctx  PDP context parameters (ip_addr field is written on success).
 *
 * @return EC200_OK or the first error that occurred.
 */
ec200_status_t ec200_data_connect(ec200_handle_t    *h,
                                  ec200_pdp_context_t *ctx);

/** @} */ /* EC200_Data */

#ifdef __cplusplus
}
#endif

#endif /* EC200_DATA_H */
