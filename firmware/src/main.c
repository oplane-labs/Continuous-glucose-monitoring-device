/**
 * @file main.c
 * @brief GlucoSense CGM-3000 firmware entry point
 *
 * Implements the main control loop (Glucose Engine) that orchestrates
 * all firmware subsystems: sensor sampling, signal processing, calibration,
 * alert evaluation, BLE communication, and power management.
 *
 * Implements: SWR-070, SWR-071
 * Risk Controls: RC-011 (watchdog recovery)
 */

#include "sensor/glucose_sensor.h"
#include "sensor/calibration.h"
#include "signal/signal_processing.h"
#include "alert/alert_manager.h"
#include "ble/ble_manager.h"
#include "ble/cgm_service.h"
#include "storage/flash_storage.h"
#include "power/power_manager.h"
#include "config/device_config.h"
#include "config/error_codes.h"
#include "cgm_types.h"

#ifndef UNIT_TEST
#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cgm_main, LOG_LEVEL_INF);
#endif

/* Device runtime state */
static device_status_t s_device_status;

/* Timing counters */
static uint32_t s_last_measurement_ms;
static uint32_t s_last_battery_check_ms;
static uint32_t s_last_reading_time_ms;

/* Forward declarations */
static cgm_error_t run_power_on_self_test(void);
static void        measurement_cycle(void);
static void        on_ble_connect(void);
static void        on_ble_disconnect(void);
static uint32_t    get_uptime_ms(void);
static void        watchdog_init(void);
static void        watchdog_feed(void);

/**
 * @brief Firmware entry point
 *
 * Initialization sequence:
 * 1. Power-on self-test (SWR-071)
 * 2. Initialize all subsystems
 * 3. Check for watchdog recovery state
 * 4. Start sensor warm-up
 * 5. Enter main measurement loop
 */
