# Technical debt

Known defects and pending work, ranked. Every item here is real, reproducible
from the cited line, and deliberately shipped rather than fixed — usually
because the fix needs a design decision or hardware time. Nothing on this
list is a surprise; if you hit one of these, it is documented, not unknown.

Source: the 2026-09-05 ten-angle code review of `main..feat/tls` (verified
against the tree, not just the diff), plus the queued work from
`docs/DEVELOPMENT.md`. Items marked **(hw-evidence)** have supporting
evidence in `hw_logs/`. Items marked **(unverified)** are plausible but not
confirmed.

Remove an item when it is fixed *and* has a regression test.

---

## A. Correctness — silent wrong answers

These return `EC200_OK` with a wrong result. Highest priority: nothing in the
current test suite catches any of them.

### A1. `ec200_time_get_network()` returns year 4026
[src/ec200_time.c:36](src/ec200_time.c#L36). `parse_timestamp()` is shared by
`AT+CCLK?` (2-digit year) and `AT+QLTS` (4-digit year) and does `2000 + yy`
unconditionally. The range check never validates the year.
`tests/test_netdiag.c` feeds QLTS a 2-digit year, so the test encodes the
same wrong assumption; the harness prints only `QLTS -> OK`.
**Fix:** `dt->year = (yy < 100) ? 2000 + yy : yy;` and a test with a real
`+QLTS: "2026/08/30,..."` line. One line. Do this first.

### A2. `ec200_data_connect()` silently ignores the caller's APN
[src/ec200_data.c:147](src/ec200_data.c#L147). The early return on an
already-addressed context skips `AT+CGDCONT`, so `ctx->apn`, `ctx->type` and
credentials are never applied. Documented as a warning in `ec200_data.h` and
`docs/api.md`, but the API still returns success for a request it did not
honour. Three sub-defects:
- The `"0.0.0.0"` sentinel is IPv4-only; IPv6/IPv4v6 contexts read as "up".
- If `CGACT` succeeds but the second `CGPADDR` still returns `0.0.0.0`, it
  returns `EC200_OK` with that address ([:164](src/ec200_data.c#L164)).
- `test_data_connect_already_active` asserts `AT+CGDCONT` is absent using an
  *empty* `ctx.apn`, so it locks the behaviour in without testing the case.

**Design decision needed:** either (a) compare the live context's APN via
`AT+CGDCONT?` and only short-circuit when it matches, or (b) return a
distinct status (e.g. `EC200_ERR_BUSY`) when the context is up with a
different APN, or (c) keep the behaviour and rename the helper.

### A3. MQTT TLS armed on the wrong client index
[src/ec200_mqtt.c:119](src/ec200_mqtt.c#L119). `AT+QMTCFG="ssl"` uses
`cfg->tcp_connect_id`; `QMTCONN`/`QMTSUB`/`QMTPUB` use `cfg->client_idx`.
The function's own comment names the parameter `<client_idx>`. With the two
ids different, TLS is enabled on one client and the session runs plaintext on
another, returning success. Every test and the harness pin both to 0.
**Fix:** use `cfg->client_idx`; add a test with the ids set differently.
Also: `ssl_ctx_id` is range-checked only when `use_tls` is true.

### A4. Out-of-bounds write in the test suite
[tests/test_netdiag.c:428](tests/test_netdiag.c#L428).
`test_ping_many_reply_lines_then_timeout` writes 6 + 70×31 = 2176 bytes into
a 2048-byte static buffer. `snprintf` returns would-have-written, so from
iteration 66 `&resp[n]` is out of bounds and `sizeof(resp) - n` underflows to
`SIZE_MAX`. Passes green only because the buffer is static and no sanitizer
is configured (see E1). **Fix:** stop the loop at `n + 31 < sizeof(resp)`,
or size the buffer to 2304.

### A5. Ping error summary not recognised
[src/ec200_tcpip.c:352](src/ec200_tcpip.c#L352). The summary detector
requires all 7 fields; the module emits a single `+QPING: <err>` on failure.
A failed ping therefore burns the full `timeout_ms` and returns
`EC200_ERR_TIMEOUT` instead of `EC200_ERR_MODULE`, with `res` zeroed.
`tests/test_netdiag.c:203` fabricates a 7-field error line the firmware does
not send. **Fix:** treat a line where field 0 parses and field 1 does not as
the error summary.

### A6. DNS multi-answer residue corrupts the next command **(hw-evidence)**
[src/ec200_tcpip.c:306](src/ec200_tcpip.c#L306). `dns_resolve` reads the
header (which carries `<count>`), takes address #1 and returns, leaving
`count−1` `+QIURC: "dnsgip"` lines unread. `hw_logs/10-all-pass-no-skips.log`
shows `dns_resolve example.com -> OK (104.20.23.154)` immediately followed by
`invalid host -> Parse error` — consistent with the second call matching the
leftover quoted-address line as its header. **Fix:** parse `<count>` from
field 2 and drain that many lines; add a test with a 2-address response
followed by a second command.

### A7. `ec200_gnss.c` HDOP wraps above 25.5
[src/ec200_gnss.c:82](src/ec200_gnss.c#L82). `(uint8_t)(hdop * 10.0f)` is
undefined for HDOP > 25.5 and on Xtensa truncates: HDOP 99.9 reads back as
23.1, 25.6 as 0.0. Poor-fix values are exactly the ones that overflow.
**Fix:** clamp to 255 before the cast, or widen to `uint16_t`.

### A8. `ec200_file_exists()` treats a parse failure as "absent"
[src/ec200_file.c:115](src/ec200_file.c#L115). `qflst_size()` returns
`EC200_ERR_PARSE` both for "no `+QFLST` line" and "line arrived but
unparsable" (interleaved URC, storm truncation, comma in filename). The
second case reports `*exists = false` with `EC200_OK`. A provisioning flow
then re-uploads the certificate every boot. **Fix:** only `EC200_ERR_CME`
means not-found.

---

## B. Timeout policy has no home

The HTTPS `CONNECT`-prompt fix (honour the caller's `timeout_ms`) landed at
one call site. The same defect exists at four others, and `timeout_ms` is
re-spent per line in two loops. The right fix is a `timeout_ms` parameter on
the `send_expect`/`wait_final` helpers threaded from each public API, not
five more inline ternaries. `EC200_AT_TIMEOUT_HTTP` (30000) is defined and
referenced nowhere.

### B1. `ec200_http_read()` — fixed 1 s `CONNECT` wait
[src/ec200_http.c:290](src/ec200_http.c#L290). Identical to the HTTPS POST
bug that was found on hardware and fixed. A slow HTTPS body read times out
regardless of the caller's budget and leaves the body in the UART, blocking
the HTTP subsystem.

### B2. `ec200_http_post()` — fixed 10 s post-body `OK` wait
[src/ec200_http.c:181](src/ec200_http.c#L181). Overrides `timeout_ms` in the
opposite direction: a caller passing 300 ms blocks 10 s if the module never
acknowledges the body.

### B3. `ec200_file_upload()` — 1 s `CONNECT` wait, 60 s declared
[src/ec200_file.c:34](src/ec200_file.c#L34). The command sends
`AT+QFUPL="<name>",<len>,60` then waits 1 s for the prompt and 10 s for
`+QFUPL`. No caller-supplied timeout at all. On timeout the module is left in
data mode (see C2).

### B4. `ec200_tcp_ping()` / `ec200_tcp_dns_resolve()` — budget per line
[src/ec200_tcpip.c:365](src/ec200_tcpip.c#L365),
[:283](src/ec200_tcpip.c#L283). Each loop iteration gets a fresh
`timeout_ms`. Ping can block up to `EC200_AT_MAX_LINES` (64) × `timeout_ms`
— 32 minutes at the harness's 30 s. The header documents "Overall deadline".
**Fix:** compute one deadline at entry, pass the remainder to each wait.

---

## C. Robustness and API contract

### C1. `ec200_ssl_socket_open()` leaks the connection id on failure
[src/ec200_ssl_socket.c:50](src/ec200_ssl_socket.c#L50). No `QSSLCLOSE` on a
non-zero `+QSSLOPEN` error, so every retry on that id fails until closed.
Also never checks field 0 matches the requested `conn_id`, so a stale URC for
another connection is accepted. The harness works around it in `settle()`.
Documented as a warning in `ec200_ssl_socket.h`.

### C2. `ec200_file_upload()` aborts mid-data-phase
[src/ec200_file.c:41](src/ec200_file.c#L41). A write or `+QFUPL` timeout
returns while the module is still counting down bytes; the next AT command is
consumed as file payload and the link desynchronises. `tests/test_secure.c`
rebuilds the handle after each such case — that is the tell. Also:
`ec200_file.h` says 1..65535 (corrected); the review found the older doc
said 131072.

### C3. `ec200_ssl.c` — no length guard on struct string fields
[src/ec200_ssl.c:40](src/ec200_ssl.c#L40). `cfg->cacert/clientcert/clientkey`
are fixed `char[]` fields passed to `snprintf("%s")` with no NUL check —
unlike every `const char *` parameter, which is guarded. An unterminated
field reads past the struct. Same shape: `cfg->host` in `ec200_mqtt.c:102`,
`ctx->apn/username/password` in `ec200_data.c:28`.

### C4. `ec200_ssl_configure()` leaves stale bindings and raises seclevel first
[src/ec200_ssl.c:86](src/ec200_ssl.c#L86). Empty cert fields do not clear a
previous binding on the same context. Seclevel is set before certs are bound,
so a mid-chain failure leaves the context verifying against the previous CA.
No unconfigure/reset entry point exists.

### C5. Output truncation returns `EC200_OK`
[src/ec200_tcpip.c:302](src/ec200_tcpip.c#L302) (`dns_resolve`),
[src/ec200_network.c:274](src/ec200_network.c#L274) (`get_spn`). Both clamp
into the caller's buffer and return success. An IPv4-sized buffer receiving
an AAAA answer gets a truncated, valid-looking prefix. Contradicts the
reject-don't-truncate policy applied to inputs in the same commit.

### C6. SMS buffer shrink causes header-as-body mispairing
[src/ec200_sms.c:236](src/ec200_sms.c#L236),
[src/ec200_at.c:309](src/ec200_at.c#L309). `receive_transaction()` *skips*
lines that do not fit and keeps appending shorter later ones, returning
`EC200_OK`. With the 1024-byte list buffer a long body can be dropped while
the next `+CMGL:` header still fits — that header becomes the previous
message's `text`. Documented on `ec200_sms_list()`. **Fix:** return
`EC200_ERR_OVERFLOW` from the engine on the first dropped line, or stop
accumulating.

### C7. `ec200_ssl_socket_recv()` / `ec200_tcp_recv()` short-read desync
[src/ec200_ssl_socket.c:118](src/ec200_ssl_socket.c#L118),
[src/ec200_tcpip.c:141](src/ec200_tcpip.c#L141). On a short raw read the
function returns immediately, leaving `actual − got` payload bytes plus the
trailing `OK` in the UART. The next command parses network-controlled bytes
as AT lines. The header documents no partial-data contract.

### C8. `ec200_cfun_t` renumbered in place
[include/ec200_types.h:591](include/ec200_types.h#L591). `AIRPLANE` 7→4,
`DISABLE_TX` deleted. Documented in `CHANGELOG.md` with migration notes;
`EC200_CFUN_AIRPLANE` carries only a `@deprecated` comment, not a compiler
attribute, so nothing warns at the call site. **Fix:** add
`__attribute__((deprecated))` where supported.

### C9. `ec200_file_info_t` is dead
[include/ec200_types.h:467](include/ec200_types.h#L467). Referenced by no
function. Doc corrected to say so. Either implement `ec200_file_list()` over
the existing `qflst_size()` helper or delete the type.

---

## D. AT engine and architecture

### D1. `ppp_escape` still wedges on `EC200_ERR_OVERFLOW`
[src/ec200_ppp.c:71](src/ec200_ppp.c#L71). After `+++`, `wait_final` can
trip the 64-line storm guard on residual PPP bytes and return `OVERFLOW`.
That falls into the `st != EC200_OK` branch, `_ppp_data_mode` stays true,
and every later AT call returns `EC200_ERR_BUSY` — the exact failure the
carrier-loss fix was written for. `ec200_ppp_disconnect()` also refuses.

### D2. Error state not reset on receive-only entry points
[src/ec200_at.c:231](src/ec200_at.c#L231). `_last_err_kind` /
`_last_err_text` are reset only in `at_transmit`. `ec200_at_wait_prefix`,
`ec200_at_wait_final`, `ec200_at_poll_urc` report the *previous* command's
error — which is why the PPP fix had to enumerate three status codes.

### D3. `NO CARRIER` collapsed into `EC200_ERR_MODULE`
[src/ec200_at.c:160](src/ec200_at.c#L160). The engine special-cases the
string one line earlier and then discards the information. A distinct kind
(or `EC200_ERR_NO_CARRIER`) would let dial/escape/resume and every socket
path distinguish "carrier gone" from "command rejected".

### D4. PPP data-mode guard on 2 of 7 entry points
[src/ec200_at.c:227](src/ec200_at.c#L227). `at_transmit` and `poll_urc` are
guarded; `wait_prefix`, `wait_final`, `write_raw`, `read_raw`, `read_exact`
are not. The gap in `wait_final` is load-bearing (escape depends on it), so
this needs an explicit bypass, not a copy-pasted `if`.

### D5. `rx_getc` has no byte bound within a line
[src/ec200_at.c:55](src/ec200_at.c#L55). Only line count is bounded. A
stream with no LF (async-HDLC PPP escapes every control byte) spins past the
documented timeout until the task watchdog fires.

### D6. `ec200_at_parse_int_field` uses `atoi` on unbounded input
[src/ec200_at.c:796](src/ec200_at.c#L796). UB on out-of-range; accepts a
bare `"-"` as 0. `.clang-tidy` disables `cert-err34-c` on a rationale
(leading-digit check) that does not address what the check flags. Use
`strtol` with range checking, as `ec200_sms.c:69` already does.

### D7. MQTT receive buffer is a process global
[src/ec200_mqtt.c:59](src/ec200_mqtt.c#L59). One `static
ec200_mqtt_message_t` for all handles; breaks with two modems, and re-enters
if the user callback publishes (a second `+QMTRECV:` mid-command rewrites the
buffer under the outer callback).

---

## E. Build, gates, and tests

### E1. Gates are advisory, not enforced
`CMakeLists.txt` coverage target uses `--print-summary` with no
`--fail-under-line`/`--fail-under-branch`, so "expect 100/100/100" can never
fail a build. No sanitizer option exists anywhere. `.github/workflows/` is
empty on this branch (CI is parked in PR #6 until the repo is public). This
is why A4 passes green. **Fix:** `--fail-under-line 100 --fail-under-branch
100`; add `EC200_SANITIZE` (`-fsanitize=address,undefined`) for test
targets; unpark CI and add clang-tidy to it.

### E2. Tests that cannot fail
- `test_ping_ok` uses `count=4`, the same value as the hardcoded timeout
  field, so it cannot distinguish the two. Proven by mutation.
- Deleting the SNI step from `ec200_ssl_configure` leaves all 29 `test_secure`
  tests green — only the terminal command of each chain is unverified. **Fix:**
  assert all `lb_on_write` rules fired in `tearDown`.
- Two MQTT-TLS `strstr` assertions test the literal they registered as the
  trigger ([tests/test_secure.c:346](tests/test_secure.c#L346)).
- The eDRX bug is the live example: 222 tests passed while `act=0`. Harness
  sub-tests should assert values, not just status.

### E3. `ec200_demo_main.c` is compiled by nothing
[examples/esp_idf/main/CMakeLists.txt:2](examples/esp_idf/main/CMakeLists.txt#L2)
hardcodes the harness. The demo gained ~90 lines in this release that no
build checks. It also pulses PWRKEY unconditionally (PWRKEY is a toggle — on
a warm reset it powers the running modem *off*) and prints
`last_cme_error()` (now −1 for verbose payloads) instead of
`last_error_text()`. **Fix:** a CMake option so CI compiles both; probe
before pulsing; use `last_error_text()`.

### E4. `gcovr` not installed on the `banis` machine
Coverage cannot be run there until `pacman -S mingw-w64-x86_64-python-lxml`
then `pip install gcovr`. See `docs/DEVELOPMENT.md`.

### E5. Harness cleanup
- `RAW CPSMS?` / `RAW CEDRXS?` / `RAW CEDRXRDP` prints at
  [ec200_test_harness.c:793-801](examples/esp_idf/main/ec200_test_harness.c#L793-L801)
  were eDRX diagnostics; the fix is now hardware-verified (2026-09-05,
  `hw_logs/15-lowpower.log`, `act=4`). Safe to delete.
- `settle()` costs 72 AT round-trips and 27 s of fixed sleep per run.
- `SSLCOM_ROOT_ECC_PEM` (1249 B) is referenced nowhere; `isrg_ca.pem` is
  uploaded every run and bound to no context.
- ~3 KB of one-shot buffers are `static` instead of on the 16 KB task stack.

---

## F. Duplication

Not bugs, but every fix above has to be made N times until these are
collapsed.

- **F1.** `ec200_ssl_socket.c` is ~110 of 135 lines cloned from
  `ec200_tcpip.c` with the AT verb swapped. Extract `sock_send(verb, …)` /
  `sock_recv(cmd, prefix, …)` statics.
- **F2.** Host-length guard pasted 5× (`tcpip.c:35, :256, :321`,
  `ssl_socket.c:25`, `time.c:121`) with a `>` vs `>=` inconsistency at
  `http.c:83`.
- **F3.** `+PREFIX:` payload-skip idiom 9×, each with its own
  `GCOVR_EXCL_BR_LINE`. One `ec200_at_payload()` helper.
- **F4.** Quoted-field extraction 5× (`network.c`, `tcpip.c`, `sms.c`,
  `mqtt.c`, `data.c`). One `ec200_at_parse_str_field()` next to
  `ec200_at_parse_int_field()`.
- **F5.** Demo and harness duplicate ~90 lines of board glue (transport
  callbacks, PWRKEY sequencing, boot probe). One `board_ec200.{h,c}`.
- **F6.** `ec200_ssl_set_seclevel` hand-formats the string `cfg_num()`
  already produces.

---

## G. Feature completeness — queued batches

From `docs/DEVELOPMENT.md`. The library aims to wrap the full EC200U AT set.

- **G1. SIM:** `CLCK` (facility lock), `CPWD`, PUK unlock. **Take care:**
  three wrong PINs block the SIM; ten wrong PUKs destroy it. Test query and
  validation paths only; never send a wrong credential to a real SIM.
- **G2. Misc:** `QADC`, `QTEMP`, `QGPSGNMEA`.
- **G3. PPP carrying IP traffic.** The control plane is hardware-proven end to
  end (dial → data mode → BUSY → escape → hangup). Actual IP traffic needs a
  host PPP stack (lwIP PPPoS) and remains unproven.

---

## H. Unverified / open questions

- **H1. `+QFUPL` checksum: hex or decimal?**
  [src/ec200_file.c:54](src/ec200_file.c#L54) parses it with the decimal
  parser; the harness prints it as `0x%04X`. Every log shows `crc=0x10EE`
  (decimal 4334 — all digits in either base), so hardware cannot settle it.
  If hex, any checksum starting A–F silently reads as 0. Needs the Quectel
  FILE AT manual or a second test file with a letter in its checksum.
- **H2. `EC200_HTTP_CT_JSON` (index 4)** is documented as "requires recent
  firmware" and has not been exercised on the rig.
