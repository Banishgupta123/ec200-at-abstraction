/**
 * @file transport.h
 * @brief UART transport interface used for CMock-based unit tests.
 *
 * These functions carry the exact signatures of the ec200 transport
 * callbacks; CMock generates mock_transport.[ch] from this header so tests
 * can set precise call expectations on the transport layer.
 */

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>

int  transport_write(const uint8_t *data, uint16_t len, void *user_ctx);
int  transport_read(uint8_t *data, uint16_t len, uint32_t timeout_ms,
                    void *user_ctx);
void transport_delay(uint32_t ms, void *user_ctx);

#endif /* TRANSPORT_H */