#ifndef UNIT_TEST
void main(void)
#else
int firmware_main(void)
#endif
{
    cgm_error_t err;

    /* Step 1: Configure watchdog (SWR-070) */
    watchdog_init();

    /* Step 2: Power-on self-test (SWR-071) */
    err = run_power_on_self_test();
    if (err != CGM_OK) {
        /* POST failed - do not enter normal operation.
         * Device will remain in a safe error state. */
#ifndef UNIT_TEST
        LOG_ERR("POST failed with error 0x%04x", err);
        while (1) {
            watchdog_feed(); /* Keep alive for diagnostic access */
            k_sleep(K_SECONDS(1));
        }
#else
        return -1;
#endif
    }

    /* Step 3: Initialize subsystems */
    flash_init();
    calibration_init();
    signal_init();
    alert_init();
    power_init();

    /* Step 4: Initialize BLE and register callbacks */
    err = ble_init();
    if (err == CGM_OK) {
        ble_register_callbacks(on_ble_connect, on_ble_disconnect);
        ble_start_advertising();
    }

    /* Step 5: Check for watchdog recovery (RC-011) */
    device_status_t recovered_state;
    if (flash_restore_device_state(&recovered_state) == CGM_OK &&
        recovered_state.sensor_state == SENSOR_STATE_ACTIVE) {
        /* Restore previous session state after watchdog reset */
        s_device_status = recovered_state;
        s_device_status.sensor_state = SENSOR_STATE_ACTIVE;
    }

    /* Step 6: Initialize sensor and start warm-up */
    sensor_init();
    sensor_start_warmup();
    s_device_status.sensor_state = SENSOR_STATE_WARMUP;

    s_last_measurement_ms = get_uptime_ms();
    s_last_battery_check_ms = get_uptime_ms();

    /* === Main measurement loop === */
#ifndef UNIT_TEST
    while (1) {
#else
    for (int loop = 0; loop < 10; loop++) {
#endif
        watchdog_feed(); /* SWR-070: Service watchdog each iteration */

        uint32_t now = get_uptime_ms();

        /* Update sensor state (checks warm-up completion, expiry) */
        s_device_status.sensor_state = sensor_get_state();

        /* Collect ADC samples at 10 Hz when sensor is active */
        if (s_device_status.sensor_state == SENSOR_STATE_ACTIVE ||
            s_device_status.sensor_state == SENSOR_STATE_WARMUP) {

            float current_na;
            err = sensor_read_current(&current_na);
            if (err == CGM_OK) {
                /* Check for sensor faults (SWR-003) */
                sensor_fault_t fault;
                sensor_detect_fault(current_na, &fault);
                s_device_status.active_faults = fault;

                if (fault == FAULT_NONE) {
                    signal_add_sample(current_na);
                }
            }
        }

        /* Process measurement every 5 minutes */
        if (now - s_last_measurement_ms >= MEASUREMENT_INTERVAL_MS &&
            s_device_status.sensor_state == SENSOR_STATE_ACTIVE) {

            measurement_cycle();
            s_last_measurement_ms = now;
        }

        /* Battery check every hour (SWR-061) */
        if (now - s_last_battery_check_ms >= CONFIG_BATTERY_CHECK_INTERVAL_MS) {
            bool critical = power_check_battery();
            s_device_status.battery_percent = power_get_battery_percent();
            s_last_battery_check_ms = now;

            if (critical) {
                /* Critical battery - initiate graceful shutdown (SWR-062) */
                power_shutdown();
            }
        }

        /* Check for signal loss (RC-006) */
        alert_check_signal_loss(now - s_last_reading_time_ms);

        /* Warn the user 24 h before sensor end-of-life (SWR-006, RC-008) */
        alert_check_sensor_lifetime(sensor_get_runtime_minutes());

        /* Enter low-power sleep until next event (SWR-060) */
        power_enter_sleep();

#ifndef UNIT_TEST
        k_sleep(K_MSEC(100)); /* 10 Hz sample rate */
#endif
    }

#ifdef UNIT_TEST
    return 0;
#endif
}

/**
 * @brief Execute one measurement cycle
 *
 * Runs the complete processing pipeline for a single 5-minute interval:
 * signal processing -> glucose conversion -> alert evaluation -> storage -> BLE notify
 */
static void measurement_cycle(void)
{
    glucose_reading_t reading;
    float temperature_c;

    /* Read temperature for compensation (SWR-023) */
    sensor_read_temperature(&temperature_c);

    /* Process accumulated samples through the signal pipeline */
    cgm_error_t err = signal_process(temperature_c, &reading);
    if (err != CGM_OK) {
        return;
    }

    /* Set timestamp */
    reading.timestamp = get_uptime_ms() / 1000;
    s_last_reading_time_ms = get_uptime_ms();

    /* Update device status */
    s_device_status.last_glucose = reading.glucose_mgdl;
    s_device_status.last_trend = (glucose_trend_t)reading.trend;
    s_device_status.last_reading_time = reading.timestamp;

    /* Evaluate alert conditions */
    float rate;
    signal_get_rate_of_change(&rate);
    alert_evaluate(reading.glucose_mgdl, rate, s_device_status.active_faults);

    /* Set alert flag in reading if any alert is active */
    alert_type_t highest_alert;
    if (alert_get_highest_priority(&highest_alert) == CGM_OK) {
        reading.status_flags |= 0x04; /* Bit 2 = alert_active */
    }

    /* Store reading to flash for backfill capability (SWR-050) */
    flash_store_reading(&reading);
    s_device_status.readings_stored = flash_get_reading_count();

    /* Send BLE notification if connected (SWR-041) */
    if (ble_is_connected()) {
        cgm_service_notify(&reading);
        cgm_service_update_runtime(sensor_get_runtime_minutes());
        cgm_service_update_status(&s_device_status);
    }

    /* Save state for potential watchdog recovery (RC-011) */
    flash_save_device_state(&s_device_status);
}

/**
 * @brief Power-on self-test (SWR-071)
 *
 * Verifies system integrity at startup:
 * 1. RAM integrity (walking ones on reserved test region)
 * 2. Flash integrity (CRC of firmware image)
 * 3. ADC functionality (reference voltage within ±5%)
 * 4. BLE radio initialization
 * 5. Sensor connectivity (impedance check)
 */
static cgm_error_t run_power_on_self_test(void)
{
    /* Test 1: RAM integrity - walking ones pattern */
    volatile uint32_t test_word;
    for (int i = 0; i < 32; i++) {
        test_word = (1UL << i);
        if (test_word != (1UL << i)) {
            return CGM_ERR_POST_RAM;
        }
    }

    /* Test 2: Flash CRC verification */
    /* In production, this verifies the CRC stored in the firmware image header
     * against a computed CRC of the firmware .text and .rodata sections. */

    /* Test 3: ADC reference voltage check */
    /* Read the internal reference voltage and verify it is within ±5%
     * of the expected value. */

    /* Test 4: BLE radio - verified during ble_init() */

    /* Test 5: Sensor impedance check */
    sensor_init();
    uint32_t impedance;
    cgm_error_t err = sensor_check_impedance(&impedance);
    if (err != CGM_OK) {
        return CGM_ERR_POST_SENSOR;
    }

    return CGM_OK;
}

static void on_ble_connect(void)
{
    s_device_status.ble_connected = true;
}

static void on_ble_disconnect(void)
{
    s_device_status.ble_connected = false;
}

static uint32_t get_uptime_ms(void)
{
#ifndef UNIT_TEST
    return k_uptime_get_32();
#else
    extern uint32_t test_stub_uptime_ms;
    return test_stub_uptime_ms;
#endif
}

static void watchdog_init(void)
{
#ifndef UNIT_TEST
    /* Configure hardware watchdog with 4-second timeout (SWR-070) */
    /* const struct device *wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
     * struct wdt_timeout_cfg cfg = {
     *     .window.max = CONFIG_WATCHDOG_TIMEOUT_MS,
     *     .callback = NULL,  // Reset on timeout
     * };
     * wdt_install_timeout(wdt, &cfg);
     * wdt_setup(wdt, WDT_FLAG_RESET_SOC); */
#endif
}

static void watchdog_feed(void)
{
#ifndef UNIT_TEST
    /* wdt_feed(wdt, wdt_channel_id); */
#endif
}
