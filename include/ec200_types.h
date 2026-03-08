/**
 * @file ec200_types.h
 * @brief Common types, enumerations, and structures for the EC200 AT abstraction library.
 *
 * This header defines all shared data types used throughout the library.
 * The library is platform-independent; the user must provide a small UART
 * wrapper (see examples/wrapper_template.c) that supplies the I/O callbacks.
 */

#ifndef EC200_TYPES_H
#define EC200_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Buffer sizes
 * ------------------------------------------------------------------------- */
#define EC200_RX_BUFFER_SIZE      (1024U)   /**< Receive ring-buffer size (bytes) */
#define EC200_TX_BUFFER_SIZE      (512U)    /**< Transmit scratch-buffer size     */
#define EC200_MAX_OPERATOR_LEN    (32U)     /**< Max operator name string length  */
#define EC200_MAX_PHONE_NUM_LEN   (20U)     /**< Max phone-number string length   */
#define EC200_MAX_SMS_TEXT_LEN    (160U)    /**< Max SMS text body length         */
#define EC200_MAX_IP_ADDR_LEN     (46U)     /**< Max IP address string length     */
#define EC200_MAX_IMEI_LEN        (16U)     /**< IMEI string length (+NUL)        */
#define EC200_MAX_IMSI_LEN        (16U)     /**< IMSI string length (+NUL)        */
#define EC200_MAX_ICCID_LEN       (21U)     /**< ICCID string length (+NUL)       */
#define EC200_MAX_FW_VER_LEN      (64U)     /**< Firmware version string length   */
#define EC200_MAX_URL_LEN         (256U)    /**< Max URL length for HTTP          */
#define EC200_MAX_TOPIC_LEN       (128U)    /**< Max MQTT topic length            */
#define EC200_MAX_PAYLOAD_LEN     (1460U)   /**< Max MQTT/TCP payload length      */
#define EC200_MAX_CONNECTIONS     (12U)     /**< Max simultaneous TCP connections */

/* -------------------------------------------------------------------------
 * Status / return codes
 * ------------------------------------------------------------------------- */
/**
 * @brief Library status codes returned by all API functions.
 */
typedef enum {
    EC200_OK              =  0,   /**< Operation succeeded                     */
    EC200_ERR_TIMEOUT     = -1,   /**< No response within the allowed timeout  */
    EC200_ERR_IO          = -2,   /**< Underlying UART send/receive failure     */
    EC200_ERR_PARSE       = -3,   /**< Response could not be parsed            */
    EC200_ERR_CME         = -4,   /**< Module returned +CME ERROR              */
    EC200_ERR_CMS         = -5,   /**< Module returned +CMS ERROR              */
    EC200_ERR_BUSY        = -6,   /**< Resource is currently in use            */
    EC200_ERR_PARAM       = -7,   /**< Invalid function argument               */
    EC200_ERR_NOT_READY   = -8,   /**< Module not yet initialised              */
    EC200_ERR_OVERFLOW    = -9,   /**< Internal buffer overflow                */
    EC200_ERR_UNSUPPORTED = -10,  /**< Feature not available on this firmware  */
    EC200_ERR_UNKNOWN     = -99,  /**< Unclassified error                      */
} ec200_status_t;

/* -------------------------------------------------------------------------
 * I/O Transport callbacks (user-supplied)
 * ------------------------------------------------------------------------- */
/**
 * @brief Write @p len bytes from @p data to the UART.
 * @return Number of bytes actually written, or <0 on error.
 */
typedef int (*ec200_write_fn)(const uint8_t *data, uint16_t len, void *user_ctx);

/**
 * @brief Read up to @p len bytes into @p data, waiting at most @p timeout_ms.
 * @return Number of bytes actually read, or <0 on error.
 */
typedef int (*ec200_read_fn)(uint8_t *data, uint16_t len, uint32_t timeout_ms, void *user_ctx);

/**
 * @brief Block for @p ms milliseconds.
 */
typedef void (*ec200_delay_fn)(uint32_t ms, void *user_ctx);

/**
 * @brief Optional URC (unsolicited result code) callback invoked by the AT
 *        engine whenever an unrecognised line arrives outside of a command
 *        transaction.
 *
 * @param urc       NUL-terminated URC string (valid only for this call).
 * @param user_ctx  Opaque pointer forwarded from ec200_handle_t::user_ctx.
 */
