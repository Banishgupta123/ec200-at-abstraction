/**
 * @file ec200_sms.h
 * @brief SMS send and receive API.
 */

#ifndef EC200_SMS_H
#define EC200_SMS_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_SMS SMS Messaging
 *  @brief Set format, send, read, list, and delete SMS messages.
 *  @{
 */

/**
 * @brief Set SMS message format (AT+CMGF).
 *
 * @param h       Initialised library handle.
 * @param format  EC200_SMS_FORMAT_PDU or EC200_SMS_FORMAT_TEXT.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_set_format(ec200_handle_t  *h,
                                    ec200_sms_format_t format);

/**
 * @brief Send an SMS in text mode (AT+CMGS).
 *
 * Assumes text mode has already been enabled via ec200_sms_set_format().
 *
 * @param h       Initialised library handle.
 * @param number  Destination phone number (NUL-terminated).
 * @param text    Message text (NUL-terminated, max EC200_MAX_SMS_TEXT_LEN).
 *
 * @return EC200_OK on successful submission, or an error code.
 */
ec200_status_t ec200_sms_send(ec200_handle_t *h,
                              const char     *number,
                              const char     *text);

/**
 * @brief Read a single SMS message by index (AT+CMGR).
 *
 * @param h      Initialised library handle.
 * @param index  Storage index (1-based).
 * @param msg    Output: parsed message structure.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_read(ec200_handle_t    *h,
                              int                index,
                              ec200_sms_message_t *msg);

/**
 * @brief List SMS messages matching a status filter (AT+CMGL).
 *
 * @param h          Initialised library handle.
 * @param stat       Filter: which message status to list.
 * @param msgs       Caller-allocated array to receive the messages.
 * @param max_msgs   Capacity of @p msgs array (maximum messages to retrieve).
 * @param count_out  Output: number of messages actually stored in @p msgs.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_list(ec200_handle_t      *h,
                              ec200_sms_stat_t      stat,
                              ec200_sms_message_t  *msgs,
                              uint8_t               max_msgs,
                              uint8_t              *count_out);

/**
 * @brief Delete an SMS message from storage (AT+CMGD).
 *
 * @param h      Initialised library handle.
 * @param index  Storage index to delete (1-based).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_delete(ec200_handle_t *h, int index);

/**
 * @brief Delete all SMS messages in a given status category (AT+CMGD with flag).
 *
 * @param h     Initialised library handle.
 * @param flag  Deletion flag:
 *              1 = delete all read messages,
 *              2 = delete all read + sent,
 *              3 = delete all read + sent + unsent,
 *              4 = delete all.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_delete_all(ec200_handle_t *h, uint8_t flag);

/** @} */ /* EC200_SMS */

#ifdef __cplusplus
}
#endif

#endif /* EC200_SMS_H */
