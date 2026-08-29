/**
 * @file wrapper_template.c
 * @brief Template for the platform-specific UART wrapper.
 *
 * Copy this file into your project and implement the three stub functions
 * using your MCU's UART / HAL driver.  Then call ec200_init() with pointers
 * to these functions.
 *
 * -------------------------------------------------------------------------
 * Compilation note
 * -------------------------------------------------------------------------
 * This file is intentionally **not** compiled as part of the ec200 library
 * itself.  It is a starting-point template for end-users.
 * -------------------------------------------------------------------------
 */

#include "ec200.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * 1.  Declare a global library handle
 * ========================================================================= */
static ec200_handle_t g_ec200;

/* =========================================================================
 * 2.  Implement the three transport callbacks
 *     Adapt the bodies to your hardware / RTOS / HAL.
 * ========================================================================= */

/**
 * @brief UART write callback.
 *
 * Transmit @p len bytes from @p data via your UART peripheral.
 * Return the number of bytes sent, or -1 on error.
 */
static int my_uart_write(const uint8_t *data, uint16_t len, void *user_ctx)
{
    (void)user_ctx;  /* not used in this example */

    /* --- Replace with your HAL / BSP call -------------------------------- */
    /*
     * Example for STM32 HAL:
     *   HAL_StatusTypeDef s = HAL_UART_Transmit(&huart2, data, len, 1000);
     *   return (s == HAL_OK) ? (int)len : -1;
     *
     * Example for Arduino:
     *   Serial1.write(data, len);
     *   return (int)len;
     */

    /* Stub: write to stdout for host-side testing */
    return (int)fwrite(data, 1, len, stdout);
}

/**
 * @brief UART read callback.
 *
 * Read up to @p len bytes into @p data, waiting at most @p timeout_ms ms.
 *
 * Contract (important):
 *  - return > 0  : number of bytes received
 *  - return   0  : timeout expired with no data (NOT an error)
 *  - return < 0  : fatal I/O error only (UART fault, driver failure)
 *
 * Returning 0 on timeout lets the library distinguish EC200_ERR_TIMEOUT
 * from EC200_ERR_IO.
 */
static int my_uart_read(uint8_t *data, uint16_t len,
                        uint32_t timeout_ms, void *user_ctx)
{
    (void)timeout_ms;
    (void)user_ctx;

    /* --- Replace with your HAL / BSP call -------------------------------- */
    /*
     * Example for STM32 HAL (DMA-backed ring-buffer):
     *   return uart_ring_buffer_read(data, len, timeout_ms);
     *
     * Example for FreeRTOS / xStreamBuffer:
     *   return (int)xStreamBufferReceive(xUartRxStream, data,
     *                                    len, pdMS_TO_TICKS(timeout_ms));
     */

    /* Stub: no real data */
    (void)data;
    (void)len;
    return 0;
}

/**
 * @brief Delay callback.
 *
 * Block for @p ms milliseconds.
 */
static void my_delay_ms(uint32_t ms, void *user_ctx)
{
    (void)user_ctx;

    /* --- Replace with your HAL / RTOS call ------------------------------- */
    /*
     * Example for STM32 HAL:
     *   HAL_Delay(ms);
     *
     * Example for FreeRTOS:
     *   vTaskDelay(pdMS_TO_TICKS(ms));
     *
     * Example for POSIX / Linux:
     *   usleep(ms * 1000UL);
     */

    /* Stub: busy-wait approximation — replace in real code */
    volatile uint32_t i;
    for (i = 0; i < ms * 1000UL; i++) {
        (void)i;
    }
}

/* =========================================================================
 * 3.  Optional: URC handler
 * ========================================================================= */
static void my_urc_handler(const char *urc, void *user_ctx)
{
    (void)user_ctx;
    printf("[URC] %s\n", urc);
}

/* =========================================================================
 * 4.  Application entry point
 * ========================================================================= */
