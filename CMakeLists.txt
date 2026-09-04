cmake_minimum_required(VERSION 3.18)

# ===========================================================================
# ESP-IDF component mode
# ---------------------------------------------------------------------------
# When this repository is consumed as an ESP-IDF component (via the IDF
# Component Manager or a components/ directory), the IDF build system defines
# ESP_PLATFORM and expects idf_component_register().  The library is pure
# C99 with no IDF dependencies, so the component is just the sources plus
# the public headers.  Everything below the return() is the host-side
# project (tests, coverage, static analysis) and never runs inside IDF.
# ===========================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS
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
            src/ec200_ppp.c
            src/ec200_file.c
            src/ec200_ssl.c
            src/ec200_ssl_socket.c
            src/ec200_time.c
        INCLUDE_DIRS include
    )
    return()
endif()

project(ec200_at_abstraction LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(EC200_BUILD_TESTS "Build the Unity/CMock unit tests" ON)
option(EC200_WERROR     "Treat warnings as errors"          ON)
option(EC200_COVERAGE   "Instrument for gcov coverage"      OFF)
option(EC200_ANALYZER   "Enable GCC -fanalyzer static analysis" OFF)

# ---------------------------------------------------------------------------
# Strict warnings (applied to first-party targets only, not fetched deps)
# ---------------------------------------------------------------------------
function(ec200_strict_warnings tgt)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${tgt} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wdouble-promotion
            -Wundef
            -Wstrict-prototypes
        )
        if(EC200_WERROR)
            target_compile_options(${tgt} PRIVATE -Werror)
        endif()
    elseif(MSVC)
        target_compile_options(${tgt} PRIVATE /W4)
        if(EC200_WERROR)
            target_compile_options(${tgt} PRIVATE /WX)
        endif()
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Coverage instrumentation
# ---------------------------------------------------------------------------
if(EC200_COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "EC200_COVERAGE requires GCC (gcov)")
    endif()
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
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
    src/ec200_ppp.c
    src/ec200_file.c
    src/ec200_ssl.c
    src/ec200_ssl_socket.c
    src/ec200_time.c
)
target_include_directories(ec200 PUBLIC include)
ec200_strict_warnings(ec200)

# The library itself gets the harshest conversion checking.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ec200 PRIVATE -Wconversion)
endif()

# Bound the worst-case stack frame: this library runs on MCUs where a task
# stack is often only a few KB, so a large local buffer is a real hazard
# (see EC200_SMS_*_BUF_LEN).  With -Werror this fails the build rather than
# letting a frame grow unnoticed.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ec200 PRIVATE -Wstack-usage=1280)
endif()

if(EC200_ANALYZER AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ec200 PRIVATE -fanalyzer)
endif()

