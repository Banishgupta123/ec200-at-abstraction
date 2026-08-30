# Development notes

Working context for this repository: how it is built and verified, how the
hardware test rig is driven, and what the module actually does (as opposed
to what the manuals say). Read this before picking the work back up.

> No credentials, SIM identifiers or phone numbers belong in this file.

## Goal

A **complete, public Quectel EC200U wrapper**. Completeness is the product:
cover the whole AT surface, including rarely used features. Every addition
gets host tests at 100% line/function/branch coverage **and** validation
against a real module.

## Quality gates

All of these must be clean before a commit:

```sh
cmake -S . -B build -DEC200_COVERAGE=ON
cmake --build build                       # -Werror, plus -Wstack-usage=1280
ctest --test-dir build                    # 6 suites
cmake --build build --target coverage     # expect 100 / 100 / 100
cmake --build build --target cppcheck
cmake --build build --target clang-tidy   # config in .clang-tidy
cmake -S . -B build-an -DEC200_ANALYZER=ON -DEC200_BUILD_TESTS=OFF
cmake --build build-an --target ec200     # GCC -fanalyzer
doxygen Doxyfile                          # must emit zero warnings
```

Two gates exist because a bug got through the normal ones:

- **`-Wstack-usage=1280`** on the library target. `EC200_RX_BUFFER_SIZE`
  once grew from 1 KB to 2 KB and silently doubled two SMS stack frames to
  ~2.2 KB, which overflows a small RTOS task. Response buffers are now
  sized per operation (`EC200_SMS_READ_BUF_LEN`, `EC200_SMS_LIST_BUF_LEN`).
- A periodic **strict sweep** with ~30 extra warnings (`-Wsign-conversion`,
  `-Wformat-truncation=2`, `-Wcast-qual`, …) at `-O2`. This is what caught
  the stack regression; the normal build did not.

Note: clang-tidy is most useful on Linux. On Windows/MinGW clang may fail
to parse the platform's own `stdlib.h`; that is environmental, not a
finding. If every file reports "file not found", pass GCC's own include
paths via `-isystem` or the analyzer silently never runs.

## Hardware test rig

Custom `isolated_dcu_esp32` board: ESP32-S3 + EC200U.

| Signal | GPIO | Notes |
|---|---|---|
| UART TX -> modem RX | IO8 | through a TXS0104E level shifter |
| UART RX <- modem TX | IO19 | same shifter |
| PWRKEY | IO9 | HIGH = pressed. **A toggle** - probe before pulsing, or you power a running modem OFF |
| RESET | IO10 | HIGH asserts; keep LOW |

The level shifter is powered from the modem's `VDD_EXT`, so the UART is
dead until the module has booted; the harness retries its probe.

`examples/esp_idf/main/ec200_test_harness.c` walks every public API
(happy path, bad path, security) and prints PASS/FAIL plus a summary.
Select it in `main/CMakeLists.txt`. Run logs are kept in `hw_logs/`.

### Sharing the serial port

`tools/serial_hub.py` owns the COM port and re-serves it, so a monitor and
automation can watch at the same time:

```sh
python tools/serial_hub.py COM14 115200 hw_logs/live.log 2323
```

- appends every byte to the log (tail it live)
- re-serves the port on TCP 2323 - attach a colored monitor with
  `idf.py monitor -p socket://127.0.0.1:2323` (or `tools/monitor.ps1`)
- connecting to port 2324 pulses the target's reset without dropping viewers

Flash cycle: stop the hub, `idf.py -p <port> flash`, restart the hub.

On Windows the ESP-IDF export script refuses to run when Git Bash's
`MSYSTEM` variable is set - clear `MSYSTEM`/`MINGW_*` first.

## Module behaviour that shaped the code

Observed on firmware `EC200UCNAAR03A03M08`. Each of these caused a fix;
they are not in the manuals.

- `AT+QHTTPCFG="contenttype"` takes a **numeric index 0-4, not a MIME
  string**. The string form is accepted at config time but fails the POST.
