# EC200 AT Abstraction Library — API Reference

**Language:** C99 &nbsp;·&nbsp; **Target:** Quectel EC200U cellular module &nbsp;·&nbsp; **Include:** `#include "ec200.h"`

This is the curated reference. The **exhaustive per-function documentation
lives in the headers** and can be browsed as HTML: run `doxygen Doxyfile`
from the repository root and open `docs/html/index.html`.

---

## Table of Contents

1. [Architecture](#architecture)
2. [Transport Callbacks](#transport-callbacks)
3. [Constants](#constants)
4. [Status Codes](#status-codes)
5. [The AT Engine](#the-at-engine) — `ec200_at.h`
6. [Core API](#core-api) — `ec200.h`
7. [SIM](#sim) — `ec200_sim.h`
8. [Network](#network) — `ec200_network.h`
9. [SMS](#sms) — `ec200_sms.h`
10. [PDP / Data](#pdp--data) — `ec200_data.h`
11. [TCP/IP](#tcpip) — `ec200_tcpip.h`
12. [HTTP](#http) — `ec200_http.h`
13. [MQTT](#mqtt) — `ec200_mqtt.h`
14. [GNSS](#gnss) — `ec200_gnss.h`
15. [Power](#power) — `ec200_power.h`
16. [Error Handling](#error-handling)
17. [Threading Model](#threading-model)

---

## Architecture

```
 ┌────────────────────────────────────────┐
 │         Your Application Code          │
 └──────────────────┬─────────────────────┘
                    │  ec200_*.h API
 ┌──────────────────▼─────────────────────┐
 │   Domain modules (sim/net/sms/tcp/…)   │
 └──────────────────┬─────────────────────┘
                    │  transaction primitives
 ┌──────────────────▼─────────────────────┐
 │        AT engine (ec200_at.h)          │
 │  line assembly · URC dispatch · waits  │
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

Key properties:

- **No dynamic allocation.** All state lives in the caller-allocated
  `ec200_handle_t` (~2.6 KB).
- **Complete lines only.** Bytes are assembled into the handle's persistent
  buffer; a line interrupted by a timeout stays buffered and is finished on
  the next call — parsers never see partial data.
- **Bounded blocking.** Every call is limited by its timeout budget *and* a
  per-transaction cap of `EC200_AT_MAX_LINES` lines, so a URC storm or a
  byte-trickling UART cannot hang the caller.
- **URC-safe.** Prefixes registered via `ec200_at_register_urc()` are
  dispatched even when the line arrives in the middle of another command.
- `ec200_init()` probes the module and disables command echo (`ATE0`).

## Transport Callbacks

```c
typedef int  (*ec200_write_fn)(const uint8_t *data, uint16_t len, void *user_ctx);
typedef int  (*ec200_read_fn) (uint8_t *data, uint16_t len,
                               uint32_t timeout_ms, void *user_ctx);
typedef void (*ec200_delay_fn)(uint32_t ms, void *user_ctx);
typedef void (*ec200_urc_handler_fn)(const char *urc, void *user_ctx);
```

| Callback | Return contract |
|---|---|
| `write` | Bytes accepted (short writes are retried by the library), `< 0` fatal error |
| `read` | `> 0` bytes read · **`0` = timeout, no data** · `< 0` = **fatal fault only** |
| `delay_ms` | — |

Returning `0` (not `-1`) from `read` on a timeout is required: it is how the
library distinguishes `EC200_ERR_TIMEOUT` from `EC200_ERR_IO`. The read
callback should block until at least one byte arrives or the timeout expires
(ISR/DMA-fed ring buffer recommended).

## Constants

| Constant | Value | Meaning |
|---|---|---|
| `EC200_RX_BUFFER_SIZE` | 2048 | Line-assembly buffer (sized for a full-payload `+QMTRECV` line) |
| `EC200_TX_BUFFER_SIZE` | 512 | Command scratch buffer |
| `EC200_MAX_SMS_TEXT_LEN` | 160 | Max SMS body |
| `EC200_MAX_PAYLOAD_LEN` | 1460 | Max MQTT/TCP payload |
| `EC200_MAX_CONNECTIONS` | 12 | TCP connection IDs 0–11 |
| `EC200_MAX_URC_HANDLERS` | 8 | Registered URC prefix slots |
| `EC200_MAX_IMEI_LEN` / `IMSI` / `ICCID` | 16 / 16 / 21 | ID string buffers (incl. NUL) |
| `EC200_MAX_URL_LEN` / `TOPIC` | 256 / 128 | HTTP URL / MQTT topic |
| `EC200_AT_TIMEOUT_DEFAULT` / `SHORT` / `LONG` / `HTTP` / `COPS` | 1000 / 300 / 10000 / 30000 / 120000 ms | Default deadlines |
| `EC200_AT_MAX_LINES` | 64 | Per-transaction line cap (storm guard) |
| `EC200_SIGNAL_UNKNOWN` | −32768 | "Not available" sentinel in `ec200_signal_quality_t` |

## Status Codes

Every API function returns `ec200_status_t`:

| Code | Value | Meaning |
|---|---|---|
| `EC200_OK` | 0 | Success |
| `EC200_ERR_TIMEOUT` | −1 | No response within the deadline |
| `EC200_ERR_IO` | −2 | Fatal UART send/receive failure |
| `EC200_ERR_PARSE` | −3 | Response could not be parsed |
| `EC200_ERR_CME` | −4 | Module returned `+CME ERROR` (see `ec200_at_last_cme_error()`) |
| `EC200_ERR_CMS` | −5 | Module returned `+CMS ERROR` (see `ec200_at_last_cms_error()`) |
| `EC200_ERR_BUSY` | −6 | Resource in use |
| `EC200_ERR_PARAM` | −7 | Invalid argument |
| `EC200_ERR_NOT_READY` | −8 | Handle not initialised |
| `EC200_ERR_OVERFLOW` | −9 | Buffer too small / line cap tripped |
| `EC200_ERR_UNSUPPORTED` | −10 | Feature unavailable on this firmware |
| `EC200_ERR_MODULE` | −11 | Plain `ERROR` or nonzero command result code |
| `EC200_ERR_UNKNOWN` | −99 | Unclassified |

`ec200_status_str()` converts any code to a human-readable string.

## The AT Engine

`ec200_at.h` — used internally by every module; also public for custom AT
work. Each primitive matches one response shape of the EC200:

| Response shape | Primitive |
|---|---|
| `cmd → lines… → OK` | `ec200_at_send()` |
| `cmd → +PREFIX line → OK` | `ec200_at_send_wait()` |
| `cmd → +PREFIX line → raw data` | `ec200_at_send_expect()` + `ec200_at_read_exact()` + `ec200_at_wait_final()` |
| `cmd → OK → … later → +PREFIX urc` | `ec200_at_send_await_urc()` |
| `cmd → ">" data prompt` | `ec200_at_send_prompt()` |
| receive-only wait (after Ctrl-Z / raw data) | `ec200_at_wait_prefix()` / `ec200_at_wait_final()` |

```c
ec200_status_t ec200_at_send(ec200_handle_t *h, const char *cmd,
                             char *resp_buf, size_t resp_buf_sz,
                             uint32_t timeout_ms);
ec200_status_t ec200_at_send_wait(ec200_handle_t *h, const char *cmd,
                                  const char *expected_prefix,
                                  char *resp_buf, size_t resp_buf_sz,
                                  uint32_t timeout_ms);
ec200_status_t ec200_at_send_expect(ec200_handle_t *h, const char *cmd,
                                    const char *expected_prefix,
                                    char *resp_buf, size_t resp_buf_sz,
                                    uint32_t timeout_ms);
ec200_status_t ec200_at_send_await_urc(ec200_handle_t *h, const char *cmd,
                                       const char *urc_prefix,
                                       char *urc_buf, size_t urc_buf_sz,
                                       uint32_t ok_timeout,
                                       uint32_t urc_timeout);
ec200_status_t ec200_at_send_prompt(ec200_handle_t *h, const char *cmd,
                                    uint32_t timeout_ms);
ec200_status_t ec200_at_wait_prefix(ec200_handle_t *h, const char *prefix,
                                    char *resp_buf, size_t resp_buf_sz,
                                    uint32_t timeout_ms);
ec200_status_t ec200_at_wait_final(ec200_handle_t *h, uint32_t timeout_ms);
```

Raw I/O and utilities:

```c
ec200_status_t ec200_at_write_raw(ec200_handle_t *h, const uint8_t *data,
                                  uint16_t len);              /* retries short writes */
ec200_status_t ec200_at_read_raw(ec200_handle_t *h, uint8_t *buf,
                                 uint16_t len, uint32_t timeout_ms,
                                 uint16_t *bytes_read);       /* single attempt */
ec200_status_t ec200_at_read_exact(ec200_handle_t *h, uint8_t *buf,
                                   uint16_t len, uint32_t timeout_ms,
                                   uint16_t *bytes_read);     /* loops until len */
ec200_status_t ec200_at_parse_int_field(const char *line, unsigned index,
                                        int *value);  /* quote-aware CSV field */
```

URC handling:

```c
ec200_status_t ec200_at_register_urc(ec200_handle_t *h, const char *prefix,
                                     ec200_urc_handler_fn handler, void *ctx);
ec200_status_t ec200_at_unregister_urc(ec200_handle_t *h, const char *prefix);
ec200_status_t ec200_at_poll_urc(ec200_handle_t *h, uint32_t timeout_ms);
int            ec200_at_last_cme_error(const ec200_handle_t *h);
int            ec200_at_last_cms_error(const ec200_handle_t *h);
```

Call `ec200_at_poll_urc()` regularly (main loop or modem task; `0` = poll
without blocking — returns `EC200_OK` also when nothing is pending).
Registered prefixes must be string literals (or otherwise outlive the
registration). Unmatched URC lines go to the fallback handler set with
`ec200_set_urc_handler()`.

## Core API

```c
ec200_status_t ec200_init(ec200_handle_t *h, ec200_write_fn write_fn,
                          ec200_read_fn read_fn, ec200_delay_fn delay_fn,
                          void *user_ctx);        /* probes AT, sends ATE0 */
ec200_status_t ec200_check_at(ec200_handle_t *h); /* "AT" keep-alive */
ec200_status_t ec200_get_imei(ec200_handle_t *h, char *imei, size_t imei_sz);
ec200_status_t ec200_get_fw_version(ec200_handle_t *h, char *ver, size_t ver_sz);
ec200_status_t ec200_get_module_info(ec200_handle_t *h, char *info, size_t info_sz);
ec200_status_t ec200_set_echo(ec200_handle_t *h, bool enable);
ec200_status_t ec200_set_cmee(ec200_handle_t *h, uint8_t mode);   /* 0/1/2 */
void           ec200_set_urc_handler(ec200_handle_t *h, ec200_urc_handler_fn handler);
const char    *ec200_status_str(ec200_status_t status);
```

ID getters return `EC200_ERR_OVERFLOW` rather than silently truncating.

## SIM

```c
ec200_status_t ec200_sim_get_status(ec200_handle_t *h, ec200_sim_status_t *status);
ec200_status_t ec200_sim_enter_pin(ec200_handle_t *h, const char *pin);
ec200_status_t ec200_sim_get_imsi(ec200_handle_t *h, char *imsi, size_t imsi_sz);
ec200_status_t ec200_sim_get_iccid(ec200_handle_t *h, char *iccid, size_t iccid_sz);
```

`ec200_sim_status_t`: `READY`, `PIN_REQUIRED`, `PUK_REQUIRED`,
`PIN2_REQUIRED`, `PUK2_REQUIRED`, `NOT_INSERTED`, `UNKNOWN`.

## Network

```c
ec200_status_t ec200_net_get_creg (ec200_handle_t *h, ec200_reg_status_t *status);
ec200_status_t ec200_net_get_cgreg(ec200_handle_t *h, ec200_reg_status_t *status);
ec200_status_t ec200_net_get_cereg(ec200_handle_t *h, ec200_reg_status_t *status);
ec200_status_t ec200_net_get_signal    (ec200_handle_t *h, ec200_signal_quality_t *sq);
ec200_status_t ec200_net_get_signal_ext(ec200_handle_t *h, ec200_signal_quality_t *sq);
ec200_status_t ec200_net_get_operator(ec200_handle_t *h, ec200_operator_info_t *info);
ec200_status_t ec200_net_set_operator(ec200_handle_t *h, ec200_cops_mode_t mode,
                                      ec200_cops_fmt_t format, const char *oper,
                                      ec200_act_t act);
ec200_status_t ec200_net_wait_registered(ec200_handle_t *h, uint32_t timeout_ms);
```

```c
typedef struct {
    int16_t rssi;  /* dBm (-113..-51), 0 if unknown                  */
    uint8_t ber;   /* 0-7, 99 = unknown                              */
    int16_t rsrp;  /* dBm (negative); EC200_SIGNAL_UNKNOWN if absent */
    int16_t sinr;  /* raw AT+QCSQ value; EC200_SIGNAL_UNKNOWN        */
} ec200_signal_quality_t;
```

`get_signal` uses `AT+CSQ`; `get_signal_ext` uses `AT+QCSQ` (real dBm values,
falls back to CSQ if unsupported). Registration parsing handles both the
read form and URC-mode-2 responses with extended location fields.
`wait_registered` polls CEREG every 2 s until home/roaming registration.

## SMS

```c
ec200_status_t ec200_sms_set_format(ec200_handle_t *h, ec200_sms_format_t format);
ec200_status_t ec200_sms_send(ec200_handle_t *h, const char *number, const char *text);
ec200_status_t ec200_sms_read(ec200_handle_t *h, int index, ec200_sms_message_t *msg);
ec200_status_t ec200_sms_list(ec200_handle_t *h, ec200_sms_stat_t stat,
                              ec200_sms_message_t *msgs, uint8_t max_msgs,
                              uint8_t *count_out);
ec200_status_t ec200_sms_delete(ec200_handle_t *h, int index);
ec200_status_t ec200_sms_delete_all(ec200_handle_t *h, uint8_t flag); /* 1-4 */
```

`sms_send` validates length (≤ 160) and rejects text containing Ctrl-Z
(`0x1A`), which would submit the message early. Text mode (`AT+CMGF=1`)
must be selected first.

## PDP / Data

```c
ec200_status_t ec200_data_set_pdp   (ec200_handle_t *h, const ec200_pdp_context_t *ctx);
ec200_status_t ec200_data_activate  (ec200_handle_t *h, uint8_t cid);   /* 1-16 */
ec200_status_t ec200_data_deactivate(ec200_handle_t *h, uint8_t cid);
ec200_status_t ec200_data_get_ip(ec200_handle_t *h, uint8_t cid,
                                 char *ip_buf, size_t ip_buf_sz);
ec200_status_t ec200_data_connect(ec200_handle_t *h, ec200_pdp_context_t *ctx);
```

When `ctx->username` is non-empty, `set_pdp` also issues
`AT+QICSGP` with PAP/CHAP authentication so the credentials take effect.
`data_connect` = set_pdp → activate → get_ip (result in `ctx->ip_addr`).

## TCP/IP

```c
ec200_status_t ec200_tcp_open(ec200_handle_t *h, uint8_t ctx_id, uint8_t conn_id,
                              ec200_sock_type_t type, const char *host,
                              uint16_t port, ec200_access_mode_t access_mode);
ec200_status_t ec200_tcp_send(ec200_handle_t *h, uint8_t conn_id,
                              const uint8_t *data, uint16_t len);
ec200_status_t ec200_tcp_recv(ec200_handle_t *h, uint8_t conn_id, uint8_t *buf,
                              uint16_t max_len, uint16_t *bytes_read,
                              uint32_t timeout_ms);
ec200_status_t ec200_tcp_close(ec200_handle_t *h, uint8_t conn_id);
ec200_status_t ec200_tcp_get_state(ec200_handle_t *h, uint8_t conn_id,
                                   ec200_socket_t *sock);
ec200_status_t ec200_tcp_bytes_available(ec200_handle_t *h, uint8_t conn_id,
                                         uint32_t *bytes_avail);
```

`tcp_open` is asynchronous on the module (`OK` first, `+QIOPEN` URC later) —
handled internally; a nonzero open result returns `EC200_ERR_MODULE`.
`tcp_get_state` uses `AT+QISTATE=1,<conn_id>` and reports
`connected` from the numeric socket state.

## HTTP

```c
ec200_status_t ec200_http_set_context(ec200_handle_t *h, uint8_t ctx_id); /* 1-16 */
ec200_status_t ec200_http_set_url(ec200_handle_t *h, const char *url);
ec200_status_t ec200_http_get(ec200_handle_t *h, uint32_t timeout_ms,
                              ec200_http_response_t *resp);
ec200_status_t ec200_http_post(ec200_handle_t *h, const uint8_t *body,
                               uint32_t body_len, const char *content_type,
                               uint32_t timeout_ms, ec200_http_response_t *resp);
ec200_status_t ec200_http_read(ec200_handle_t *h, uint8_t *buf, size_t buf_sz,
                               uint32_t *bytes_read, uint32_t timeout_ms);
ec200_status_t ec200_http_stop(ec200_handle_t *h);
```

GET/POST are asynchronous (`OK`, then `+QHTTPGET:`/`+QHTTPPOST:` URC) —
handled internally. `http_read` streams the body between `CONNECT` and the
trailing `OK`; if the buffer fills, the remainder is drained and
`EC200_ERR_OVERFLOW` is returned with `bytes_read == buf_sz` (reserve one
byte yourself if you want to NUL-terminate).

## MQTT

```c
ec200_status_t ec200_mqtt_open      (ec200_handle_t *h, const ec200_mqtt_config_t *cfg);
ec200_status_t ec200_mqtt_connect   (ec200_handle_t *h, const ec200_mqtt_config_t *cfg);
ec200_status_t ec200_mqtt_disconnect(ec200_handle_t *h, uint8_t client_idx);
ec200_status_t ec200_mqtt_close     (ec200_handle_t *h, uint8_t tcp_connect_id);
ec200_status_t ec200_mqtt_subscribe  (ec200_handle_t *h, uint8_t client_idx,
                                      uint16_t msg_id, const char *topic,
                                      ec200_mqtt_qos_t qos);
ec200_status_t ec200_mqtt_unsubscribe(ec200_handle_t *h, uint8_t client_idx,
                                      uint16_t msg_id, const char *topic);
ec200_status_t ec200_mqtt_publish(ec200_handle_t *h, uint8_t client_idx,
                                  uint16_t msg_id, ec200_mqtt_qos_t qos,
                                  bool retain, const char *topic,
                                  const uint8_t *payload, uint32_t payload_len);
void ec200_mqtt_set_message_cb(ec200_handle_t *h, ec200_mqtt_msg_fn callback);
```

All QMT commands are asynchronous (`OK`, then `+QMTxxx:` result URC) —
handled internally. `publish` uses the length-parameterised `AT+QMTPUBEX`,
so **binary payloads containing `0x1A` are safe**; oversized payloads are
rejected (`EC200_ERR_PARAM`), never truncated. QoS > 0 requires
`msg_id != 0`. Setting a message callback registers a `+QMTRECV:` URC
handler, so incoming publications are delivered even when they arrive while
another command is in flight (poll via `ec200_at_poll_urc()`).

## GNSS

```c
ec200_status_t ec200_gnss_start(ec200_handle_t *h);
ec200_status_t ec200_gnss_stop(ec200_handle_t *h);
ec200_status_t ec200_gnss_get_status(ec200_handle_t *h, bool *enabled);
ec200_status_t ec200_gnss_get_location(ec200_handle_t *h, ec200_gnss_location_t *loc);
ec200_status_t ec200_gnss_set_nmea_output(ec200_handle_t *h, uint8_t nmea_types);
```

`get_location` issues `AT+QGPSLOC=2` (decimal degrees). Before a fix is
acquired the module answers `+CME ERROR: 516` → `EC200_ERR_CME`.

## Power

```c
ec200_status_t ec200_power_set_cfun(ec200_handle_t *h, ec200_cfun_t level, bool reset);
ec200_status_t ec200_power_get_cfun(ec200_handle_t *h, ec200_cfun_t *level);
ec200_status_t ec200_power_down(ec200_handle_t *h, bool normal);
ec200_status_t ec200_power_set_sleep(ec200_handle_t *h, bool enable);
ec200_status_t ec200_power_reset(ec200_handle_t *h);   /* CFUN=1,1 */
```

PWRKEY/reset **GPIO sequencing is the platform wrapper's responsibility** —
each board wires it differently.

## Error Handling

```c
ec200_status_t st = ec200_sms_send(&modem, "+123", "hi");
if (st == EC200_ERR_CMS) {
    int code = ec200_at_last_cms_error(&modem);  /* e.g. 302 */
}
printf("%s\n", ec200_status_str(st));
```

The CME/CMS error state is reset at the start of every command, so it always
refers to the most recent transaction. A plain `ERROR` line — and nonzero
command-specific result codes (QMT/QIOPEN/QHTTP `<err>` fields) — map to
`EC200_ERR_MODULE`.

## Threading Model

The library is **not thread-safe**: all calls on one handle — including
`ec200_at_poll_urc()` — must come from a single task/thread. In RTOS
designs, dedicate a modem task that owns the handle; other tasks submit
requests through a queue rather than sharing the handle behind a mutex.

---

*Verified against the source at the time of writing; the headers remain the
authoritative reference (`doxygen Doxyfile` → `docs/html/`).*
