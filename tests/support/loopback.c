/**
 * @file loopback.c
 * @brief Scripted loopback transport implementation.
 */

#include "loopback.h"

#include <string.h>

#define LB_RX_SIZE     (131072U)
#define LB_TX_SIZE     (8192U)
#define LB_MAX_RULES   (16U)

typedef struct {
    const char *match;
    const char *response;
    bool        fired;
} lb_rule_t;

static uint8_t   rx_buf[LB_RX_SIZE];
static size_t    rx_head, rx_tail;

static char      tx_buf[LB_TX_SIZE];
static size_t    tx_len;

static lb_rule_t rules[LB_MAX_RULES];
static size_t    rule_count;
static size_t    rule_next;

static bool      io_error;
static bool      read_io_error;
static bool      read_error_when_empty;
static int       short_write;
static int       fail_write_after; /* <0 = disabled, 0 = fail now */

void lb_reset(void)
{
    rx_head = rx_tail = 0;
    tx_len = 0;
    tx_buf[0] = '\0';
    memset(rules, 0, sizeof(rules));
    rule_count = rule_next = 0;
    io_error = false;
    read_io_error = false;
    read_error_when_empty = false;
    short_write = 0;
    fail_write_after = -1;
}

void lb_feed_bytes(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        rx_buf[rx_head % LB_RX_SIZE] = data[i];
        rx_head++;
    }
}

void lb_feed(const char *s)
{
    lb_feed_bytes((const uint8_t *)s, strlen(s));
}

void lb_on_write(const char *match, const char *response)
{
    if (rule_count < LB_MAX_RULES) {
        rules[rule_count].match    = match;
        rules[rule_count].response = response;
        rules[rule_count].fired    = false;
        rule_count++;
    }
}

const char *lb_tx_data(void)
{
    return tx_buf;
}

size_t lb_rx_pending(void)
{
    return rx_head - rx_tail;
}

void lb_set_io_error(bool enable)
{
    io_error = enable;
}

void lb_set_short_write(int n)
{
    short_write = n;
}

void lb_set_read_io_error(bool enable)
{
    read_io_error = enable;
}

void lb_set_read_error_when_empty(bool enable)
{
    read_error_when_empty = enable;
}

void lb_fail_write_after(int n)
{
    fail_write_after = n;
}

int lb_write(const uint8_t *data, uint16_t len, void *ctx)
{
    (void)ctx;
    if (io_error) {
        return -1;
    }
    if (fail_write_after == 0) {
        return -1;
    }
    if (fail_write_after > 0) {
        fail_write_after--;
    }

    uint16_t accept = len;
    if (short_write > 0 && (int)accept > short_write) {
        accept = (uint16_t)short_write;
        short_write = 0; /* one-shot */
    }

    /* Record written bytes (truncate silently if the log fills). */
    size_t space = LB_TX_SIZE - 1U - tx_len;
    size_t n = (accept < space) ? accept : space;
    memcpy(&tx_buf[tx_len], data, n);
    tx_len += n;
    tx_buf[tx_len] = '\0';

    /* Fire the next pending rule if this write matches it. */
    if (rule_next < rule_count && !rules[rule_next].fired) {
        char chunk[512];
        size_t clen = (accept < sizeof(chunk) - 1U)
                        ? accept : sizeof(chunk) - 1U;
        memcpy(chunk, data, clen);
        chunk[clen] = '\0';
        if (strstr(chunk, rules[rule_next].match) != NULL) {
            rules[rule_next].fired = true;
            lb_feed(rules[rule_next].response);
            rule_next++;
        }
    }

    return (int)accept;
}

int lb_read(uint8_t *data, uint16_t len, uint32_t timeout_ms, void *ctx)
{
    (void)timeout_ms;
    (void)ctx;
    if (io_error || read_io_error) {
        return -1;
    }

    size_t avail = rx_head - rx_tail;
    if (avail == 0) {
        return read_error_when_empty ? -1 : 0;
    }
    size_t n = (avail < len) ? avail : len;
    for (size_t i = 0; i < n; i++) {
        data[i] = rx_buf[rx_tail % LB_RX_SIZE];
        rx_tail++;
    }
    return (int)n;
}

void lb_delay(uint32_t ms, void *ctx)
{
    (void)ms;
    (void)ctx;
}
