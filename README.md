# ec200-at-abstraction

A platform-independent abstraction layer for **Quectel EC200U series** cellular modules.

## Overview

This library wraps the Quectel EC200U AT command set into a clean, type-safe C99 API.
It handles response parsing, URC dispatch, data structuring, and error propagation
internally. The library has **no dependency on any specific MCU or RTOS** — the user
provides three lightweight callbacks (UART write, UART read, millisecond delay)
through a small wrapper, and the library takes care of the rest.

```
 ┌────────────────────────────────────────┐
 │         Your Application Code          │
 └──────────────────┬─────────────────────┘
                    │  ec200_*.h API
 ┌──────────────────▼─────────────────────┐
 │      EC200 AT Abstraction Library      │
 │  (ec200.h / sim / network / sms / …)  │
 └──────────────────┬─────────────────────┘
                    │  write / read / delay callbacks
 ┌──────────────────▼─────────────────────┐
 │  Your Platform Wrapper (HAL / RTOS)    │
 │  (examples/wrapper_template.c)         │
 └──────────────────┬─────────────────────┘
                    │  UART hardware
 ┌──────────────────▼─────────────────────┐
 │         Quectel EC200U Module          │
 └────────────────────────────────────────┘
```

### Engine design

The AT engine (`ec200_at.c`) models the response shapes the EC200 actually
produces, so higher modules never re-implement transport logic:

| Shape                                   | Primitive                  |
|-----------------------------------------|----------------------------|
| `cmd → lines… → OK`                     | `ec200_at_send()`          |
| `cmd → +PREFIX line → OK`               | `ec200_at_send_wait()`     |
| `cmd → +PREFIX line → raw data`         | `ec200_at_send_expect()` + `ec200_at_read_exact()` + `ec200_at_wait_final()` |
| `cmd → OK → … later → +PREFIX urc`      | `ec200_at_send_await_urc()`|
| `cmd → ">" data prompt`                 | `ec200_at_send_prompt()`   |
| receive-only wait (after Ctrl-Z / data) | `ec200_at_wait_prefix()` / `ec200_at_wait_final()` |

Key properties:

- **Complete lines only** — partial lines interrupted by a timeout stay
  buffered in the handle and are finished on the next call; a parser never
  sees an unterminated string.
- **Bounded blocking** — every call is limited by its timeout budget *and* a
  per-transaction line cap, so a URC storm or byte-trickling UART cannot hang
  the caller.
- **URC-safe** — prefixes registered with `ec200_at_register_urc()` are
  dispatched even when the line arrives in the middle of another command
  (MQTT messages use this internally).
- **No dynamic allocation, ~2.6 KB RAM per handle, no deep stack frames.**
- Echo is disabled (`ATE0`) automatically during `ec200_init()`.

## Repository Structure

```
ec200-at-abstraction/
├── include/             – Public headers (include ec200.h)
├── src/                 – Library implementation files
├── examples/
│   └── wrapper_template.c  – Platform wrapper template
├── tests/
│   ├── test_at_engine.c        – AT engine unit tests
│   ├── test_modules.c          – Core/SIM/network/data/power/GNSS tests
│   ├── test_protocols.c        – TCP/MQTT/HTTP/SMS flow tests
│   ├── test_transport_cmock.c  – CMock transport-contract tests
│   └── support/                – Scripted loopback + mock interface
└── CMakeLists.txt
```

## Quick Start

### 1. Implement the platform wrapper

Copy `examples/wrapper_template.c` into your project and implement the three
callbacks. **The read callback's return contract matters:**

```c
/* Write bytes to the EC200 UART.
 * Return bytes accepted (short writes are retried), or <0 on fatal error. */
static int my_uart_write(const uint8_t *data, uint16_t len, void *ctx) {
    return HAL_UART_Transmit(&huart2, data, len, 1000) == HAL_OK ? len : -1;
}

/* Read bytes from the EC200 UART.
 * Return >0 bytes read, 0 on TIMEOUT (no data), or <0 on FATAL error only.
 * Returning 0 (not -1) on timeout lets the library distinguish
 * EC200_ERR_TIMEOUT from EC200_ERR_IO. */
static int my_uart_read(uint8_t *data, uint16_t len,
                        uint32_t timeout_ms, void *ctx) {
    return uart_ring_buf_read(data, len, timeout_ms);
}

/* Millisecond delay */
static void my_delay_ms(uint32_t ms, void *ctx) {
    HAL_Delay(ms);
}
```

### 2. Initialise the library

```c
ec200_handle_t modem;

ec200_status_t st = ec200_init(&modem,
                               my_uart_write,
                               my_uart_read,
                               my_delay_ms,
                               NULL /* optional user context */);
if (st != EC200_OK) {
    // handle error
}
```

`ec200_init()` probes the module and disables command echo.

### 3. Use the API

