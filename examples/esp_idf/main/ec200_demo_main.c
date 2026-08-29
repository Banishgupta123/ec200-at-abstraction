/**
 * @file ec200_demo_main.c
 * @brief ESP-IDF demo / hardware smoke test for the EC200 AT abstraction
 *        library.
 *
 * Wires the library's three transport callbacks to an ESP32 UART and walks
 * the modem bring-up sequence, printing each step's result.  The IDF UART
 * driver's ISR-fed RX ring buffer and timeout semantics match the library's
 * read-callback contract exactly (>0 bytes, 0 on timeout, <0 on error).
 *
 * Adjust the pin/port defines below for your board wiring.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "ec200.h"

/* -------------------------------------------------------------------------
 * Board wiring — adjust for your hardware
 * ------------------------------------------------------------------------- */
#define MODEM_UART_NUM      UART_NUM_1
#define MODEM_UART_TX_PIN   (17)     /* ESP TX  -> EC200 RX */
#define MODEM_UART_RX_PIN   (18)     /* ESP RX  <- EC200 TX */
#define MODEM_UART_BAUD     (115200)
#define MODEM_RX_BUF_BYTES  (4096)
#define MODEM_APN           "internet"   /* replace with your APN */

/* -------------------------------------------------------------------------
 * Transport callbacks
 * ------------------------------------------------------------------------- */
static int modem_write(const uint8_t *data, uint16_t len, void *ctx)
{
    (void)ctx;
    return uart_write_bytes(MODEM_UART_NUM, data, len);
}

/* uart_read_bytes: >0 bytes read, 0 on timeout, <0 on error — the exact
 * contract ec200_read_fn requires. */
static int modem_read(uint8_t *data, uint16_t len,
                      uint32_t timeout_ms, void *ctx)
{
    (void)ctx;
    return uart_read_bytes(MODEM_UART_NUM, data, len,
                           pdMS_TO_TICKS(timeout_ms));
}

static void modem_delay(uint32_t ms, void *ctx)
{
    (void)ctx;
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void urc_handler(const char *urc, void *ctx)
{
    (void)ctx;
    printf("[URC] %s\n", urc);
}

/* -------------------------------------------------------------------------
 * Demo
 * ------------------------------------------------------------------------- */
static ec200_handle_t s_modem;

static void report(const char *step, ec200_status_t st)
{
    printf("%-24s %s\n", step, ec200_status_str(st));
}

void app_main(void)
{
    /* --- UART ----------------------------------------------------------- */
    const uart_config_t cfg = {
        .baud_rate  = MODEM_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(MODEM_UART_NUM, MODEM_RX_BUF_BYTES,
                                        0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART_NUM, MODEM_UART_TX_PIN,
                                 MODEM_UART_RX_PIN, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    /* --- Bring-up ------------------------------------------------------- */
    printf("\nEC200 demo (library v%d.%d.%d)\n\n",
           EC200_LIB_VERSION_MAJOR, EC200_LIB_VERSION_MINOR,
           EC200_LIB_VERSION_PATCH);

    ec200_status_t st = ec200_init(&s_modem, modem_write, modem_read,
                                   modem_delay, NULL);
    report("init", st);
    if (st != EC200_OK) {
        printf("Modem not responding — check wiring/power. Halting.\n");
        return;
    }

    ec200_set_urc_handler(&s_modem, urc_handler);
    report("cmee=2", ec200_set_cmee(&s_modem, 2));

    char imei[EC200_MAX_IMEI_LEN] = {0};
    st = ec200_get_imei(&s_modem, imei, sizeof(imei));
    report("imei", st);
    if (st == EC200_OK) {
        printf("  IMEI: %s\n", imei);
    }

    char fw[EC200_MAX_FW_VER_LEN] = {0};
    st = ec200_get_fw_version(&s_modem, fw, sizeof(fw));
    report("firmware", st);
    if (st == EC200_OK) {
        printf("  FW: %s\n", fw);
    }

    ec200_sim_status_t sim = EC200_SIM_UNKNOWN;
    st = ec200_sim_get_status(&s_modem, &sim);
    report("sim status", st);
    printf("  SIM state: %d\n", (int)sim);

    if (sim == EC200_SIM_READY) {
        printf("Waiting for network registration (60 s max)...\n");
        st = ec200_net_wait_registered(&s_modem, 60000);
        report("register", st);

        ec200_signal_quality_t sq;
        if (ec200_net_get_signal_ext(&s_modem, &sq) == EC200_OK) {
            printf("  RSSI %d dBm, RSRP %d dBm\n",
                   (int)sq.rssi, (int)sq.rsrp);
        }

        if (st == EC200_OK) {
            ec200_pdp_context_t pdp = {
                .cid  = 1,
                .type = EC200_PDP_TYPE_IP,
                .apn  = MODEM_APN,
            };
            st = ec200_data_connect(&s_modem, &pdp);
            report("pdp connect", st);
            if (st == EC200_OK) {
                printf("  IP: %s\n", pdp.ip_addr);
            }
        }
    }

    /* --- Idle: keep dispatching unsolicited events ----------------------- */
    printf("\nEntering URC poll loop.\n");
    for (;;) {
        (void)ec200_at_poll_urc(&s_modem, 100);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
