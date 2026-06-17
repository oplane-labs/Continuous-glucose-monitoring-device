cmake_minimum_required(VERSION 3.20)

project(cgm3000_firmware
    VERSION 2.4.1
    DESCRIPTION "GlucoSense CGM-3000 Continuous Glucose Monitor Firmware"
    LANGUAGES C
)

# Target: nRF52840 with Zephyr RTOS
set(BOARD nrf52840dk_nrf52840)

# Compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(
    -Wall -Wextra -Werror
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
    -Os
)

# Source files
set(FIRMWARE_SOURCES
    src/main.c
    src/sensor/glucose_sensor.c
    src/sensor/calibration.c
    src/signal/signal_processing.c
    src/signal/filter.c
    src/ble/ble_manager.c
    src/ble/cgm_service.c
    src/storage/flash_storage.c
    src/alert/alert_manager.c
    src/stats/glucose_stats.c
    src/power/power_manager.c
)

# Include directories
set(FIRMWARE_INCLUDES
    include
    src
    src/config
)

# --- Production firmware target ---
if(NOT BUILD_TESTS)
    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

    target_sources(app PRIVATE ${FIRMWARE_SOURCES})
    target_include_directories(app PRIVATE ${FIRMWARE_INCLUDES})

    # Zephyr Kconfig options
    # CONFIG_BT=y
    # CONFIG_BT_PERIPHERAL=y
    # CONFIG_BT_SMP=y
    # CONFIG_BT_GATT_CGM=y
    # CONFIG_FLASH=y
    # CONFIG_SPI=y
    # CONFIG_ADC=y
    # CONFIG_WATCHDOG=y
    # CONFIG_PM=y

# --- Unit test build ---
else()
    enable_testing()

    add_compile_definitions(UNIT_TEST)

    # Unity test framework
    add_subdirectory(tests/unity EXCLUDE_FROM_ALL)

    # Test helper stubs
    add_library(test_stubs STATIC tests/test_stubs.c)
    target_include_directories(test_stubs PUBLIC include src tests)

    # Firmware library (built for host, with test stubs)
    add_library(firmware_lib STATIC ${FIRMWARE_SOURCES})
    target_include_directories(firmware_lib PUBLIC ${FIRMWARE_INCLUDES})
    target_include_directories(firmware_lib PUBLIC tests)
    target_compile_definitions(firmware_lib PUBLIC UNIT_TEST)
    target_link_libraries(firmware_lib PUBLIC m test_stubs)

    # Test executables
    add_executable(test_calibration tests/test_calibration.c)
    target_link_libraries(test_calibration firmware_lib unity)
    add_test(NAME test_calibration COMMAND test_calibration)

    add_executable(test_signal_processing tests/test_signal_processing.c)
    target_link_libraries(test_signal_processing firmware_lib unity)
    add_test(NAME test_signal_processing COMMAND test_signal_processing)

    add_executable(test_alert_manager tests/test_alert_manager.c)
    target_link_libraries(test_alert_manager firmware_lib unity)
    add_test(NAME test_alert_manager COMMAND test_alert_manager)

    add_executable(test_glucose_stats tests/test_glucose_stats.c)
    target_link_libraries(test_glucose_stats firmware_lib unity)
    add_test(NAME test_glucose_stats COMMAND test_glucose_stats)
endif()