typedef void (*ec200_urc_handler_fn)(const char *urc, void *user_ctx);

/* -------------------------------------------------------------------------
 * Network registration / status types
 * ------------------------------------------------------------------------- */
/**
 * @brief Registration status values shared by AT+CREG / AT+CGREG / AT+CEREG.
 */
typedef enum {
    EC200_REG_NOT_REGISTERED      = 0,
    EC200_REG_REGISTERED_HOME     = 1,
    EC200_REG_SEARCHING           = 2,
    EC200_REG_DENIED              = 3,
    EC200_REG_UNKNOWN             = 4,
    EC200_REG_REGISTERED_ROAMING  = 5,
} ec200_reg_status_t;

/**
 * @brief Operator selection mode.
 */
typedef enum {
    EC200_COPS_MODE_AUTOMATIC  = 0,
    EC200_COPS_MODE_MANUAL     = 1,
    EC200_COPS_MODE_DEREGISTER = 2,
    EC200_COPS_MODE_SET_FORMAT = 3,
    EC200_COPS_MODE_MANUAL_AUTO= 4,
} ec200_cops_mode_t;

/**
 * @brief Operator name format (AT+COPS format parameter).
 */
typedef enum {
    EC200_COPS_FMT_LONG_NAME  = 0,
    EC200_COPS_FMT_SHORT_NAME = 1,
    EC200_COPS_FMT_NUMERIC    = 2,
} ec200_cops_fmt_t;

/**
 * @brief Access technology reported by the module.
 */
typedef enum {
    EC200_ACT_GSM           = 0,
    EC200_ACT_GSM_COMPACT   = 1,
    EC200_ACT_UTRAN         = 2,
    EC200_ACT_GSM_EGPRS     = 3,
    EC200_ACT_UTRAN_HSDPA   = 4,
    EC200_ACT_UTRAN_HSUPA   = 5,
    EC200_ACT_UTRAN_HSPA    = 6,
    EC200_ACT_LTE           = 7,
    EC200_ACT_UNKNOWN       = 0xFF,
} ec200_act_t;

/**
 * @brief Operator information returned by AT+COPS.
 */
typedef struct {
    ec200_cops_mode_t mode;
    ec200_cops_fmt_t  format;
    char              oper[EC200_MAX_OPERATOR_LEN];
    ec200_act_t       act;
} ec200_operator_info_t;

/**
 * @brief Signal quality returned by AT+CSQ.
 */
typedef struct {
    int8_t  rssi;  /**< RSSI in dBm (-113 to -51, or 0 for unknown)  */
    uint8_t ber;   /**< Bit error rate (0-7, 99 = unknown)            */
    uint8_t rsrp;  /**< LTE RSRP (0-96, 255 = unknown) from AT+QCSQ  */
    int8_t  sinr;  /**< LTE SINR in dB (from AT+QCSQ)                */
} ec200_signal_quality_t;

/* -------------------------------------------------------------------------
 * SIM types
 * ------------------------------------------------------------------------- */
/**
 * @brief SIM PIN state returned by AT+CPIN.
 */
typedef enum {
    EC200_SIM_READY          = 0,
    EC200_SIM_PIN_REQUIRED   = 1,
    EC200_SIM_PUK_REQUIRED   = 2,
    EC200_SIM_PIN2_REQUIRED  = 3,
    EC200_SIM_PUK2_REQUIRED  = 4,
    EC200_SIM_NOT_INSERTED   = 5,
    EC200_SIM_UNKNOWN        = 0xFF,
} ec200_sim_status_t;

/* -------------------------------------------------------------------------
 * SMS types
 * ------------------------------------------------------------------------- */
/**
 * @brief SMS message format.
 */
typedef enum {
    EC200_SMS_FORMAT_PDU  = 0,
    EC200_SMS_FORMAT_TEXT = 1,
} ec200_sms_format_t;

/**
 * @brief SMS message status (for listing).
 */
