# Select ONE entry point: the demo, or the exhaustive API test harness.
set(EC200_APP_SRC "ec200_test_harness.c")   # or "ec200_demo_main.c"

idf_component_register(
    SRCS "${EC200_APP_SRC}"
    PRIV_REQUIRES esp_driver_uart esp_driver_gpio esp_hw_support
)
