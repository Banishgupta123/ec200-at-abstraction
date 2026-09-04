# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] — 2026-09-05

Major version because of the **Breaking** changes below.

### Breaking

- **`ec200_cfun_t` renumbered.** `EC200_CFUN_AIRPLANE` changed value from `7`
  to `4`, and `EC200_CFUN_DISABLE_TX` (which was `4`) was removed.

  The EC200U rejects `AT+CFUN=7` with `+CME ERROR`; the module's RF-off state
  is `4`. `EC200_CFUN_AIRPLANE` is retained as a deprecated alias of the new
  `EC200_CFUN_RF_OFF`.

  *Migration:* code using `EC200_CFUN_DISABLE_TX` no longer compiles — use
  `EC200_CFUN_RF_OFF`. Code using `EC200_CFUN_AIRPLANE` still compiles but now
  emits `AT+CFUN=4` instead of `AT+CFUN=7`; this is a silent behaviour change.
  A `switch` listing both `EC200_CFUN_AIRPLANE` and `EC200_CFUN_RF_OFF` will
  now fail to compile with a duplicate-case error.

- **`ec200_data_connect()` no longer always sends `AT+CGDCONT`.** It now probes
  the context address first and returns `EC200_OK` immediately if the context
  is already up, so `ctx->apn`, `ctx->type` and PAP/CHAP credentials are not
  applied in that case. See the warning in `ec200_data.h`. On LTE, cid 1 is
  typically already active on the carrier's attach APN before any AT command
  is issued, and re-defining a live context fails on the module.

  *Migration:* if you require a specific APN, call `ec200_data_deactivate()`
  first, or drive `ec200_data_set_pdp()` → `ec200_data_activate()` directly.

- **`ec200_http_post()` takes a `ec200_http_content_type_t`, not a string.**
  The EC200U's `AT+QHTTPCFG="contenttype"` takes a numeric index (0-4), not a
  MIME string. The content type is now always sent; previously passing `NULL`
  left the module's current setting alone.

- **Over-long string arguments are rejected instead of truncated.** Functions
  taking a host, URL, filename or operator name now return `EC200_ERR_PARAM`
  rather than silently truncating into a malformed AT command.

### Added

- TLS / secure transport: HTTPS, MQTTS, TLS client sockets (`ec200_ssl.h`,
  `ec200_ssl_socket.h`) and modem UFS file storage for certificates
  (`ec200_file.h`).
- Network diagnostics: DNS resolution, ping, network clock and NTP sync
  (`ec200_time.h`, additions to `ec200_tcpip.h`).
- SMS completeness: preferred storage (`CPMS`), service centre (`CSCA`),
  write-then-send from storage (`CMGW`/`CMSS`), and incoming-message
  notification control (`CNMI`).
- Low power: PSM (`CPSMS`) and eDRX (`CEDRXS`/`CEDRXRDP`) in `ec200_power.h`.
- `ec200_at_last_error_text()` — returns the verbose `+CME`/`+CMS` message
  text. Under `AT+CMEE=2` the module answers with text rather than a number,
  and the text is often the only diagnostic available.

### Changed

- `EC200_SMS_READ_BUF_LEN` (512) and `EC200_SMS_LIST_BUF_LEN` (1024) replace
  the 2048-byte response buffers in `ec200_sms.c`, to bound stack usage. This
  reduces how many messages `ec200_sms_list()` can return in one call to
  roughly four at the default; both are `#ifndef`-guarded and can be raised at
  compile time. See the warning on `ec200_sms_list()`.

### Fixed

- `ec200_at_last_cme_error()` no longer discards verbose error text. It returns
  `-1` when the payload is not numeric; use `ec200_at_last_error_text()` for
  the message.
- `ec200_ppp_escape()` no longer wedges the handle permanently when the carrier
  is gone. `NO CARRIER` leaves the module in command mode, so the data-mode
  flag is now cleared rather than left set (which made every later AT call
  return `EC200_ERR_BUSY`).
- MQTT `AT+QMTCFG="ssl"` state is now set explicitly on every open. The setting
  persists across sessions on the module, so a previous MQTTS session left TLS
  enabled for a subsequent plaintext connection.
- The HTTPS POST `CONNECT` prompt wait now honours the caller's timeout. Over
  TLS the prompt is slow, and a fixed short wait made every HTTPS POST time out
  and left the HTTP subsystem blocked.
- SMS stack frames reduced (previously ~2.2 KB).

### Known issues

Identified, documented, and deliberately shipped. The full ranked list with
file/line citations and fix sketches is in [TECH_DEBT.md](TECH_DEBT.md).
The ones most likely to affect a consumer:

- `ec200_time_get_network()` (`AT+QLTS`) returns a year around 4026.
  `ec200_time_get()` (`AT+CCLK?`) is unaffected.
- `ec200_mqtt_open()` applies the TLS setting to `cfg->tcp_connect_id` while
  the session runs on `cfg->client_idx`. Set both to the same value.
- `ec200_http_read()`, `ec200_http_post()` (post-body wait) and
  `ec200_file_upload()` use fixed internal timeouts instead of the caller's.
- `ec200_tcp_ping()` returns `EC200_ERR_TIMEOUT` rather than
  `EC200_ERR_MODULE` for a failed ping, after the full timeout.
- `ec200_tcp_dns_resolve()` leaves extra address lines unread on a
  multi-address answer.
- `ec200_ssl_socket_open()` leaves the connection id allocated on failure;
  close it before retrying.

## [1.1.0]

- Packaged as an ESP-IDF component (`idf_component.yml`, dual-mode
  `CMakeLists.txt`).
- PPP dial-up control plane (`ec200_ppp.h`).
- AT engine rework: persistent line assembly, deadline budgets, line-storm
  guard, URC dispatch table.