typedef enum {
    EC200_SMS_STAT_REC_UNREAD = 0,
    EC200_SMS_STAT_REC_READ   = 1,
    EC200_SMS_STAT_STO_UNSENT = 2,
    EC200_SMS_STAT_STO_SENT   = 3,
    EC200_SMS_STAT_ALL        = 4,
} ec200_sms_stat_t;

/**
 * @brief A parsed incoming SMS message.
 */
typedef struct {
    int      index;                              /**< Storage index               */
    ec200_sms_stat_t stat;                       /**< Read / unread state         */
    char     sender[EC200_MAX_PHONE_NUM_LEN];    /**< Originating address         */
    char     timestamp[24];                      /**< Arrival time string         */
    char     text[EC200_MAX_SMS_TEXT_LEN + 1];   /**< Message body (NUL-term.)    */
} ec200_sms_message_t;

/* -------------------------------------------------------------------------
 * PDP / Data context types
 * ------------------------------------------------------------------------- */
/**
 * @brief PDP context type.
 */
typedef enum {
    EC200_PDP_TYPE_IP    = 0,
    EC200_PDP_TYPE_IPV6  = 1,
    EC200_PDP_TYPE_IPV4V6= 2,
} ec200_pdp_type_t;

/**
 * @brief PDP context configuration.
 */
typedef struct {
    uint8_t       cid;                       /**< Context ID (1-16)          */
    ec200_pdp_type_t type;                   /**< IP version                 */
    char          apn[64];                   /**< Access Point Name          */
    char          username[32];              /**< Auth username (may be "")  */
    char          password[32];              /**< Auth password (may be "")  */
    char          ip_addr[EC200_MAX_IP_ADDR_LEN]; /**< Assigned IP (output) */
} ec200_pdp_context_t;

/* -------------------------------------------------------------------------
 * TCP/IP socket types
 * ------------------------------------------------------------------------- */
/**
 * @brief Socket connection type.
 */
typedef enum {
    EC200_SOCK_TCP   = 0,
    EC200_SOCK_UDP   = 1,
    EC200_SOCK_TCP_LISTENER = 2,
    EC200_SOCK_UDP_SERVICE  = 3,
} ec200_sock_type_t;

/**
 * @brief Socket access mode.
 */
typedef enum {
    EC200_ACCESS_BUFFER = 0,  /**< Buffer access mode  */
    EC200_ACCESS_DIRECT = 1,  /**< Direct push mode    */
    EC200_ACCESS_TRANS  = 2,  /**< Transparent mode    */
} ec200_access_mode_t;

/**
 * @brief Per-socket state.
 */
typedef struct {
    int              conn_id;                         /**< Connection ID (0-11)         */
    ec200_sock_type_t type;                           /**< TCP / UDP                    */
    char             remote_host[EC200_MAX_URL_LEN];  /**< Remote host / IP             */
    uint16_t         remote_port;                     /**< Remote port                  */
    bool             connected;                       /**< Connection open flag         */
    uint32_t         bytes_received;                  /**< Total bytes received counter */
} ec200_socket_t;

/* -------------------------------------------------------------------------
 * HTTP types
 * ------------------------------------------------------------------------- */
/**
 * @brief HTTP method.
 */
typedef enum {
    EC200_HTTP_GET    = 0,
    EC200_HTTP_POST   = 1,
    EC200_HTTP_HEAD   = 2,
    EC200_HTTP_DELETE = 3,
    EC200_HTTP_PUT    = 4,
} ec200_http_method_t;

/**
 * @brief HTTP response summary.
 */
typedef struct {
    uint16_t status_code;    /**< HTTP status code (200, 404, …)   */
    uint32_t content_length; /**< Body length in bytes             */
} ec200_http_response_t;

/* -------------------------------------------------------------------------
 * MQTT types
 * ------------------------------------------------------------------------- */
/**
 * @brief MQTT QoS level.
 */
typedef enum {
    EC200_MQTT_QOS0 = 0,
    EC200_MQTT_QOS1 = 1,
    EC200_MQTT_QOS2 = 2,
} ec200_mqtt_qos_t;

/**
 * @brief MQTT connection configuration.
 */
