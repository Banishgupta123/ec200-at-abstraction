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

Note: clang-tidy is most useful on Linux. It also runs clean under MSYS2
when clang-tidy and GCC come from the same `mingw-w64-x86_64-*` packages —
the earlier "cannot parse the platform's own `stdlib.h`" failure came from
mixing a system clang with a different GCC's headers. If every file reports
"file not found", pass GCC's own include paths via `-isystem`, or the
analyzer silently never runs.

### Toolchain on a fresh Windows box

There is no host compiler by default. What the gates need:

```sh
winget install --id MSYS2.MSYS2 -e
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
          mingw-w64-x86_64-cppcheck mingw-w64-x86_64-clang-tools-extra \
          mingw-w64-x86_64-python-lxml ruby doxygen
/mingw64/bin/python -m pip install --break-system-packages gcovr
```

Then put `/c/msys64/mingw64/bin` and `/c/msys64/usr/bin` on PATH (the
second one carries Ruby, without which the CMock suite silently drops out
and only 5 of the 6 ctest suites run). `gcovr` has no MinGW package and its
`lxml` dependency will not build from source, hence the pacman line for it.

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
- `AT+CPMS=` (set form) **does** answer `+CPMS: u1,t1,u2,t2,u3,t3` — six bare
  integers — while `AT+CPMS?` interleaves the names
  (`"ME",u,t,"ME",u,t,…`). One parser serves both via a field stride; do not
  assume the set form replies with a bare `OK`. `ME` reports 100 slots.
- `AT+CMGW` returns the assigned index and transmits nothing. `AT+CMSS`
  does transmit, and returns its own message reference (observed: 129). It
  **leaves the stored copy in place** afterwards, now marked `STO SENT`, so
  a harness that stores-and-sends must delete the slot or the storage fills
  up over repeated runs.
- `AT+CEDRXS?` answers **`+CEDRXS: <mode>,<AcT-type>,"<value>"`** — with a
  leading `<mode>` that 3GPP 27.007's read form does not show. Reading the
  AcT-type from field 0 therefore returns the mode, and does it *silently*:
  both are small integers, so the bug survived a green harness run and was
  only caught by printing the values. Observed raw replies:

  ```
  +CPSMS: 0,,,"00100001","00000001"     <- mode, then two empty, then timers
  +CEDRXS: 0,4,"0101"                    <- mode FIRST, then AcT, then value
  +CEDRXRDP: 0                           <- AcT 0 alone = eDRX not in use
  ```

  Lesson recorded because it cost a full flash cycle: a harness assertion
  that only checks the status code cannot catch a field-offset bug. Assert
  the parsed **values** for anything with more than one numeric field.
- `AT+CEDRXS?` keeps reporting the last requested AcT-type and value after
  eDRX is disabled, so "is it on" must come from `<mode>`, never from
  whether a value is present.
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

Batches 1 (TLS), 2 (network diagnostics) and 3 (SMS completeness: `CPMS`,
`CSCA`, `CMGW`/`CMSS`, `CNMI`) are done and hardware-proven.

Batch 4 (low power: `CPSMS`, `CEDRXS`, `CEDRXRDP`) is written and host-tested
at 100%. It ran on hardware once at **222 passed, 0 failed, 0 skipped**, but
that run is what exposed the `+CEDRXS:` field-offset bug — so the *fixed*
parser and the strengthened value assertions have **not yet been run on the
module**. See "Picking this back up" below.

Still queued:

- SIM completeness: `CLCK` (facility lock), `CPWD`, PUK unlock
- Misc: `QADC`, `QTEMP`, `QGPSGNMEA`

The PPP control plane is now exercised on hardware end to end: dial ->
data mode -> AT refused with BUSY -> escape -> hangup. Carrying actual IP
traffic still needs a host PPP stack (lwIP PPPoS); only that remains
unproven.

The harness runs with **no skips**: 195 passed, 0 failed, 0 skipped.

The count can vary by one or two between runs: a few sub-tests are guarded
on a precondition (an NTP sync that can time out, an inbox that may be
empty, a TLS connect that can fail), and those guards correctly skip the
assertion rather than failing. Zero failures is the invariant; a skip means
a precondition genuinely did not hold, so read the reason before assuming a
regression.

The harness now sends **two real SMS per run** when `main/test_secrets.h`
defines `SMS_DEST` — one via CMGS, one via CMSS from storage. Both resolve
their destination through `sms_dest()`, which prefers `SMS_DEST` and
otherwise falls back to the SIM's own number so the message loops back to
the rig instead of reaching anyone. Re-flashing or resetting re-runs
everything, so each reboot costs two messages: do not reboot the board
repeatedly just to re-capture a log.