- **No `CFUN=7`** on this module. RF-off/airplane is `CFUN=4`.
- `AT+QMTCFG="ssl"` **persists across sessions**: a previous MQTTS session
  leaves TLS enabled and breaks later plaintext `QMTOPEN` until a power
  cycle, so the flag is always stated explicitly.
- `QMTDISC` already tears down the network connection, so a following
  `QMTCLOSE` has nothing to close and never emits its URC.
- On LTE the attach bearer is **already active**: re-defining or
  re-activating it errors, and `CGPADDR` may read `0.0.0.0` while the
  bearer is genuinely up moments later.
- GNSS with no fix reports **`+CMS ERROR`** (not CME) on `AT+QGPSLOC`.
- `AT+QCSQ` reports RSSI as a **positive** number while RSRP/SINR are the
  usual negative dBm.
- The `CONNECT` prompt for `QHTTPPOST` is **slow over TLS**; a fixed short
  wait made every HTTPS POST time out *and wedged the HTTP subsystem* for
  later requests. Prompt waits must honour the caller's timeout.
- **Subsystem interference:** a request left in flight (HTTP, TLS socket,
  NTP) blocks the next subsystem's connect. The harness runs a `settle()`
  between heavy groups.
- `ATD*99` fails with a verbose **"Operation not allowed"** while the PDP
  context is still active in AT mode (on LTE the attach bearer is up).
  Deactivate the context first, then dial.
- The data call ends by itself when nothing drives PPP/LCP, so `+++` is
  answered with **`NO CARRIER`** rather than `OK`. The library must clear
  its data-mode flag in that case - leaving it set made every later AT call
  return `EC200_ERR_BUSY` and wedged the handle permanently (fixed).
- After `AT+QPOWD` the **first PWRKEY pulse can be ignored**: the module is
  still shutting down. Wait for the shutdown to finish (~15 s) and retry;
  two attempts were needed in practice.
- A fresh boot restores the factory **`ATE1`**, so echo must be disabled
  again after any power cycle.
- Error payloads: with `AT+CMEE=2` the module *may* answer with text
  instead of a number, so the code is parsed only when the payload is
  entirely numeric and the raw text is always kept
  (`ec200_at_last_error_text()`). It is genuinely mixed on this firmware:
  `QFLST` errors stay numeric even in verbose mode, while `ATD` returns
  "Operation not allowed" - so both paths occur in practice and the text
  is often the only diagnostic available.

### Security, validated on hardware

An MQTT topic containing `\r\nAT+CFUN=0` did **not** execute: the module
rejected it, RF stayed on and registration held. Certificate verification
is genuinely enforced - a handshake against a CA that cannot verify the
host fails. Oversized inputs and Ctrl-Z in SMS text are rejected, and
binary payloads (including `0x1A`) transit intact.

## State and remaining work

Batches 1 (TLS: filesystem, SSL contexts, HTTPS, MQTTS, TLS sockets) and
2 (QNWINFO/QSPN/CGATT, DNS, ping, clock/NTP) are done and hardware-proven.

Still queued:

- SMS completeness: `CNMI` (incoming-SMS URC), `CPMS`, `CSCA`, `CMGW`/`CMSS`
- Low power: `CPSMS` (PSM), `CEDRXS` (eDRX)
- SIM completeness: `CLCK` (facility lock), `CPWD`, PUK unlock
- Misc: `QADC`, `QTEMP`, `QGPSGNMEA`

The PPP control plane is now exercised on hardware end to end: dial ->
data mode -> AT refused with BUSY -> escape -> hangup. Carrying actual IP
traffic still needs a host PPP stack (lwIP PPPoS); only that remains
unproven.

The single remaining harness skip is `sms_send`: it is deliberately not
wired to a personal number, and `AT+CNUM` returns nothing on this SIM, so
the self-loopback path is unavailable. Set `SMS_DEST` in the harness to
send one real message.

CI (`.github/workflows/ci.yml`) lives on an unmerged branch: GitHub Actions
does not run on a private repo without billing, so it is parked until the
repository is public. clang-tidy should be added to it at that point.
