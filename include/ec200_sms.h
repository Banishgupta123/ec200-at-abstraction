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

/* -------------------------------------------------------------------------
 * Storage selection (AT+CPMS)
 * ------------------------------------------------------------------------- */

/**
 * @brief Select the message storage areas (AT+CPMS).
 *
 * @param h            Initialised library handle.
 * @param read_delete  mem1: storage ec200_sms_read()/ec200_sms_delete() act on.
 * @param write_send   mem2: storage ec200_sms_write()/ec200_sms_send_stored()
 *                     act on.  ::EC200_SMS_MEM_MT is not valid here.
 * @param receive      mem3: storage arriving messages are written to.
 * @param usage_out    Optional: occupancy of the three areas after the change
 *                     (NULL to discard).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_set_storage(ec200_handle_t      *h,
                                     ec200_sms_mem_t      read_delete,
                                     ec200_sms_mem_t      write_send,
                                     ec200_sms_mem_t      receive,
                                     ec200_sms_storage_t *usage_out);

/**
 * @brief Query the current storage selection and occupancy (AT+CPMS?).
 *
 * @param h          Initialised library handle.
 * @param usage_out  Output: used/total for each of the three areas.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_get_storage(ec200_handle_t      *h,
                                     ec200_sms_storage_t *usage_out);

/* -------------------------------------------------------------------------
 * Service centre address (AT+CSCA)
 * ------------------------------------------------------------------------- */

/**
 * @brief Set the SMS service centre address (AT+CSCA).
 *
 * @param h       Initialised library handle.
 * @param number  SMSC number, international format recommended ("+4477...").
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_set_smsc(ec200_handle_t *h, const char *number);

/**
 * @brief Query the SMS service centre address (AT+CSCA?).
 *
 * @param h       Initialised library handle.
 * @param out     Buffer receiving the NUL-terminated number.
 * @param out_sz  Size of @p out; ::EC200_MAX_PHONE_NUM_LEN is always enough.
 *
 * @return EC200_OK, or EC200_ERR_OVERFLOW if the address does not fit in
 *         @p out — it is never truncated, because writing a truncated number
 *         back with ec200_sms_set_smsc() would misconfigure the module.
 */
ec200_status_t ec200_sms_get_smsc(ec200_handle_t *h, char *out, size_t out_sz);

/* -------------------------------------------------------------------------
 * Store and send-from-storage (AT+CMGW / AT+CMSS)
 * ------------------------------------------------------------------------- */

/**
 * @brief Write a message to storage without sending it (AT+CMGW).
 *
 * Assumes text mode (see ec200_sms_set_format()).  The message is stored in
 * the mem2 area selected via ec200_sms_set_storage().
 *
 * @param h          Initialised library handle.
 * @param number     Destination number stored with the message.
 * @param text       Message text (max ::EC200_MAX_SMS_TEXT_LEN, no Ctrl-Z).
 * @param index_out  Optional: storage index the module assigned (NULL to
 *                   discard).
 *
 * @return EC200_OK on success, EC200_ERR_PARAM for an over-long or
 *         Ctrl-Z-bearing input, or an error code.
 */
ec200_status_t ec200_sms_write(ec200_handle_t *h,
                               const char     *number,
                               const char     *text,
                               int            *index_out);

/**
 * @brief Send a message previously stored with ec200_sms_write() (AT+CMSS).
 *
 * @param h       Initialised library handle.
 * @param index   Storage index to send (1-based).
 * @param mr_out  Optional: message reference returned by the network (NULL to
 *                discard).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_send_stored(ec200_handle_t *h,
                                     int             index,
                                     int            *mr_out);

/* -------------------------------------------------------------------------
 * New-message indication (AT+CNMI)
 * ------------------------------------------------------------------------- */

/**
 * @brief Configure new-message indications (AT+CNMI).
 *
 * To be told about each arriving message without having it dumped into the
 * command stream, use `{2, 1, 0, 0, 0}` and register a handler for the
 * `+CMTI:` URC:
 *
 * @code
 *   const ec200_sms_cnmi_t cfg = { 2, 1, 0, 0, 0 };
 *   ec200_sms_set_indication(&h, &cfg);
 *   ec200_at_register_urc(&h, "+CMTI:", on_new_sms, NULL);
 * @endcode
 *
 * The handler then turns the line into an index with
 * ec200_sms_parse_notification() and fetches it with ec200_sms_read().
 *
 * @param h    Initialised library handle.
 * @param cfg  Settings to apply; out-of-range fields return EC200_ERR_PARAM.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_set_indication(ec200_handle_t         *h,
                                        const ec200_sms_cnmi_t *cfg);

/**
 * @brief Query the current new-message indication settings (AT+CNMI?).
 *
 * @param h    Initialised library handle.
 * @param cfg  Output: the module's current settings.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_sms_get_indication(ec200_handle_t   *h,
                                        ec200_sms_cnmi_t *cfg);

/**
 * @brief Parse a `+CMTI:` notification line into storage and index.
 *
 * Pure parser — it sends nothing and needs no handle, so it is safe to call
 * from inside a URC handler.
 *
 * @param line  The URC line, e.g. `+CMTI: "ME",3`.
 * @param out   Output: storage area and 1-based index.
 *
 * @return EC200_OK, EC200_ERR_PARAM on a NULL argument, or EC200_ERR_PARSE
 *         when the line is not a well-formed +CMTI notification.
 */
ec200_status_t ec200_sms_parse_notification(const char               *line,
                                            ec200_sms_notification_t *out);

/** @} */ /* EC200_SMS */

#ifdef __cplusplus
}
#endif

#endif /* EC200_SMS_H */