### Picking this back up

Exactly one thing is outstanding: **flash and run the harness to confirm the
eDRX fix.** Everything else is committed, gated and green.

```sh
# host gates (all currently clean)
cmake -S . -B build -G Ninja -DEC200_COVERAGE=ON -DCMAKE_C_COMPILER=gcc
cmake --build build && ctest --test-dir build
cmake --build build --target coverage      # 100 / 100 / 100

# hardware
cd examples/esp_idf && idf.py build && idf.py -p <port> flash
cd ../.. && python tools/serial_hub.py <port> 115200 hw_logs/15-lowpower.log 2323 --reset
```

Expect **> 222 passed, 0 failed, 0 skipped** (the fix adds assertions), and
check these lines specifically — they are the ones that were wrong:

```
edrx now enabled=1 act=4 requested="0101"     <- act must be 4, NOT 0
module reports TAU=3600s active=2s
```

The `RAW CPSMS?` / `RAW CEDRXS?` / `RAW CEDRXRDP` dumps in `test_lowpower()`
were diagnostics for this bug. They are harmless to keep, but can be deleted
once the fix is confirmed.

If the flash fails with the port busy, a `serial_hub.py` from an earlier
session still owns it — stop it first.

### Rig on a different machine

The transcript in `.session/` was recorded under `C:\Users\banis\…`; this
checkout lives under `C:\Users\banish\…`. Two consequences:

- `examples/esp_idf/build/` may be configured for the old path. `idf.py`
  refuses to build and says so; `idf.py fullclean` fixes it.
- The rig's COM port is not stable across machines. `COM14` in the serial-hub
  example was a Bluetooth port on the second machine. Enumerate before
  assuming, and note which ports appear only *after* plugging the board in:
  `Get-CimInstance Win32_PnPEntity | ? { $_.Name -match 'COM\d+' }`

### Two ways in, and only one of them can flash

The board can be reached over either link, and they behave differently:

| Link | Enumerates as | Flash? |
|---|---|---|
| Native USB (ESP32-S3 USB-Serial-JTAG) | `VID_303A PID_1002`, a CDC port + JTAG + MSC | **yes** |
| External FTDI adapter | `VID_0403 PID_6010`, an FT2232 *pair* of COM ports | no |

On the FTDI path only **EN is wired, to RTS** — that is exactly what
`pulse_reset()` in `tools/serial_hub.py` drives. IO0/BOOT is **not** on DTR,
so esptool can reset the chip but can never hold it in the ROM downloader:
it resets, boots the app, and esptool fails with `Invalid head of packet
(0x20)` — that `0x20` is a space from the application's own log output.
Diagnose it by pulsing a reset and reading the ROM banner: `boot:0x8
(SPI_FAST_FLASH_BOOT)` means the strapping never requested download mode.

So **flash over the native USB port**, where esptool enters download mode by
itself. The FTDI pair is fine as a passive console.

Of the FT2232's two ports, only one is the UART; the other is the JTAG
channel and stays silent. openocd cannot claim it either while Windows has
the FTDI VCP driver bound (`LIBUSB_ERROR_NOT_FOUND`) — that needs a Zadig
rebind to WinUSB, which removes both COM ports until rebound.

**`serial_tail.py --reset` must not be used on the native USB port.** Its
sequence is FTDI-shaped: it drives DTR low (IO0) while releasing RTS (EN),
which on USB-Serial-JTAG is a *download-mode request*, not a reboot. The
board goes silent and looks dead. To reset into the application, hold IO0
high — pulse RTS only, leaving DTR false — or let esptool do it with
`--after hard-reset`.

`sms_send` sends one real message per run, so the destination is kept out
of this repository. The harness first tries the SIM's own number (`AT+CNUM`,
then the "own numbers" phonebook `AT+CPBS="ON"`/`AT+CPBR`) for a loopback
that messages nobody; this SIM has neither provisioned. Otherwise it uses
`SMS_DEST` from `main/test_secrets.h`, which is gitignored:

```c
/* examples/esp_idf/main/test_secrets.h - never committed */
#define SMS_DEST "+1234567890"
```

Without that file the send is skipped rather than failing, so a fresh
clone still runs clean.

CI (`.github/workflows/ci.yml`) lives on an unmerged branch: GitHub Actions
does not run on a private repo without billing, so it is parked until the
repository is public. clang-tidy should be added to it at that point.
