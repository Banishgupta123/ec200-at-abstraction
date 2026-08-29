/**
 * @file test_helpers.h
 * @brief Shared helpers for the Unity test suites.
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "ec200.h"
#include "loopback.h"
#include "unity.h"

/**
 * @brief Reset the loopback and run ec200_init() against scripted
 *        AT / ATE0 probe responses.
 *
 * Leaves @p handle initialised and the loopback rule queue empty.
 */
#define SETUP_MODEM(handle)                                                  \
    do {                                                                     \
        lb_reset();                                                          \
        lb_on_write("AT\r",  "\r\nOK\r\n");                                  \
        lb_on_write("ATE0",  "\r\nOK\r\n");                                  \
        TEST_ASSERT_EQUAL_INT(EC200_OK,                                      \
            ec200_init((handle), lb_write, lb_read, lb_delay, NULL));        \
    } while (0)

#endif /* TEST_HELPERS_H */
