# EC200 AT Abstraction Library — API Reference

**Version:** 1.0.0  
**Language:** C99  
**Target:** Quectel EC200U cellular module  
**Include:** `#include "ec200.h"`

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Start](#quick-start)
3. [Types & Constants](#types--constants)
   - [Buffer-Size Constants](#buffer-size-constants)
   - [Status Codes](#status-codes)
   - [Callback Types](#callback-types)
   - [Network Types](#network-types)
   - [SIM Types](#sim-types)
   - [SMS Types](#sms-types)
   - [PDP / Data Types](#pdp--data-types)
   - [TCP/IP Types](#tcpip-types)
   - [HTTP Types](#http-types)
   - [MQTT Types](#mqtt-types)
   - [GNSS Types](#gnss-types)
   - [Power Types](#power-types)
   - [Library Handle](#library-handle)
4. [Core API](#core-api) — `ec200.h`
5. [AT Transport Layer](#at-transport-layer) — `ec200_at.h`
6. [SIM Card Management](#sim-card-management) — `ec200_sim.h`
7. [Network Registration](#network-registration) — `ec200_network.h`
8. [SMS Messaging](#sms-messaging) — `ec200_sms.h`
9. [PDP / Data Connection](#pdp--data-connection) — `ec200_data.h`
10. [TCP/IP Sockets](#tcpip-sockets) — `ec200_tcpip.h`
11. [HTTP Client](#http-client) — `ec200_http.h`
12. [MQTT Client](#mqtt-client) — `ec200_mqtt.h`
13. [GNSS / GPS](#gnss--gps) — `ec200_gnss.h`
14. [Power Management](#power-management) — `ec200_power.h`
15. [Error Handling](#error-handling)

---

## Overview

The EC200 AT Abstraction Library wraps the full Quectel EC200U AT command set
into a clean, type-safe C99 API.  It has **no dependency** on any specific MCU
or RTOS — the user supplies three lightweight callbacks (UART write, UART read,
millisecond delay) and the library handles all command framing, response
parsing, and error propagation.

```
 ┌────────────────────────────────────────┐
 │         Your Application Code          │
 └──────────────────┬─────────────────────┘
                    │  ec200_*.h API
 ┌──────────────────▼─────────────────────┐
 │      EC200 AT Abstraction Library      │
 └──────────────────┬─────────────────────┘
                    │  write / read / delay callbacks
 ┌──────────────────▼─────────────────────┐
 │  Your Platform Wrapper (HAL / RTOS)    │
 └──────────────────┬─────────────────────┘
                    │  UART hardware
 ┌──────────────────▼─────────────────────┐
 │         Quectel EC200U Module          │
 └────────────────────────────────────────┘
```

---

## Quick Start

### 1 — Implement the platform wrapper

```c
#include "ec200.h"

static int my_uart_write(const uint8_t *data, uint16_t len, void *ctx)
{
    // e.g. STM32: return HAL_UART_Transmit(&huart2, data, len, 1000) == HAL_OK ? len : -1;
    return (int)fwrite(data, 1, len, stdout);
}

static int my_uart_read(uint8_t *data, uint16_t len,
                        uint32_t timeout_ms, void *ctx)
{
    // e.g. STM32 DMA ring-buffer: return uart_ring_buf_read(data, len, timeout_ms);
    (void)data; (void)len; (void)timeout_ms; (void)ctx;
    return 0;
}

static void my_delay_ms(uint32_t ms, void *ctx)
{
    // e.g. STM32: HAL_Delay(ms);  FreeRTOS: vTaskDelay(pdMS_TO_TICKS(ms));
    (void)ctx;
}
```

### 2 — Initialise the library

```c
ec200_handle_t modem;
ec200_status_t st = ec200_init(&modem,
                               my_uart_write,
                               my_uart_read,
                               my_delay_ms,
                               NULL /* optional user context */);
if (st != EC200_OK) {
    printf("init failed: %s\n", ec200_status_str(st));
}
ec200_set_echo(&modem, false);
ec200_set_cmee(&modem, 2);   // verbose error strings
```

### 3 — Connect and use the network

```c
// Wait for LTE registration (60 s timeout)
ec200_net_wait_registered(&modem, 60000);

// Activate PDP context
ec200_pdp_context_t pdp = {
    .cid  = 1,
    .type = EC200_PDP_TYPE_IP,
    .apn  = "internet",
};
ec200_data_connect(&modem, &pdp);
printf("IP: %s\n", pdp.ip_addr);

// HTTP GET
ec200_http_set_context(&modem, 1);
ec200_http_set_url(&modem, "http://example.com/api");
ec200_http_response_t resp;
ec200_http_get(&modem, 30000, &resp);

uint8_t body[512];
uint32_t body_len;
ec200_http_read(&modem, body, sizeof(body), &body_len, 10000);
```

---

## Types & Constants

### Buffer-Size Constants

All defined in `ec200_types.h`.

| Constant | Value | Description |
|---|---|---|
| `EC200_RX_BUFFER_SIZE` | 1024 | Receive ring-buffer size (bytes) |
| `EC200_TX_BUFFER_SIZE` | 512 | Transmit scratch-buffer size (bytes) |
| `EC200_MAX_OPERATOR_LEN` | 32 | Max operator name length |
| `EC200_MAX_PHONE_NUM_LEN` | 20 | Max phone-number string length |
| `EC200_MAX_SMS_TEXT_LEN` | 160 | Max SMS text body length |
| `EC200_MAX_IP_ADDR_LEN` | 46 | Max IP address string length |
| `EC200_MAX_IMEI_LEN` | 16 | IMEI string length (+NUL) |
| `EC200_MAX_IMSI_LEN` | 16 | IMSI string length (+NUL) |
| `EC200_MAX_ICCID_LEN` | 21 | ICCID string length (+NUL) |
| `EC200_MAX_FW_VER_LEN` | 64 | Firmware version string length |
| `EC200_MAX_URL_LEN` | 256 | Max URL length for HTTP |
| `EC200_MAX_TOPIC_LEN` | 128 | Max MQTT topic length |
| `EC200_MAX_PAYLOAD_LEN` | 1460 | Max MQTT/TCP payload length |
| `EC200_MAX_CONNECTIONS` | 12 | Max simultaneous TCP connections |

---

### Status Codes

`ec200_status_t` — returned by every API function.

| Value | Code | Meaning |
|---|---|---|
| `0` | `EC200_OK` | Operation succeeded |
| `-1` | `EC200_ERR_TIMEOUT` | No response within the allowed timeout |
| `-2` | `EC200_ERR_IO` | Underlying UART send/receive failure |
| `-3` | `EC200_ERR_PARSE` | Response could not be parsed |
| `-4` | `EC200_ERR_CME` | Module returned +CME ERROR |
| `-5` | `EC200_ERR_CMS` | Module returned +CMS ERROR |
| `-6` | `EC200_ERR_BUSY` | Resource is currently in use |
| `-7` | `EC200_ERR_PARAM` | Invalid function argument |
| `-8` | `EC200_ERR_NOT_READY` | Module not yet initialised |
| `-9` | `EC200_ERR_OVERFLOW` | Internal buffer overflow |
| `-10` | `EC200_ERR_UNSUPPORTED` | Feature not available on this firmware |
| `-99` | `EC200_ERR_UNKNOWN` | Unclassified error |

---

### Callback Types

```c
// Write len bytes to UART; return bytes written or <0 on error
typedef int  (*ec200_write_fn)(const uint8_t *data, uint16_t len, void *user_ctx);

// Read up to len bytes, waiting at most timeout_ms; return bytes read or <0
typedef int  (*ec200_read_fn)(uint8_t *data, uint16_t len,
                               uint32_t timeout_ms, void *user_ctx);

// Block for ms milliseconds
typedef void (*ec200_delay_fn)(uint32_t ms, void *user_ctx);

// URC callback — invoked when an unsolicited line arrives
typedef void (*ec200_urc_handler_fn)(const char *urc, void *user_ctx);

// MQTT received-message callback
typedef void (*ec200_mqtt_msg_fn)(const ec200_mqtt_message_t *msg, void *user_ctx);
```

---

### Network Types

```c
// AT+CREG / AT+CGREG / AT+CEREG registration status
typedef enum {
    EC200_REG_NOT_REGISTERED     = 0,
    EC200_REG_REGISTERED_HOME    = 1,
    EC200_REG_SEARCHING          = 2,
    EC200_REG_DENIED             = 3,
    EC200_REG_UNKNOWN            = 4,
    EC200_REG_REGISTERED_ROAMING = 5,
} ec200_reg_status_t;

// AT+COPS mode
typedef enum {
    EC200_COPS_MODE_AUTOMATIC  = 0,
    EC200_COPS_MODE_MANUAL     = 1,
    EC200_COPS_MODE_DEREGISTER = 2,
    EC200_COPS_MODE_SET_FORMAT = 3,
    EC200_COPS_MODE_MANUAL_AUTO= 4,
} ec200_cops_mode_t;

// AT+COPS format
typedef enum {
    EC200_COPS_FMT_LONG_NAME  = 0,
    EC200_COPS_FMT_SHORT_NAME = 1,
    EC200_COPS_FMT_NUMERIC    = 2,
} ec200_cops_fmt_t;

// Access technology
typedef enum {
    EC200_ACT_GSM        = 0,  EC200_ACT_UTRAN      = 2,
    EC200_ACT_GSM_EGPRS  = 3,  EC200_ACT_LTE        = 7,
    EC200_ACT_UNKNOWN    = 0xFF,
    // ... (see ec200_types.h for full list)
} ec200_act_t;

typedef struct {
    ec200_cops_mode_t mode;
    ec200_cops_fmt_t  format;
    char              oper[EC200_MAX_OPERATOR_LEN];
    ec200_act_t       act;
} ec200_operator_info_t;

typedef struct {
    int8_t  rssi;   // RSSI in dBm (-113 to -51, 0 = unknown)
    uint8_t ber;    // Bit error rate (0-7, 99 = unknown)
    uint8_t rsrp;   // LTE RSRP 0-96, 255 = unknown
    int8_t  sinr;   // LTE SINR in dB
} ec200_signal_quality_t;
```

---

### SIM Types

```c
typedef enum {
    EC200_SIM_READY         = 0,
    EC200_SIM_PIN_REQUIRED  = 1,
    EC200_SIM_PUK_REQUIRED  = 2,
    EC200_SIM_PIN2_REQUIRED = 3,
    EC200_SIM_PUK2_REQUIRED = 4,
    EC200_SIM_NOT_INSERTED  = 5,
    EC200_SIM_UNKNOWN       = 0xFF,
} ec200_sim_status_t;
```

---

### SMS Types

```c
typedef enum { EC200_SMS_FORMAT_PDU = 0, EC200_SMS_FORMAT_TEXT = 1 } ec200_sms_format_t;

typedef enum {
    EC200_SMS_STAT_REC_UNREAD = 0, EC200_SMS_STAT_REC_READ   = 1,
    EC200_SMS_STAT_STO_UNSENT = 2, EC200_SMS_STAT_STO_SENT   = 3,
    EC200_SMS_STAT_ALL        = 4,
} ec200_sms_stat_t;

typedef struct {
    int              index;
    ec200_sms_stat_t stat;
    char             sender[EC200_MAX_PHONE_NUM_LEN];
    char             timestamp[24];
    char             text[EC200_MAX_SMS_TEXT_LEN + 1];
} ec200_sms_message_t;
```

---

### PDP / Data Types

```c
typedef enum {
    EC200_PDP_TYPE_IP    = 0,
    EC200_PDP_TYPE_IPV6  = 1,
    EC200_PDP_TYPE_IPV4V6= 2,
} ec200_pdp_type_t;

typedef struct {
    uint8_t          cid;                          // Context ID (1-16)
    ec200_pdp_type_t type;
    char             apn[64];
    char             username[32];
    char             password[32];
    char             ip_addr[EC200_MAX_IP_ADDR_LEN]; // Assigned IP (output)
} ec200_pdp_context_t;
```

---

### TCP/IP Types

```c
typedef enum {
    EC200_SOCK_TCP          = 0, EC200_SOCK_UDP          = 1,
    EC200_SOCK_TCP_LISTENER = 2, EC200_SOCK_UDP_SERVICE  = 3,
} ec200_sock_type_t;

typedef enum {
    EC200_ACCESS_BUFFER = 0, EC200_ACCESS_DIRECT = 1, EC200_ACCESS_TRANS = 2,
} ec200_access_mode_t;

typedef struct {
    int               conn_id;
    ec200_sock_type_t type;
    char              remote_host[EC200_MAX_URL_LEN];
    uint16_t          remote_port;
    bool              connected;
    uint32_t          bytes_received;
} ec200_socket_t;
```

---

### HTTP Types

```c
typedef enum {
    EC200_HTTP_GET    = 0, EC200_HTTP_POST   = 1, EC200_HTTP_HEAD   = 2,
    EC200_HTTP_DELETE = 3, EC200_HTTP_PUT    = 4,
} ec200_http_method_t;

typedef struct {
    uint16_t status_code;    // e.g. 200, 404
    uint32_t content_length;
} ec200_http_response_t;
```

---

### MQTT Types

```c
typedef enum {
    EC200_MQTT_QOS0 = 0, EC200_MQTT_QOS1 = 1, EC200_MQTT_QOS2 = 2,
} ec200_mqtt_qos_t;

typedef struct {
    char     host[EC200_MAX_URL_LEN];
    uint16_t port;                    // Default 1883
    char     client_id[64];
    char     username[64];
    char     password[64];
    uint16_t keep_alive;              // Seconds
    bool     clean_session;
    uint8_t  tcp_connect_id;          // AT+QMTOPEN context (0-5)
    uint8_t  client_idx;              // AT+QMTCONN client index
} ec200_mqtt_config_t;

typedef struct {
    char     topic[EC200_MAX_TOPIC_LEN];
    uint8_t  payload[EC200_MAX_PAYLOAD_LEN];
    uint32_t payload_len;
    ec200_mqtt_qos_t qos;
} ec200_mqtt_message_t;
```

---

### GNSS Types

```c
typedef struct {
    bool    fix_valid;
    float   latitude;       // Degrees (+N, -S)
    float   longitude;      // Degrees (+E, -W)
    float   altitude;       // Metres above MSL
    float   speed_kmh;
    float   course;         // Degrees
    uint8_t satellites_used;
    uint8_t hdop;
    char    utc_time[40];
} ec200_gnss_location_t;
```

---

### Power Types

```c
typedef enum {
    EC200_CFUN_MIN       = 0, // Minimum functionality (RF off)
    EC200_CFUN_FULL      = 1, // Full functionality
    EC200_CFUN_DISABLE_TX= 4, // Disable TX only
    EC200_CFUN_AIRPLANE  = 7, // Airplane mode
} ec200_cfun_t;
```

---

### Library Handle

```c
typedef struct {
    // User-supplied callbacks (required)
    ec200_write_fn       write;
    ec200_read_fn        read;
    ec200_delay_fn       delay_ms;
    void                *user_ctx;

    // Optional callbacks
    ec200_urc_handler_fn urc_handler;  // NULL = ignore URCs
    ec200_mqtt_msg_fn    mqtt_msg_cb;  // NULL = ignore MQTT messages

    // Internal (do not access directly)
    char  _rx_buf[EC200_RX_BUFFER_SIZE];
    char  _tx_buf[EC200_TX_BUFFER_SIZE];
    bool  _initialised;
    int   _last_cme_error;
    int   _last_cms_error;
} ec200_handle_t;
```

---

## Core API

**Header:** `ec200.h`  
**Group:** `EC200_Core`

### Library Version Macros

```c
#define EC200_LIB_VERSION_MAJOR  1
#define EC200_LIB_VERSION_MINOR  0
#define EC200_LIB_VERSION_PATCH  0
```

---

### `ec200_init`

```c
ec200_status_t ec200_init(ec200_handle_t *h,
                          ec200_write_fn  write_fn,
                          ec200_read_fn   read_fn,
                          ec200_delay_fn  delay_fn,
                          void           *user_ctx);
```

Initialise the library handle.  Must be called once before any other function.

| Parameter | Direction | Description |
|---|---|---|
| `h` | in | Pointer to a caller-allocated `ec200_handle_t` |
| `write_fn` | in | Platform UART write callback (must not be NULL) |
| `read_fn` | in | Platform UART read callback (must not be NULL) |
| `delay_fn` | in | Platform delay callback (must not be NULL) |
| `user_ctx` | in | Opaque pointer forwarded to every callback (may be NULL) |

**Returns:** `EC200_OK`, `EC200_ERR_PARAM` (NULL callback), or `EC200_ERR_TIMEOUT`.

---

### `ec200_check_at`

```c
ec200_status_t ec200_check_at(ec200_handle_t *h);
```

Send a plain `AT` and verify the module replies `OK`.  Useful as a keep-alive.

**Returns:** `EC200_OK` or `EC200_ERR_TIMEOUT`.

---

### `ec200_get_imei`

```c
ec200_status_t ec200_get_imei(ec200_handle_t *h,
                              char           *imei,
                              size_t          imei_sz);
```

Read the module IMEI via `AT+GSN`.

| Parameter | Direction | Description |
|---|---|---|
| `imei` | out | Buffer ≥ `EC200_MAX_IMEI_LEN` bytes |
| `imei_sz` | in | Size of `imei` buffer |

**Returns:** `EC200_OK` or an error code.

---

### `ec200_get_fw_version`

```c
ec200_status_t ec200_get_fw_version(ec200_handle_t *h,
                                    char           *ver,
                                    size_t          ver_sz);
```

Read firmware revision via `AT+GMR`.

---

### `ec200_get_module_info`

```c
ec200_status_t ec200_get_module_info(ec200_handle_t *h,
                                     char           *info,
                                     size_t          info_sz);
```

Read manufacturer identification via `ATI`.

---

### `ec200_set_echo`

```c
ec200_status_t ec200_set_echo(ec200_handle_t *h, bool enable);
```

Enable (`ATE1`) or disable (`ATE0`) command echo.

---

### `ec200_set_cmee`

```c
ec200_status_t ec200_set_cmee(ec200_handle_t *h, uint8_t mode);
```

Set verbose error reporting via `AT+CMEE=<mode>`.  
`mode`: 0 = disabled, 1 = numeric, 2 = verbose text.

---

### `ec200_set_urc_handler`

```c
void ec200_set_urc_handler(ec200_handle_t       *h,
                           ec200_urc_handler_fn  handler);
```

Register a URC callback.  Pass `NULL` to unregister.

---

### `ec200_status_str`

```c
const char *ec200_status_str(ec200_status_t status);
```

Convert a status code to a human-readable string (never returns NULL).

---

## AT Transport Layer

**Header:** `ec200_at.h`  
**Group:** `EC200_AT`

### Timeout Constants

| Constant | Value (ms) | Use |
|---|---|---|
| `EC200_AT_TIMEOUT_DEFAULT` | 1000 | Generic AT commands |
| `EC200_AT_TIMEOUT_SHORT` | 300 | Fast queries |
| `EC200_AT_TIMEOUT_LONG` | 10000 | Network attach / socket open |
| `EC200_AT_TIMEOUT_HTTP` | 30000 | HTTP GET/POST |
| `EC200_AT_TIMEOUT_COPS` | 120000 | Operator search |

---

### `ec200_at_send`

```c
ec200_status_t ec200_at_send(ec200_handle_t *h,
                             const char     *cmd,
                             char           *resp_buf,
                             size_t          resp_buf_sz,
                             uint32_t        timeout_ms);
```

Send a raw AT command string and wait for `OK` / `ERROR` / `+CME ERROR` / `+CMS ERROR`.

| Parameter | Description |
|---|---|
| `cmd` | NUL-terminated command string without `\r\n` |
| `resp_buf` | Optional buffer for raw response; pass `NULL` to discard |
| `resp_buf_sz` | Size of `resp_buf` (ignored when `NULL`) |
| `timeout_ms` | Maximum wait in milliseconds |

**Returns:** `EC200_OK`, `EC200_ERR_TIMEOUT`, `EC200_ERR_CME`, `EC200_ERR_CMS`, `EC200_ERR_IO`, or `EC200_ERR_OVERFLOW`.

---

### `ec200_at_send_wait`

```c
ec200_status_t ec200_at_send_wait(ec200_handle_t *h,
                                  const char     *cmd,
                                  const char     *expected_prefix,
                                  char           *resp_buf,
                                  size_t          resp_buf_sz,
                                  uint32_t        timeout_ms);
```

Like `ec200_at_send` but returns `EC200_OK` as soon as a line starting with
`expected_prefix` (e.g. `"+CPIN:"`) is received.

---

### `ec200_at_write_raw`

```c
ec200_status_t ec200_at_write_raw(ec200_handle_t *h,
                                  const uint8_t  *data,
                                  uint16_t        len);
```

Write raw bytes to UART (bypasses AT framing — for transparent/data mode).

---

### `ec200_at_read_raw`

```c
ec200_status_t ec200_at_read_raw(ec200_handle_t *h,
                                 uint8_t        *buf,
                                 uint16_t        len,
                                 uint32_t        timeout_ms,
                                 uint16_t       *bytes_read);
```

Read raw bytes from UART.  `bytes_read` is set to the actual byte count.

---

### `ec200_at_poll_urc`

```c
ec200_status_t ec200_at_poll_urc(ec200_handle_t *h, uint32_t timeout_ms);
```

Poll for unsolicited result codes.  Call periodically from a main loop or RTOS
task.  Dispatches lines to `h->urc_handler`.  Pass `timeout_ms = 0` for a
non-blocking poll.

---

### `ec200_at_last_cme_error` / `ec200_at_last_cms_error`

```c
int ec200_at_last_cme_error(const ec200_handle_t *h);
int ec200_at_last_cms_error(const ec200_handle_t *h);
```

Retrieve the last raw `+CME ERROR` or `+CMS ERROR` code.  Returns `-1` if the
last error was not of that type.

---

## SIM Card Management

**Header:** `ec200_sim.h`  
**Group:** `EC200_SIM`

### `ec200_sim_get_status`

```c
ec200_status_t ec200_sim_get_status(ec200_handle_t     *h,
                                    ec200_sim_status_t *status);
```

Query SIM PIN state via `AT+CPIN?`.

---

### `ec200_sim_enter_pin`

```c
ec200_status_t ec200_sim_enter_pin(ec200_handle_t *h, const char *pin);
```

Enter the SIM PIN via `AT+CPIN=<pin>`.

---

### `ec200_sim_get_imsi`

```c
ec200_status_t ec200_sim_get_imsi(ec200_handle_t *h,
                                  char           *imsi,
                                  size_t          imsi_sz);
```

Read IMSI via `AT+CIMI`.  Buffer must be ≥ `EC200_MAX_IMSI_LEN`.

---

### `ec200_sim_get_iccid`

```c
ec200_status_t ec200_sim_get_iccid(ec200_handle_t *h,
                                   char           *iccid,
                                   size_t          iccid_sz);
```

Read ICCID via `AT+CCID`.  Buffer must be ≥ `EC200_MAX_ICCID_LEN`.

---

## Network Registration

**Header:** `ec200_network.h`  
**Group:** `EC200_Network`

### `ec200_net_get_creg`

```c
ec200_status_t ec200_net_get_creg(ec200_handle_t     *h,
                                  ec200_reg_status_t *status);
```

Query CS (circuit-switched) registration via `AT+CREG?`.

---

### `ec200_net_get_cgreg`

```c
ec200_status_t ec200_net_get_cgreg(ec200_handle_t     *h,
                                   ec200_reg_status_t *status);
```

Query GPRS registration via `AT+CGREG?`.

---

### `ec200_net_get_cereg`

```c
ec200_status_t ec200_net_get_cereg(ec200_handle_t     *h,
                                   ec200_reg_status_t *status);
```

Query LTE/EPS registration via `AT+CEREG?`.

---

### `ec200_net_get_signal`

```c
ec200_status_t ec200_net_get_signal(ec200_handle_t         *h,
                                    ec200_signal_quality_t *sq);
```

Query signal quality (`rssi`, `ber`) via `AT+CSQ`.

---

### `ec200_net_get_signal_ext`

```c
ec200_status_t ec200_net_get_signal_ext(ec200_handle_t         *h,
                                        ec200_signal_quality_t *sq);
```

Query extended LTE signal quality (`rssi`, `ber`, `rsrp`, `sinr`) via `AT+QCSQ`.

---

### `ec200_net_get_operator`

```c
ec200_status_t ec200_net_get_operator(ec200_handle_t        *h,
                                      ec200_operator_info_t *info);
```

Read current operator via `AT+COPS?`.

---

### `ec200_net_set_operator`

```c
ec200_status_t ec200_net_set_operator(ec200_handle_t    *h,
                                      ec200_cops_mode_t  mode,
                                      ec200_cops_fmt_t   format,
                                      const char        *oper,
                                      ec200_act_t        act);
```

Select operator manually or set automatic selection via `AT+COPS=…`.

---

### `ec200_net_wait_registered`

```c
ec200_status_t ec200_net_wait_registered(ec200_handle_t *h,
                                         uint32_t        timeout_ms);
```

Block until the device is registered on the network (HOME or ROAMING).
Polls `AT+CEREG` every 2 s.

**Returns:** `EC200_OK` when registered, `EC200_ERR_TIMEOUT` if not registered
within `timeout_ms`.

---

## SMS Messaging

**Header:** `ec200_sms.h`  
**Group:** `EC200_SMS`

### `ec200_sms_set_format`

```c
ec200_status_t ec200_sms_set_format(ec200_handle_t     *h,
                                    ec200_sms_format_t  format);
```

Set SMS format via `AT+CMGF`.  Use `EC200_SMS_FORMAT_TEXT` for text mode.

---

### `ec200_sms_send`

```c
ec200_status_t ec200_sms_send(ec200_handle_t *h,
                              const char     *number,
                              const char     *text);
```

Send SMS in text mode via `AT+CMGS`.  Text mode must be enabled first.

---

### `ec200_sms_read`

```c
ec200_status_t ec200_sms_read(ec200_handle_t      *h,
                              int                  index,
                              ec200_sms_message_t *msg);
```

Read a single message by 1-based storage index via `AT+CMGR`.

---

### `ec200_sms_list`

```c
ec200_status_t ec200_sms_list(ec200_handle_t      *h,
                              ec200_sms_stat_t      stat,
                              ec200_sms_message_t  *msgs,
                              uint8_t               max_msgs,
                              uint8_t              *count_out);
```

List messages matching a status filter via `AT+CMGL`.

---

### `ec200_sms_delete`

```c
ec200_status_t ec200_sms_delete(ec200_handle_t *h, int index);
```

Delete message at 1-based `index` via `AT+CMGD`.

---

### `ec200_sms_delete_all`

```c
ec200_status_t ec200_sms_delete_all(ec200_handle_t *h, uint8_t flag);
```

Bulk delete via `AT+CMGD` with flag:  
`1` = all read, `2` = read + sent, `3` = read + sent + unsent, `4` = all.

---

## PDP / Data Connection

**Header:** `ec200_data.h`  
**Group:** `EC200_Data`

### `ec200_data_set_pdp`

```c
ec200_status_t ec200_data_set_pdp(ec200_handle_t            *h,
                                  const ec200_pdp_context_t *ctx);
```

Configure a PDP context via `AT+CGDCONT`.  `ctx->cid` must be 1–16.

---

### `ec200_data_activate`

```c
ec200_status_t ec200_data_activate(ec200_handle_t *h, uint8_t cid);
```

Activate PDP context `cid` via `AT+CGACT=1,<cid>`.

---

### `ec200_data_deactivate`

```c
ec200_status_t ec200_data_deactivate(ec200_handle_t *h, uint8_t cid);
```

Deactivate PDP context `cid` via `AT+CGACT=0,<cid>`.

---

### `ec200_data_get_ip`

```c
ec200_status_t ec200_data_get_ip(ec200_handle_t *h,
                                 uint8_t         cid,
                                 char           *ip_buf,
                                 size_t          ip_buf_sz);
```

Query the IP address assigned to `cid` via `AT+CGPADDR`.

---

### `ec200_data_connect`

```c
ec200_status_t ec200_data_connect(ec200_handle_t      *h,
                                  ec200_pdp_context_t *ctx);
```

Convenience helper: calls `ec200_data_set_pdp` → `ec200_data_activate` →
`ec200_data_get_ip` in sequence.  On success, `ctx->ip_addr` is populated.

---

## TCP/IP Sockets

**Header:** `ec200_tcpip.h`  
**Group:** `EC200_TCPIP`

### `ec200_tcp_open`

```c
ec200_status_t ec200_tcp_open(ec200_handle_t      *h,
                              uint8_t              ctx_id,
                              uint8_t              conn_id,
                              ec200_sock_type_t    type,
                              const char          *host,
                              uint16_t             port,
                              ec200_access_mode_t  access_mode);
```

Open a TCP or UDP socket via `AT+QIOPEN`.

| Parameter | Description |
|---|---|
| `ctx_id` | PDP context ID (0–16) |
| `conn_id` | Connection ID (0 to `EC200_MAX_CONNECTIONS-1`) |
| `type` | `EC200_SOCK_TCP` or `EC200_SOCK_UDP` |
| `access_mode` | Buffer / direct / transparent mode |

---

### `ec200_tcp_send`

```c
ec200_status_t ec200_tcp_send(ec200_handle_t *h,
                              uint8_t         conn_id,
                              const uint8_t  *data,
                              uint16_t        len);
```

Send data via `AT+QISEND`.

---

### `ec200_tcp_recv`

```c
ec200_status_t ec200_tcp_recv(ec200_handle_t *h,
                              uint8_t         conn_id,
                              uint8_t        *buf,
                              uint16_t        max_len,
                              uint16_t       *bytes_read,
                              uint32_t        timeout_ms);
```

Receive data via `AT+QIRD`.  `bytes_read` contains actual bytes received.

---

### `ec200_tcp_close`

```c
ec200_status_t ec200_tcp_close(ec200_handle_t *h, uint8_t conn_id);
```

Close connection via `AT+QICLOSE`.

---

### `ec200_tcp_get_state`

```c
ec200_status_t ec200_tcp_get_state(ec200_handle_t *h,
                                   uint8_t         conn_id,
                                   ec200_socket_t *sock);
```

Query connection state via `AT+QISTATE`.

---

### `ec200_tcp_bytes_available`

```c
ec200_status_t ec200_tcp_bytes_available(ec200_handle_t *h,
                                         uint8_t         conn_id,
                                         uint32_t       *bytes_avail);
```

Query bytes waiting in receive buffer via `AT+QIRD=<conn_id>,0`.

---

## HTTP Client

**Header:** `ec200_http.h`  
**Group:** `EC200_HTTP`

### `ec200_http_set_context`

```c
ec200_status_t ec200_http_set_context(ec200_handle_t *h, uint8_t ctx_id);
```

Bind the HTTP session to a PDP context via `AT+QHTTPCFG`.

---

### `ec200_http_set_url`

```c
ec200_status_t ec200_http_set_url(ec200_handle_t *h, const char *url);
```

Set request URL (max `EC200_MAX_URL_LEN`) via `AT+QHTTPURL`.

---

### `ec200_http_get`

```c
ec200_status_t ec200_http_get(ec200_handle_t        *h,
                              uint32_t               timeout_ms,
                              ec200_http_response_t *resp);
```

Perform HTTP GET via `AT+QHTTPGET`.  `resp` is populated with status code and
content length.  URL must be set first with `ec200_http_set_url()`.

---

### `ec200_http_post`

```c
ec200_status_t ec200_http_post(ec200_handle_t        *h,
                               const uint8_t         *body,
                               uint32_t               body_len,
                               const char            *content_type,
                               uint32_t               timeout_ms,
                               ec200_http_response_t *resp);
```

Perform HTTP POST via `AT+QHTTPPOST`.

---

### `ec200_http_read`

```c
ec200_status_t ec200_http_read(ec200_handle_t *h,
                               uint8_t        *buf,
                               size_t          buf_sz,
                               uint32_t       *bytes_read,
                               uint32_t        timeout_ms);
```

Read response body via `AT+QHTTPREAD`.  Call after a successful GET or POST.

---

### `ec200_http_stop`

```c
ec200_status_t ec200_http_stop(ec200_handle_t *h);
```

Release HTTP session via `AT+QHTTPSTOP`.

---

## MQTT Client

**Header:** `ec200_mqtt.h`  
**Group:** `EC200_MQTT`

### `ec200_mqtt_open`

```c
ec200_status_t ec200_mqtt_open(ec200_handle_t            *h,
                               const ec200_mqtt_config_t *cfg);
```

Open TCP connection to the MQTT broker via `AT+QMTOPEN`.

---

### `ec200_mqtt_connect`

```c
ec200_status_t ec200_mqtt_connect(ec200_handle_t            *h,
                                  const ec200_mqtt_config_t *cfg);
```

Send MQTT CONNECT packet via `AT+QMTCONN`.  Must follow `ec200_mqtt_open()`.

---

### `ec200_mqtt_disconnect`

```c
ec200_status_t ec200_mqtt_disconnect(ec200_handle_t *h, uint8_t client_idx);
```

Send MQTT DISCONNECT via `AT+QMTDISC`.

---

### `ec200_mqtt_close`

```c
ec200_status_t ec200_mqtt_close(ec200_handle_t *h, uint8_t tcp_connect_id);
```

Close underlying TCP connection via `AT+QMTCLOSE`.

---

### `ec200_mqtt_subscribe`

```c
ec200_status_t ec200_mqtt_subscribe(ec200_handle_t  *h,
                                    uint8_t          client_idx,
                                    uint16_t         msg_id,
                                    const char      *topic,
                                    ec200_mqtt_qos_t qos);
```

Subscribe to a topic via `AT+QMTSUB`.

---

### `ec200_mqtt_unsubscribe`

```c
ec200_status_t ec200_mqtt_unsubscribe(ec200_handle_t *h,
                                      uint8_t         client_idx,
                                      uint16_t        msg_id,
                                      const char     *topic);
```

Unsubscribe from a topic via `AT+QMTUNS`.

---

### `ec200_mqtt_publish`

```c
ec200_status_t ec200_mqtt_publish(ec200_handle_t  *h,
                                  uint8_t          client_idx,
                                  uint16_t         msg_id,
                                  ec200_mqtt_qos_t qos,
                                  bool             retain,
                                  const char      *topic,
                                  const uint8_t   *payload,
                                  uint32_t         payload_len);
```

Publish a message via `AT+QMTPUB`.  Use `msg_id = 0` for QoS 0.

---

### `ec200_mqtt_set_message_cb`

```c
void ec200_mqtt_set_message_cb(ec200_handle_t   *h,
                               ec200_mqtt_msg_fn callback);
```

Register a callback for incoming `+QMTRECV` URCs.  Pass `NULL` to unregister.

---

## GNSS / GPS

**Header:** `ec200_gnss.h`  
**Group:** `EC200_GNSS`

### `ec200_gnss_start`

```c
ec200_status_t ec200_gnss_start(ec200_handle_t *h);
```

Turn GNSS engine on via `AT+QGPS=1`.

---

### `ec200_gnss_stop`

```c
ec200_status_t ec200_gnss_stop(ec200_handle_t *h);
```

Turn GNSS engine off via `AT+QGPSEND`.

---

### `ec200_gnss_get_status`

```c
ec200_status_t ec200_gnss_get_status(ec200_handle_t *h, bool *enabled);
```

Query GNSS engine state via `AT+QGPS?`.

---

### `ec200_gnss_get_location`

```c
ec200_status_t ec200_gnss_get_location(ec200_handle_t        *h,
                                       ec200_gnss_location_t *loc);
```

Read current location via `AT+QGPSLOC`.  Returns `EC200_ERR_CME` if no fix.

---

### `ec200_gnss_set_nmea_output`

```c
ec200_status_t ec200_gnss_set_nmea_output(ec200_handle_t *h,
                                          uint8_t         nmea_types);
```

Configure NMEA output sentences via `AT+QGPSCFG`.

| Bit | Sentence |
|---|---|
| `0x01` | GGA |
| `0x02` | RMC |
| `0x04` | GSV |
| `0x08` | GSA |
| `0x10` | VTG |

---

## Power Management

**Header:** `ec200_power.h`  
**Group:** `EC200_Power`

### `ec200_power_set_cfun`

```c
ec200_status_t ec200_power_set_cfun(ec200_handle_t *h,
                                    ec200_cfun_t    level,
                                    bool            reset);
```

Set functional level via `AT+CFUN`.  If `reset` is `true`, the module is reset
before applying the new level.

---

### `ec200_power_get_cfun`

```c
ec200_status_t ec200_power_get_cfun(ec200_handle_t *h, ec200_cfun_t *level);
```

Query current functional level via `AT+CFUN?`.

---

### `ec200_power_down`

```c
ec200_status_t ec200_power_down(ec200_handle_t *h, bool normal);
```

Power the module down via `AT+QPOWD`.  `normal = true` for graceful shutdown.

---

### `ec200_power_set_sleep`

```c
ec200_status_t ec200_power_set_sleep(ec200_handle_t *h, bool enable);
```

Enable or disable slow-clock / sleep mode via `AT+QSCLK`.

---

### `ec200_power_reset`

```c
ec200_status_t ec200_power_reset(ec200_handle_t *h);
```

Reset the module via `AT+CFUN=1,1`.  Convenience wrapper around
`ec200_power_set_cfun(h, EC200_CFUN_FULL, true)`.

---

## Error Handling

All API functions return `ec200_status_t`.

```c
ec200_status_t st = ec200_some_function(&modem, ...);
if (st != EC200_OK) {
    printf("Error: %s\n", ec200_status_str(st));

    if (st == EC200_ERR_CME)
        printf("CME code: %d\n", ec200_at_last_cme_error(&modem));

    if (st == EC200_ERR_CMS)
        printf("CMS code: %d\n", ec200_at_last_cms_error(&modem));
}
```

### Error Code Quick Reference

| Code | Value | Meaning |
|---|---|---|
| `EC200_OK` | 0 | Success |
| `EC200_ERR_TIMEOUT` | -1 | No response within timeout |
| `EC200_ERR_IO` | -2 | UART failure |
| `EC200_ERR_PARSE` | -3 | Response parsing failed |
| `EC200_ERR_CME` | -4 | `+CME ERROR` from module |
| `EC200_ERR_CMS` | -5 | `+CMS ERROR` from module |
| `EC200_ERR_BUSY` | -6 | Resource in use |
| `EC200_ERR_PARAM` | -7 | Invalid argument |
| `EC200_ERR_NOT_READY` | -8 | Library not initialised |
| `EC200_ERR_OVERFLOW` | -9 | Buffer overflow |
| `EC200_ERR_UNSUPPORTED` | -10 | Feature unavailable |
| `EC200_ERR_UNKNOWN` | -99 | Unclassified error |
