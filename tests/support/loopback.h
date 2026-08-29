/**
 * @file loopback.h
 * @brief Scripted loopback transport for host-side tests.
 *
 * Unlike a plain ring-buffer loopback, this transport also records
 * everything the library writes and supports ordered write-triggered
 * response rules, so tests can verify BOTH the commands sent and the
 * response parsing:
 *
 * @code
 *   lb_reset();
 *   lb_on_write("AT+GSN", "\r\n867698040000001\r\nOK\r\n");
 *   ec200_get_imei(&h, imei, sizeof(imei));
 *   TEST_ASSERT_NOT_NULL(strstr(lb_tx_data(), "AT+GSN\r"));
 * @endcode
 *
 * Read contract implemented: >0 bytes, 0 on timeout/empty, -1 when
 * lb_set_io_error() was called.
 */

#ifndef LOOPBACK_H
#define LOOPBACK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** Reset buffers, rules, and error state. */
void lb_reset(void);

/** Queue raw response text for the library to read. */
void lb_feed(const char *s);

/** Queue raw response bytes (for binary payloads). */
void lb_feed_bytes(const uint8_t *data, size_t len);

/**
 * @brief Add an ordered write-triggered response rule.
 *
 * Rules fire in the order they were added: when a write containing
 * @p match arrives and every earlier rule has already fired, @p response
 * is queued for reading.
 */
void lb_on_write(const char *match, const char *response);

/** NUL-terminated log of all bytes written by the library. */
const char *lb_tx_data(void);

/** Number of bytes currently queued for reading. */
size_t lb_rx_pending(void);

/** Make subsequent reads/writes fail with -1 (fatal I/O error). */
void lb_set_io_error(bool enable);

/** Make only reads fail with -1 (writes keep working). */
void lb_set_read_io_error(bool enable);

/** Serve queued data normally, but fail with -1 once the queue is empty. */
void lb_set_read_error_when_empty(bool enable);

/** Cap the next write to @p n bytes (short-write simulation); 0 = off. */
void lb_set_short_write(int n);

/** Let the next @p n writes succeed, then fail with -1; negative = off. */
void lb_fail_write_after(int n);

/* Transport callbacks to hand to ec200_init(). */
int  lb_write(const uint8_t *data, uint16_t len, void *ctx);
int  lb_read(uint8_t *data, uint16_t len, uint32_t timeout_ms, void *ctx);
void lb_delay(uint32_t ms, void *ctx);

#endif /* LOOPBACK_H */