int main(void)
{
    /* ------------------------------------------------------------------ */
    /* 4a.  Initialise the library                                         */
    /* ------------------------------------------------------------------ */
    ec200_status_t st = ec200_init(&g_ec200,
                                   my_uart_write,
                                   my_uart_read,
                                   my_delay_ms,
                                   NULL /* user_ctx */);
    if (st != EC200_OK) {
        printf("ec200_init failed: %s\n", ec200_status_str(st));
        return -1;
    }

    /* Register URC handler (optional) */
    ec200_set_urc_handler(&g_ec200, my_urc_handler);

    /* Enable verbose error reporting (echo is already disabled by init) */
    ec200_set_cmee(&g_ec200, 2);

    /* ------------------------------------------------------------------ */
    /* 4b.  Read module identification                                      */
    /* ------------------------------------------------------------------ */
    char imei[EC200_MAX_IMEI_LEN];
    if (ec200_get_imei(&g_ec200, imei, sizeof(imei)) == EC200_OK) {
        printf("IMEI: %s\n", imei);
    }

    char fw[EC200_MAX_FW_VER_LEN];
    if (ec200_get_fw_version(&g_ec200, fw, sizeof(fw)) == EC200_OK) {
        printf("FW : %s\n", fw);
    }

    /* ------------------------------------------------------------------ */
    /* 4c.  SIM and network                                                 */
    /* ------------------------------------------------------------------ */
    ec200_sim_status_t sim_stat;
    if (ec200_sim_get_status(&g_ec200, &sim_stat) == EC200_OK &&
        sim_stat == EC200_SIM_READY) {

        char imsi[EC200_MAX_IMSI_LEN];
        ec200_sim_get_imsi(&g_ec200, imsi, sizeof(imsi));
        printf("IMSI: %s\n", imsi);
    }

    printf("Waiting for network registration...\n");
    if (ec200_net_wait_registered(&g_ec200, 60000) != EC200_OK) {
        printf("Registration timed out.\n");
        return -1;
    }
    printf("Registered!\n");

    ec200_signal_quality_t sq;
    ec200_net_get_signal(&g_ec200, &sq);
    printf("RSSI: %d dBm\n", (int)sq.rssi);

    /* ------------------------------------------------------------------ */
    /* 4d.  Activate PDP context                                            */
    /* ------------------------------------------------------------------ */
    ec200_pdp_context_t pdp = {
        .cid      = 1,
        .type     = EC200_PDP_TYPE_IP,
        .apn      = "internet",   /* <- replace with your APN */
        .username = "",
        .password = "",
    };

    if (ec200_data_connect(&g_ec200, &pdp) == EC200_OK) {
        printf("IP: %s\n", pdp.ip_addr);
    }

    /* ------------------------------------------------------------------ */
    /* 4e.  Send an HTTPS GET (example)                                     */
    /* ------------------------------------------------------------------ */
    ec200_http_set_context(&g_ec200, pdp.cid);
    ec200_http_set_url(&g_ec200, "http://httpbin.org/get");

    ec200_http_response_t http_resp;
    if (ec200_http_get(&g_ec200, 30000, &http_resp) == EC200_OK) {
        printf("HTTP %u, body %lu bytes\n",
               http_resp.status_code,
               (unsigned long)http_resp.content_length);

        /* Reserve one byte for the NUL terminator: pass sizeof(body) - 1 so
         * body[body_len] is always a valid write. */
        uint8_t body[512];
        uint32_t body_len = 0;
        if (ec200_http_read(&g_ec200, body, sizeof(body) - 1U,
                            &body_len, 10000) == EC200_OK) {
            body[body_len] = '\0';
            printf("Body: %s\n", (char *)body);
        }
    }
    ec200_http_stop(&g_ec200);

    /* ------------------------------------------------------------------ */
    /* 4f.  Send an SMS (example)                                           */
    /* ------------------------------------------------------------------ */
    ec200_sms_set_format(&g_ec200, EC200_SMS_FORMAT_TEXT);
    ec200_sms_send(&g_ec200, "+1234567890", "Hello from EC200!");

    /* ------------------------------------------------------------------ */
    /* 4g.  GNSS example                                                    */
    /* ------------------------------------------------------------------ */
    ec200_gnss_start(&g_ec200);
    my_delay_ms(5000, NULL); /* Allow time to acquire a fix */

    ec200_gnss_location_t loc;
    if (ec200_gnss_get_location(&g_ec200, &loc) == EC200_OK && loc.fix_valid) {
        printf("Location: %.6f, %.6f  Alt: %.1f m\n",
               (double)loc.latitude,
               (double)loc.longitude,
               (double)loc.altitude);
    }
    ec200_gnss_stop(&g_ec200);

    return 0;
}