# ---------------------------------------------------------------------------
# Unit tests: Unity + CMock + scripted loopback
# ---------------------------------------------------------------------------
if(EC200_BUILD_TESTS)
    enable_testing()
    include(FetchContent)

    FetchContent_Declare(unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG        v2.6.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(unity)

    # CMock is fetched source-only; its runtime is built below and its Ruby
    # generator produces the mocks at build time.
    FetchContent_Declare(cmock
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/CMock.git
        GIT_TAG        v2.6.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  cmake-no-subdir   # do not add_subdirectory()
    )
    FetchContent_MakeAvailable(cmock)

    # -- Loopback transport support ----------------------------------------
    add_library(test_support STATIC tests/support/loopback.c)
    target_include_directories(test_support PUBLIC tests/support)
    ec200_strict_warnings(test_support)

    # -- Loopback-driven suites --------------------------------------------
    function(add_ec200_test name)
        add_executable(${name} tests/${name}.c)
        target_link_libraries(${name} PRIVATE ec200 unity test_support)
        ec200_strict_warnings(${name})
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    add_ec200_test(test_at_engine)
    add_ec200_test(test_modules)
    add_ec200_test(test_protocols)
    add_ec200_test(test_secure)
    add_ec200_test(test_netdiag)

    # -- CMock-based transport-contract suite ------------------------------
    find_program(RUBY_EXECUTABLE ruby HINTS "C:/Ruby32-x64/bin")
    if(RUBY_EXECUTABLE)
        set(MOCK_DIR ${CMAKE_BINARY_DIR}/mocks)
        add_custom_command(
            OUTPUT  ${MOCK_DIR}/mock_transport.c ${MOCK_DIR}/mock_transport.h
            COMMAND ${RUBY_EXECUTABLE} ${cmock_SOURCE_DIR}/lib/cmock.rb
                    -o${CMAKE_CURRENT_SOURCE_DIR}/tests/cmock_config.yml
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/transport.h
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/transport.h
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmock_config.yml
            COMMENT "Generating CMock mocks from transport.h"
            VERBATIM
        )

        add_library(cmock_runtime STATIC ${cmock_SOURCE_DIR}/src/cmock.c)
        target_include_directories(cmock_runtime PUBLIC ${cmock_SOURCE_DIR}/src)
        target_link_libraries(cmock_runtime PUBLIC unity)

        add_executable(test_transport_cmock
            tests/test_transport_cmock.c
            ${MOCK_DIR}/mock_transport.c
        )
        target_include_directories(test_transport_cmock PRIVATE
            ${MOCK_DIR}
            tests/support
        )
        target_link_libraries(test_transport_cmock PRIVATE ec200 cmock_runtime)
        add_test(NAME test_transport_cmock COMMAND test_transport_cmock)
    else()
        message(WARNING "Ruby not found - CMock tests are disabled "
                        "(loopback suites still run)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Static analysis: cppcheck target
# ---------------------------------------------------------------------------
find_program(CPPCHECK_EXECUTABLE cppcheck HINTS "C:/Program Files/Cppcheck")
if(CPPCHECK_EXECUTABLE)
    add_custom_target(cppcheck
        COMMAND ${CPPCHECK_EXECUTABLE}
                --enable=warning,style,performance,portability
                --std=c99
                --language=c
                --inline-suppr
                --error-exitcode=1
                --suppress=missingIncludeSystem
                --check-level=exhaustive
                -I ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/src
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running cppcheck over src/"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# Static analysis: clang-tidy target (checks configured in .clang-tidy)
# ---------------------------------------------------------------------------
# NOTE: on Windows/MinGW clang may fail to parse the platform's own stdlib.h;
# that is an environment limitation, not a finding.  CI runs this on Linux.
find_program(CLANG_TIDY_EXECUTABLE clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
    file(GLOB EC200_TIDY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
    add_custom_target(clang-tidy
        COMMAND ${CLANG_TIDY_EXECUTABLE}
                --quiet
                -p ${CMAKE_BINARY_DIR}
                --extra-arg=-Wno-unknown-warning-option
                ${EC200_TIDY_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running clang-tidy over src/"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# Coverage report: gcovr target (requires EC200_COVERAGE=ON + a test run)
# ---------------------------------------------------------------------------
find_program(GCOVR_EXECUTABLE gcovr)
if(GCOVR_EXECUTABLE AND EC200_COVERAGE)
    get_filename_component(_toolchain_bin ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(GCOV_EXECUTABLE gcov HINTS ${_toolchain_bin})
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/coverage)
    add_custom_target(coverage
        COMMAND ${GCOVR_EXECUTABLE}
                --root ${CMAKE_CURRENT_SOURCE_DIR}
                --filter ${CMAKE_CURRENT_SOURCE_DIR}/src
                --gcov-executable ${GCOV_EXECUTABLE}
                --print-summary
                --html-details ${CMAKE_BINARY_DIR}/coverage/index.html
                ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating gcov coverage report (coverage/index.html)"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# Wrapper-template example (compile-check only)
# ---------------------------------------------------------------------------
add_executable(wrapper_template examples/wrapper_template.c)
target_link_libraries(wrapper_template PRIVATE ec200)
ec200_strict_warnings(wrapper_template)
set_target_properties(wrapper_template PROPERTIES EXCLUDE_FROM_ALL TRUE)