```c
/* Enable verbose errors */
ec200_set_cmee(&modem, 2);

/* Read IMEI */
char imei[EC200_MAX_IMEI_LEN];
ec200_get_imei(&modem, imei, sizeof(imei));

/* Wait for network registration */
ec200_net_wait_registered(&modem, 60000 /* 60 s */);

/* Open a data connection (username/password trigger AT+QICSGP auth) */
ec200_pdp_context_t pdp = {
    .cid = 1,
    .type = EC200_PDP_TYPE_IP,
    .apn  = "internet",
};
ec200_data_connect(&modem, &pdp);
printf("IP: %s\n", pdp.ip_addr);

/* HTTP GET */
ec200_http_set_context(&modem, 1);
ec200_http_set_url(&modem, "http://example.com/api/data");
ec200_http_response_t resp;
ec200_http_get(&modem, 30000, &resp);

uint8_t body[512];
uint32_t body_len;
ec200_http_read(&modem, body, sizeof(body) - 1, &body_len, 10000);
body[body_len] = '\0';

/* Send SMS */
ec200_sms_set_format(&modem, EC200_SMS_FORMAT_TEXT);
ec200_sms_send(&modem, "+1234567890", "Hello!");

/* MQTT publish (binary-safe: uses AT+QMTPUBEX) */
ec200_mqtt_publish(&modem, 0, 1, EC200_MQTT_QOS1, false,
                   "sensors/t1", payload, payload_len);

/* GNSS fix */
ec200_gnss_start(&modem);
ec200_gnss_location_t loc;
ec200_gnss_get_location(&modem, &loc);
printf("Lat: %.6f  Lon: %.6f\n", loc.latitude, loc.longitude);

/* Poll URCs from your main loop */
ec200_at_poll_urc(&modem, 0);
```

## API Modules

| Header | Description |
|---|---|
| `ec200.h` | Init, IMEI, firmware, echo, CMEE, status strings |
| `ec200_sim.h` | PIN status, enter PIN, IMSI, ICCID |
| `ec200_network.h` | CREG/CGREG/CEREG, CSQ/QCSQ, COPS, wait-for-register |
| `ec200_sms.h` | Set format, send, read, list, delete |
| `ec200_data.h` | CGDCONT, QICSGP auth, CGACT, CGPADDR, connect helper |
| `ec200_tcpip.h` | QIOPEN, QISEND, QIRD, QICLOSE, QISTATE |
| `ec200_http.h` | QHTTPCFG, QHTTPURL, QHTTPGET, QHTTPPOST, QHTTPREAD |
| `ec200_mqtt.h` | QMTOPEN, QMTCONN, QMTSUB, QMTPUBEX, QMTDISC, message callback |
| `ec200_gnss.h` | QGPS start/stop/status, QGPSLOC, QGPSCFG NMEA types |
| `ec200_power.h` | CFUN get/set/reset, QPOWD, QSCLK |
| `ec200_ppp.h` | PPP dial-up control plane: dial, escape (+++), resume, hangup |
| `ec200_file.h` | Modem UFS: upload/delete/list/size/storage (cert storage) |
| `ec200_ssl.h` | TLS context config (QSSLCFG): version, cipher, seclevel, certs |
| `ec200_ssl_socket.h` | TLS client sockets (QSSLOPEN/SEND/RECV/CLOSE) |
| `ec200_at.h` | AT transaction primitives, raw I/O, URC registry |

## TLS (HTTPS / MQTTS / secure sockets)

Secure transport is a three-step setup — upload certs, configure an SSL
context, then point a protocol at it:

```c
/* 1. Upload the CA (and, for mutual TLS, client cert+key) to the modem FS */
ec200_file_upload(&modem, "ca.pem", ca_pem, ca_len, NULL);

/* 2. Configure an SSL context */
ec200_ssl_config_t ssl = {
    .ctx_id      = 2,
    .version     = EC200_SSL_VER_TLS1_2,
    .ciphersuite = EC200_SSL_CIPHER_ALL,
    .seclevel    = EC200_SSL_SECLEVEL_SERVER,   /* verify the server */
    .cacert      = "ca.pem",
};
ec200_ssl_configure(&modem, &ssl);

/* 3a. HTTPS */
ec200_http_set_ssl_context(&modem, 2);
ec200_http_set_url(&modem, "https://example.com/api");   /* then GET/POST */

/* 3b. MQTTS */
ec200_mqtt_config_t mq = { .host="broker", .port=8883,
                           .use_tls=true, .ssl_ctx_id=2, /* ... */ };
ec200_mqtt_open(&modem, &mq);

/* 3c. Raw TLS socket */
ec200_ssl_socket_open(&modem, 1 /*pdp*/, 2 /*ssl*/, 0 /*conn*/,
                      "host", 443);
```

Security levels: `SECLEVEL_NONE` (encrypt only), `SECLEVEL_SERVER`
(verify server against your CA), `SECLEVEL_MUTUAL` (also present a client
cert+key). Certs live in the modem's filesystem and are referenced by name.

## PPP

The library also covers the PPP **control plane** — the AT side of a dial-up
data session:

