cmake_minimum_required(VERSION 3.14)
project(ec200_at_abstraction C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wdouble-promotion
        -Wno-unused-parameter
    )
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(ec200 STATIC
    src/ec200.c
    src/ec200_at.c
    src/ec200_sim.c
    src/ec200_network.c
    src/ec200_sms.c
    src/ec200_data.c
    src/ec200_tcpip.c
    src/ec200_http.c
    src/ec200_mqtt.c
    src/ec200_gnss.c
    src/ec200_power.c
)

target_include_directories(ec200 PUBLIC include)

# Link math library if needed (used by ec200_gnss.c for nmea_to_decimal)
target_link_libraries(ec200 PUBLIC m)

# ---------------------------------------------------------------------------
# Unit-test executable
# ---------------------------------------------------------------------------
enable_testing()

add_executable(test_ec200 tests/test_ec200.c)
target_link_libraries(test_ec200 PRIVATE ec200)

add_test(NAME ec200_unit_tests COMMAND test_ec200)

# ---------------------------------------------------------------------------
# Wrapper-template example (compile-check only – not linked to real UART)
# ---------------------------------------------------------------------------
add_executable(wrapper_template examples/wrapper_template.c)
target_link_libraries(wrapper_template PRIVATE ec200)

# Mark the wrapper as a compile-check only target (it will link but not run)
set_target_properties(wrapper_template PROPERTIES
    EXCLUDE_FROM_ALL TRUE
)
