# ec200-at-abstraction

An platform-independent abstraction layer for **Quectel EC200U series** cellular modules.

## Overview

This library wraps the full Quectel EC200U AT command set into a clean, type-safe C99 API.
It handles all response parsing, data structuring, and error propagation internally.
The library has **no dependency on any specific MCU or RTOS** — the user provides three
lightweight callbacks (UART write, UART read, millisecond delay) through a small wrapper,
and the library takes care of the rest.

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

## Repository Structure

```
ec200-at-abstraction/
├── include/
│   ├── ec200.h          – Main public header (include this)
│   ├── ec200_types.h    – Common types, enums, structs
│   ├── ec200_at.h       – Low-level AT transport layer
│   ├── ec200_sim.h      – SIM card management
│   ├── ec200_network.h  – Network registration & operators
│   ├── ec200_sms.h      – SMS send / receive
│   ├── ec200_data.h     – PDP context / data connection
│   ├── ec200_tcpip.h    – TCP/UDP sockets (AT+QI*)
│   ├── ec200_http.h     – HTTP client (AT+QHTTP*)
│   ├── ec200_mqtt.h     – MQTT client (AT+QMT*)
│   ├── ec200_gnss.h     – GNSS/GPS (AT+QGPS*)
│   └── ec200_power.h    – Power management
├── src/                 – Library implementation files
├── examples/
│   └── wrapper_template.c  – Platform wrapper template
├── tests/
│   └── test_ec200.c     – Host-side unit tests (no hardware needed)
└── CMakeLists.txt
```

## Quick Start

### 1. Implement the platform wrapper

Copy `examples/wrapper_template.c` into your project and implement the three callbacks:

```c
#include "ec200.h"

/* Write bytes to the EC200 UART */
static int my_uart_write(const uint8_t *data, uint16_t len, void *ctx) {
    return HAL_UART_Transmit(&huart2, data, len, 1000) == HAL_OK ? len : -1;
}

/* Read bytes from the EC200 UART with timeout */
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

### 3. Use the API

```c
/* Disable echo, enable verbose errors */
ec200_set_echo(&modem, false);
ec200_set_cmee(&modem, 2);

/* Read IMEI */
char imei[EC200_MAX_IMEI_LEN];
ec200_get_imei(&modem, imei, sizeof(imei));

/* Wait for network registration */
ec200_net_wait_registered(&modem, 60000 /* 60 s */);

/* Open a data connection */
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
ec200_http_read(&modem, body, sizeof(body), &body_len, 10000);

/* Send SMS */
ec200_sms_set_format(&modem, EC200_SMS_FORMAT_TEXT);
ec200_sms_send(&modem, "+1234567890", "Hello!");

/* GNSS fix */
ec200_gnss_start(&modem);
ec200_gnss_location_t loc;
ec200_gnss_get_location(&modem, &loc);
printf("Lat: %.6f  Lon: %.6f\n", loc.latitude, loc.longitude);
```

## API Modules

| Header | Description |
|---|---|
| `ec200.h` | Init, IMEI, firmware, echo, CMEE, status strings |
| `ec200_sim.h` | PIN status, enter PIN, IMSI, ICCID |
| `ec200_network.h` | CREG/CGREG/CEREG, CSQ/QCSQ, COPS, wait-for-register |
| `ec200_sms.h` | Set format, send, read, list, delete |
| `ec200_data.h` | CGDCONT, CGACT, CGPADDR, convenience connect helper |
| `ec200_tcpip.h` | QIOPEN, QISEND, QIRD, QICLOSE, QISTATE |
| `ec200_http.h` | QHTTPCFG, QHTTPURL, QHTTPGET, QHTTPPOST, QHTTPREAD |
| `ec200_mqtt.h` | QMTOPEN, QMTCONN, QMTSUB, QMTPUB, QMTDISC, message callback |
| `ec200_gnss.h` | QGPS start/stop/status, QGPSLOC, QGPSCFG NMEA types |
| `ec200_power.h` | CFUN get/set/reset, QPOWD, QSCLK |
| `ec200_at.h` | Raw AT send, raw UART read/write, URC poll |

## Error Handling

All API functions return `ec200_status_t`:

| Code | Meaning |
|---|---|
| `EC200_OK` | Success |
| `EC200_ERR_TIMEOUT` | No response within the timeout |
| `EC200_ERR_IO` | UART send/receive failure |
| `EC200_ERR_PARSE` | Response could not be parsed |
| `EC200_ERR_CME` | `+CME ERROR` from the module |
| `EC200_ERR_CMS` | `+CMS ERROR` from the module |
| `EC200_ERR_PARAM` | Invalid argument |
| `EC200_ERR_NOT_READY` | Library not initialised |
| `EC200_ERR_OVERFLOW` | Internal buffer overflow |

Use `ec200_at_last_cme_error()` / `ec200_at_last_cms_error()` to retrieve the raw error code after a `EC200_ERR_CME` / `EC200_ERR_CMS` return.

## Building

### CMake (recommended)

```sh
mkdir build && cd build
cmake ..
cmake --build .
ctest           # run unit tests
```

### Embedded project

Add all files in `src/` to your build system and add `include/` to the include path.
No external dependencies beyond a standard C99 compiler.

## Running Tests

The test suite runs entirely on the host — no EC200 hardware needed.
A loopback transport pre-seeds canned module responses to exercise all
parsing and error-handling paths.

```sh
cd build
./test_ec200
```

Expected output:
```
EC200 AT Abstraction Library - Unit Tests
==========================================
  status_str                                         PASS
  init_null_params                                   PASS
  ...
  urc_dispatch                                       PASS

==========================================
Results: 26 / 26 tests passed
```

## License

MIT — see individual file headers.
 
