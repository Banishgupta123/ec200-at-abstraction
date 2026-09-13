# Select ONE entry point: the demo, or the exhaustive API test harness.
set(EC200_APP_SRC "ec200_test_harness.c")   # or "ec200_demo_main.c"

# IDF 5.3 split the monolithic "driver" component into per-peripheral
# esp_driver_* components.  The library itself pulls in no IDF component at
# all; only this example needs UART and GPIO, so pick the names that exist on
# the IDF being built against.  Without this the example fails to configure on
# IDF < 5.3 with "Failed to resolve component 'esp_driver_uart'".
if(IDF_VERSION_MAJOR GREATER 5 OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    set(EC200_DRIVER_REQS esp_driver_uart esp_driver_gpio)
else()
    set(EC200_DRIVER_REQS driver)
endif()

idf_component_register(
    SRCS "${EC200_APP_SRC}"
    PRIV_REQUIRES ${EC200_DRIVER_REQS} esp_hw_support
)