typedef struct {
    char     host[EC200_MAX_URL_LEN];   /**< Broker hostname or IP     */
    uint16_t port;                      /**< Broker port (default 1883)*/
    char     client_id[64];             /**< MQTT client identifier    */
    char     username[64];              /**< Auth username (may be "")  */
    char     password[64];              /**< Auth password (may be "")  */
    uint16_t keep_alive;                /**< Keep-alive interval (sec) */
    bool     clean_session;             /**< Clean session flag        */
    uint8_t  tcp_connect_id;            /**< AT+QMTOPEN context (0-5)  */
    uint8_t  client_idx;                /**< AT+QMTCONN client index   */
} ec200_mqtt_config_t;

/**
 * @brief Received MQTT message.
 */
typedef struct {
    char     topic[EC200_MAX_TOPIC_LEN];   /**< Message topic            */
    uint8_t  payload[EC200_MAX_PAYLOAD_LEN];/**< Raw payload bytes       */
    uint32_t payload_len;                  /**< Payload length in bytes  */
    ec200_mqtt_qos_t qos;                  /**< QoS of the publication   */
} ec200_mqtt_message_t;

/**
 * @brief MQTT message-received callback.
 *
 * @param msg      Received message (valid for the duration of the callback).
 * @param user_ctx Opaque pointer from ec200_handle_t::user_ctx.
 */
typedef void (*ec200_mqtt_msg_fn)(const ec200_mqtt_message_t *msg, void *user_ctx);

/* -------------------------------------------------------------------------
 * GNSS types
 * ------------------------------------------------------------------------- */
/**
 * @brief GNSS fix / location data.
 */
typedef struct {
    bool     fix_valid;       /**< true = position fix obtained      */
    float    latitude;        /**< Latitude in degrees (+N, -S)      */
    float    longitude;       /**< Longitude in degrees (+E, -W)     */
    float    altitude;        /**< Altitude in metres above MSL      */
    float    speed_kmh;       /**< Speed over ground (km/h)          */
    float    course;          /**< Course over ground (degrees)      */
    uint8_t  satellites_used; /**< Satellites used in fix            */
    uint8_t  hdop;            /**< Horizontal dilution of precision  */
    char     utc_time[40];    /**< UTC date/time string              */
} ec200_gnss_location_t;

/* -------------------------------------------------------------------------
 * Power types
 * ------------------------------------------------------------------------- */
/**
 * @brief Functional level for AT+CFUN.
 */
typedef enum {
    EC200_CFUN_MIN    = 0,  /**< Minimum functionality (RF off)    */
    EC200_CFUN_FULL   = 1,  /**< Full functionality                */
    EC200_CFUN_DISABLE_TX = 4, /**< Disable TX only               */
    EC200_CFUN_AIRPLANE = 7,   /**< Airplane mode                 */
} ec200_cfun_t;

/* -------------------------------------------------------------------------
 * Main library handle
 * ------------------------------------------------------------------------- */
/**
 * @brief Library handle.  One instance per physical module.
 *
 * Initialise with ec200_init() before using any other API.
 * All fields except the four callbacks should be treated as opaque.
 */
typedef struct {
    /* --- User-supplied transport layer ----------------------------------- */
    ec200_write_fn       write;       /**< UART write callback (required)   */
    ec200_read_fn        read;        /**< UART read callback  (required)   */
    ec200_delay_fn       delay_ms;    /**< Delay callback      (required)   */
    void                *user_ctx;    /**< Forwarded to all callbacks        */

    /* --- Optional callbacks --------------------------------------------- */
    ec200_urc_handler_fn urc_handler; /**< URC callback (NULL = ignore)     */
    ec200_mqtt_msg_fn    mqtt_msg_cb; /**< MQTT message callback            */

    /* --- Internal buffers (do not access directly) ----------------------- */
    char     _rx_buf[EC200_RX_BUFFER_SIZE];  /**< Raw receive buffer        */
    char     _tx_buf[EC200_TX_BUFFER_SIZE];  /**< Command scratch buffer    */

    /* --- Cached state ---------------------------------------------------- */
    bool     _initialised;            /**< Set by ec200_init()              */
    int      _last_cme_error;         /**< Last +CME ERROR code             */
    int      _last_cms_error;         /**< Last +CMS ERROR code             */
} ec200_handle_t;

#ifdef __cplusplus
}
#endif

#endif /* EC200_TYPES_H */