```c
ec200_data_set_pdp(&modem, &pdp);       /* configure the context first     */
ec200_ppp_dial(&modem, 1);              /* ATD*99***1# → CONNECT           */
/* UART now carries PPP frames: hand it to your PPP stack (e.g. lwIP
 * PPPoS / esp_netif on ESP-IDF).  All AT APIs return EC200_ERR_BUSY
 * until the session is escaped. */
ec200_ppp_escape(&modem);               /* +++ (guard timing handled)      */
ec200_ppp_resume(&modem);               /* ATO → CONNECT                   */
ec200_ppp_disconnect(&modem);           /* escape (if needed) + ATH        */
```

The PPP **protocol itself** is deliberately out of scope — that is the host
network stack's job. CMUX (simultaneous AT + PPP) is not supported.

## Error Handling

All API functions return `ec200_status_t`:

| Code | Meaning |
|---|---|
| `EC200_OK` | Success |
| `EC200_ERR_TIMEOUT` | No response within the timeout |
| `EC200_ERR_IO` | Fatal UART send/receive failure |
| `EC200_ERR_PARSE` | Response could not be parsed |
| `EC200_ERR_CME` | `+CME ERROR` from the module |
| `EC200_ERR_CMS` | `+CMS ERROR` from the module |
| `EC200_ERR_MODULE` | Plain `ERROR` or nonzero command result code |
| `EC200_ERR_PARAM` | Invalid argument |
| `EC200_ERR_NOT_READY` | Library not initialised |
| `EC200_ERR_OVERFLOW` | Buffer too small / line-storm guard tripped |

Use `ec200_at_last_cme_error()` / `ec200_at_last_cms_error()` to retrieve the
raw error code after an `EC200_ERR_CME` / `EC200_ERR_CMS` return. The error
state is reset at the start of every command, so it always refers to the most
recent transaction.

## Building

### CMake (recommended)

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build      # run unit tests
```

Build options:

| Option | Default | Effect |
|---|---|---|
| `EC200_BUILD_TESTS` | `ON` | Build the Unity/CMock test suites |
| `EC200_WERROR` | `ON` | Treat warnings as errors |
| `EC200_COVERAGE` | `OFF` | Instrument for gcov (GCC only) |
| `EC200_ANALYZER` | `OFF` | Compile with GCC `-fanalyzer` |

The library is compiled with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
-Wdouble-promotion -Wundef -Wstrict-prototypes -Werror`.

### ESP-IDF project

The repository doubles as an ESP-IDF component (any IDF version — the
library is pure C99 with no IDF API usage). Add it to your project's
`main/idf_component.yml`:

```yaml
dependencies:
  ec200-at-abstraction:
    git: "https://github.com/Banishgupta123/ec200-at-abstraction.git"
    version: "v1.1.0"
```

The IDF Component Manager fetches it on the next `idf.py build`; then just
`#include "ec200.h"`. A complete demo project — including a UART wrapper
whose driver semantics match the library's read-callback contract exactly —
is in [examples/esp_idf/](examples/esp_idf/):

```sh
cd examples/esp_idf
idf.py set-target esp32s3
idf.py build flash monitor
```

Alternatively, add the repo as a git submodule under your project's
`components/` directory.

### Other embedded projects

Add all files in `src/` to your build system and add `include/` to the include
path. No external dependencies beyond a standard C99 compiler; no dynamic
allocation; the library is not thread-safe (one task per handle).

## Testing

The test suite runs entirely on the host — no EC200 hardware needed — using
[Unity](https://github.com/ThrowTheSwitch/Unity) and
[CMock](https://github.com/ThrowTheSwitch/CMock) (fetched automatically by
CMake; CMock's generator needs Ruby on the path).

Two complementary transports drive the library:

- a **scripted loopback** that records every byte the library writes and
  feeds canned module responses on matching commands — verifying both the
  exact AT commands sent *and* the response parsing, including regression
  tests for asynchronous OK-then-URC flows, URCs arriving mid-command,
  stream desynchronisation, and binary payload integrity;
- **CMock expectation mocks** of the transport callbacks, pinning the
  engine's call contract (short-write retries, backpressure delays,
  timeout-vs-fatal-error reads).

```sh
ctest --test-dir build --output-on-failure
```

### Coverage (gcov + gcovr)

```sh
cmake -S . -B build -DEC200_COVERAGE=ON
cmake --build build
ctest --test-dir build
cmake --build build --target coverage   # writes build/coverage/index.html
```

The suite currently holds **100% line, function, and branch coverage** of
`src/`. A small number of deliberately unreachable safety-net arms
(invariant guards documented in the code) are excluded from branch counting
via `GCOVR_EXCL_BR_LINE` markers.

### Static analysis

```sh
cmake --build build --target cppcheck             # cppcheck over src/
cmake -S . -B build-analyze -DEC200_ANALYZER=ON   # GCC -fanalyzer
cmake --build build-analyze --target ec200
```

Both analyzers run clean on the current sources.

## Documentation

- [docs/api.md](docs/api.md) — curated API reference (types, contracts, per-module functions)
- Full per-function documentation is generated from the headers:
  `doxygen Doxyfile` → open `docs/html/index.html` (builds warning-free)

## License

MIT — see individual file headers.
