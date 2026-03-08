/**
 * @file ec200_mqtt.h
 * @brief MQTT client API (Quectel AT+QMT*).
 */

#ifndef EC200_MQTT_H
#define EC200_MQTT_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open a TCP connection to the MQTT broker (AT+QMTOPEN).
 *
 * @param h       Initialised library handle.
 * @param cfg     MQTT connection configuration.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_mqtt_open(ec200_handle_t          *h,
                               const ec200_mqtt_config_t *cfg);

/**
 * @brief Connect to the MQTT broker (AT+QMTCONN).
 *
 * Must be called after ec200_mqtt_open().
 *
 * @param h    Initialised library handle.
 * @param cfg  MQTT connection configuration (client_id, username, password).
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_mqtt_connect(ec200_handle_t          *h,
                                  const ec200_mqtt_config_t *cfg);

/**
 * @brief Disconnect from the MQTT broker (AT+QMTDISC).
 *
 * @param h          Initialised library handle.
 * @param client_idx MQTT client index from the configuration.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_mqtt_disconnect(ec200_handle_t *h, uint8_t client_idx);

/**
 * @brief Close the underlying TCP connection to the broker (AT+QMTCLOSE).
 *
 * @param h              Initialised library handle.
 * @param tcp_connect_id TCP connect ID from the configuration.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_mqtt_close(ec200_handle_t *h, uint8_t tcp_connect_id);

/**
 * @brief Subscribe to an MQTT topic (AT+QMTSUB).
 *
 * @param h          Initialised library handle.
 * @param client_idx MQTT client index.
 * @param msg_id     Packet identifier (1-65535).
 * @param topic      NUL-terminated topic filter string.
 * @param qos        Desired QoS level.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_mqtt_subscribe(ec200_handle_t  *h,
                                    uint8_t          client_idx,
                                    uint16_t         msg_id,
                                    const char      *topic,
                                    ec200_mqtt_qos_t qos);

/**
 * @brief Unsubscribe from an MQTT topic (AT+QMTUNS).
 *
 * @param h          Initialised library handle.
 * @param client_idx MQTT client index.
 * @param msg_id     Packet identifier.
 * @param topic      NUL-terminated topic filter string.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_mqtt_unsubscribe(ec200_handle_t *h,
                                      uint8_t         client_idx,
                                      uint16_t        msg_id,
                                      const char     *topic);

/**
 * @brief Publish a message to an MQTT topic (AT+QMTPUB).
 *
 * @param h           Initialised library handle.
 * @param client_idx  MQTT client index.
 * @param msg_id      Packet identifier (0 for QoS 0).
 * @param qos         QoS level.
 * @param retain      Retain flag.
 * @param topic       NUL-terminated topic string.
 * @param payload     Pointer to payload bytes.
 * @param payload_len Payload length in bytes.
 *
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_mqtt_publish(ec200_handle_t  *h,
                                  uint8_t          client_idx,
                                  uint16_t         msg_id,
                                  ec200_mqtt_qos_t qos,
                                  bool             retain,
                                  const char      *topic,
                                  const uint8_t   *payload,
                                  uint32_t         payload_len);

/**
 * @brief Register a callback for incoming MQTT messages ("+QMTRECV" URC).
 *
 * The callback is invoked from ec200_at_poll_urc() when a publish is received.
 *
 * @param h        Initialised library handle.
 * @param callback Callback function (NULL to unregister).
 */
void ec200_mqtt_set_message_cb(ec200_handle_t   *h,
                               ec200_mqtt_msg_fn callback);

#ifdef __cplusplus
}
#endif

#endif /* EC200_MQTT_H */
