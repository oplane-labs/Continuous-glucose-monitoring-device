/**
 * @file alert_manager.c
 * @brief Alert management implementation
 *
 * Implements: SWR-030, SWR-031, SWR-032, SWR-033, SWR-034
 * Risk Controls: RC-003, RC-004, RC-005, RC-006
 */

#include "alert_manager.h"
#include "config/device_config.h"
#include <math.h>
#include <string.h>

#ifndef UNIT_TEST
#include <zephyr/kernel.h>
#else
#include "test_stubs.h"
#endif

/* Per-alert state tracking */
typedef struct {
    bool     active;
    uint8_t  consecutive_count;  /* Consecutive readings triggering this alert */
    uint32_t snooze_until_ms;    /* 0 = not snoozed */
    uint32_t first_triggered_ms;
} alert_state_t;

/* Module state */
static struct {
    alert_config_t config;
    alert_state_t  alerts[ALERT_TYPE_COUNT];
    uint16_t       last_glucose;
    bool           initialized;
} s_alert;

/* Priority mapping (SWR-033) - lower index = higher priority */
static const alert_priority_t s_priority_map[ALERT_TYPE_COUNT] = {
    [ALERT_SENSOR_FAULT]  = ALERT_PRIORITY_CRITICAL,
    [ALERT_LOW_GLUCOSE]   = ALERT_PRIORITY_URGENT,
    [ALERT_PREDICTED_LOW] = ALERT_PRIORITY_URGENT,
    [ALERT_RAPID_FALL]    = ALERT_PRIORITY_URGENT,
    [ALERT_HIGH_GLUCOSE]  = ALERT_PRIORITY_WARNING,
    [ALERT_RAPID_RISE]    = ALERT_PRIORITY_WARNING,
    [ALERT_LOW_BATTERY]   = ALERT_PRIORITY_INFO,
    [ALERT_SIGNAL_LOSS]   = ALERT_PRIORITY_INFO,
};

static uint32_t get_uptime_ms(void);
static bool is_snoozed(alert_type_t type);

cgm_error_t alert_init(void)
{
    memset(&s_alert, 0, sizeof(s_alert));

    /* Set default thresholds */
    s_alert.config.low_glucose_threshold = CONFIG_LOW_GLUCOSE_DEFAULT;
    s_alert.config.high_glucose_threshold = CONFIG_HIGH_GLUCOSE_DEFAULT;
    s_alert.config.rapid_fall_rate = CONFIG_RAPID_FALL_RATE;
    s_alert.config.rapid_rise_rate = CONFIG_RAPID_RISE_RATE;
    s_alert.config.snooze_duration_minutes = 30; /* Default 30 min snooze */
    s_alert.config.predicted_low_threshold = CONFIG_PREDICTED_LOW_DEFAULT;
    s_alert.config.prediction_horizon_min = CONFIG_PREDICTION_HORIZON_MIN;

    s_alert.initialized = true;
    return CGM_OK;
}

/**
 * @brief Evaluate all alert conditions for a new glucose reading.
 *
 * Alert logic:
 * - Low glucose (SWR-030, RC-003): 2 consecutive readings below threshold
 * - High glucose (SWR-031, RC-004): 3 consecutive readings above threshold
 * - Rapid fall (SWR-032, RC-005): 3 consecutive rate calculations < -2 mg/dL/min
 * - Rapid rise: 3 consecutive rate calculations > +3 mg/dL/min
 * - Sensor fault: Immediate on any fault condition
 */
cgm_error_t alert_evaluate(uint16_t glucose_mgdl, float rate_mgdl_per_min,
                            sensor_fault_t sensor_fault)
{
    if (!s_alert.initialized) {
        return CGM_ERR_SIGNAL_INSUFFICIENT;
    }

    s_alert.last_glucose = glucose_mgdl;

    /* --- Sensor fault alert (immediate, highest priority) --- */
    if (sensor_fault != FAULT_NONE) {
        s_alert.alerts[ALERT_SENSOR_FAULT].active = true;
        s_alert.alerts[ALERT_SENSOR_FAULT].first_triggered_ms = get_uptime_ms();
    } else {
        s_alert.alerts[ALERT_SENSOR_FAULT].active = false;
        s_alert.alerts[ALERT_SENSOR_FAULT].consecutive_count = 0;
    }

    /* --- Low glucose alert (SWR-030, Risk Control RC-003) ---
     * Requires 2 consecutive readings below threshold to reduce
     * false alerts from transient dips. */
    if (glucose_mgdl < s_alert.config.low_glucose_threshold &&
        glucose_mgdl != GLUCOSE_INVALID) {
        s_alert.alerts[ALERT_LOW_GLUCOSE].consecutive_count++;
        if (s_alert.alerts[ALERT_LOW_GLUCOSE].consecutive_count >=
            CONFIG_LOW_ALERT_CONSECUTIVE) {
            if (!is_snoozed(ALERT_LOW_GLUCOSE)) {
                s_alert.alerts[ALERT_LOW_GLUCOSE].active = true;
                s_alert.alerts[ALERT_LOW_GLUCOSE].first_triggered_ms = get_uptime_ms();
            }
        }
    } else {
        s_alert.alerts[ALERT_LOW_GLUCOSE].active = false;
        s_alert.alerts[ALERT_LOW_GLUCOSE].consecutive_count = 0;
    }

    /* --- Predicted low alert (SWR-035) ---
     * Project current glucose forward by the configured horizon using the
     * measured rate of change. Sensor input is untrusted and is validated
     * against physiological plausibility bounds before use:
     *   - glucose must not be GLUCOSE_INVALID,
     *   - glucose must lie within [GLUCOSE_MIN_MGDL, GLUCOSE_MAX_MGDL],
     *   - rate must be finite (no NaN / Inf propagating from upstream),
     *   - |rate| must not exceed CONFIG_PREDICTION_RATE_MAX_ABS,
     *   - sensor must not currently report a fault,
     *   - LOW_GLUCOSE must not already be active (no double-firing).
     * Inputs that fail validation skip the prediction path entirely; any
     * existing PREDICTED_LOW remains in its prior state rather than being
     * silently cleared by bad data. Fires when the projection (linear
     * extrapolation of rate over horizon) falls below the configured
     * threshold while glucose is trending down at >= 1 mg/dL/min. Cleared
     * with hysteresis once the projection recovers above threshold +
     * CONFIG_PREDICTED_LOW_CLEAR_HYST. */
    if (s_alert.alerts[ALERT_LOW_GLUCOSE].active ||
        sensor_fault != FAULT_NONE) {
        /* Real low or sensor fault active — predicted-low is redundant or
         * unsafe to compute. Suppress it. */
        s_alert.alerts[ALERT_PREDICTED_LOW].active = false;
    } else {
        bool inputs_valid =
            (glucose_mgdl != GLUCOSE_INVALID) &&
            (glucose_mgdl >= CONFIG_PREDICTION_GLUCOSE_MIN) &&
            (glucose_mgdl <= CONFIG_PREDICTION_GLUCOSE_MAX) &&
            isfinite(rate_mgdl_per_min) &&
            (fabsf(rate_mgdl_per_min) <= CONFIG_PREDICTION_RATE_MAX_ABS);

        if (inputs_valid) {
            float horizon = (float)s_alert.config.prediction_horizon_min;
            float predicted =
                (float)glucose_mgdl + rate_mgdl_per_min * horizon;
            uint16_t threshold = s_alert.config.predicted_low_threshold;
            bool falling =
                rate_mgdl_per_min <= CONFIG_PREDICTION_MIN_FALL_RATE;

            if (falling && predicted < (float)threshold) {
                if (!is_snoozed(ALERT_PREDICTED_LOW) &&
                    !s_alert.alerts[ALERT_PREDICTED_LOW].active) {
                    s_alert.alerts[ALERT_PREDICTED_LOW].active = true;
                    s_alert.alerts[ALERT_PREDICTED_LOW].first_triggered_ms =
                        get_uptime_ms();
                }
            } else if (s_alert.alerts[ALERT_PREDICTED_LOW].active &&
                       predicted >= (float)(threshold +
                                            CONFIG_PREDICTED_LOW_CLEAR_HYST)) {
                s_alert.alerts[ALERT_PREDICTED_LOW].active = false;
            }
        }
        /* If inputs_valid is false, leave prior state untouched: a corrupt
         * sample must neither create nor clear an alert. */
    }

    /* --- High glucose alert (SWR-031, Risk Control RC-004) ---
     * Requires 3 consecutive readings above threshold. */
    if (glucose_mgdl > s_alert.config.high_glucose_threshold) {
        s_alert.alerts[ALERT_HIGH_GLUCOSE].consecutive_count++;
        if (s_alert.alerts[ALERT_HIGH_GLUCOSE].consecutive_count >=
            CONFIG_HIGH_ALERT_CONSECUTIVE) {
            if (!is_snoozed(ALERT_HIGH_GLUCOSE)) {
                s_alert.alerts[ALERT_HIGH_GLUCOSE].active = true;
                s_alert.alerts[ALERT_HIGH_GLUCOSE].first_triggered_ms = get_uptime_ms();
            }
        }
    } else {
        s_alert.alerts[ALERT_HIGH_GLUCOSE].active = false;
        s_alert.alerts[ALERT_HIGH_GLUCOSE].consecutive_count = 0;
    }

    /* --- Rapid fall alert (SWR-032, Risk Control RC-005) ---
     * 3 consecutive rate calculations below -2 mg/dL/min */
    if (rate_mgdl_per_min < (float)s_alert.config.rapid_fall_rate) {
        s_alert.alerts[ALERT_RAPID_FALL].consecutive_count++;
        if (s_alert.alerts[ALERT_RAPID_FALL].consecutive_count >=
            CONFIG_RATE_ALERT_CONSECUTIVE) {
            if (!is_snoozed(ALERT_RAPID_FALL)) {
                s_alert.alerts[ALERT_RAPID_FALL].active = true;
            }
        }
    } else {
        s_alert.alerts[ALERT_RAPID_FALL].active = false;
        s_alert.alerts[ALERT_RAPID_FALL].consecutive_count = 0;
    }

    /* --- Rapid rise alert --- */
    if (rate_mgdl_per_min > (float)s_alert.config.rapid_rise_rate) {
        s_alert.alerts[ALERT_RAPID_RISE].consecutive_count++;
        if (s_alert.alerts[ALERT_RAPID_RISE].consecutive_count >=
            CONFIG_RATE_ALERT_CONSECUTIVE) {
            if (!is_snoozed(ALERT_RAPID_RISE)) {
                s_alert.alerts[ALERT_RAPID_RISE].active = true;
            }
        }
    } else {
        s_alert.alerts[ALERT_RAPID_RISE].active = false;
        s_alert.alerts[ALERT_RAPID_RISE].consecutive_count = 0;
    }

    return CGM_OK;
}

bool alert_is_active(alert_type_t type)
{
    if (type >= ALERT_TYPE_COUNT) {
        return false;
    }
    return s_alert.alerts[type].active;
}

/**
 * @brief Get highest priority active alert (SWR-033)
 * Alerts are enumerated in priority order (index 0 = highest).
 */
cgm_error_t alert_get_highest_priority(alert_type_t *type)
{
    for (int i = 0; i < ALERT_TYPE_COUNT; i++) {
        if (s_alert.alerts[i].active) {
            *type = (alert_type_t)i;
            return CGM_OK;
        }
    }
    return CGM_ERR_SIGNAL_INSUFFICIENT; /* No active alerts */
}

/**
 * @brief Snooze an alert (SWR-034)
 *
 * Non-critical alerts can be snoozed for a configurable duration.
 * Critical alerts cannot be snoozed:
 * - SENSOR_FAULT is always non-snoozable
 * - LOW_GLUCOSE with reading < 45 mg/dL is non-snoozable
 */
cgm_error_t alert_snooze(alert_type_t type)
{
    if (type >= ALERT_TYPE_COUNT) {
        return CGM_ERR_SIGNAL_INSUFFICIENT;
    }

    /* Critical alerts cannot be snoozed (SWR-034) */
    if (type == ALERT_SENSOR_FAULT) {
        return CGM_ERR_SIGNAL_INSUFFICIENT;
    }

    if (type == ALERT_LOW_GLUCOSE &&
        s_alert.last_glucose < CONFIG_CRITICAL_LOW_MGDL) {
        return CGM_ERR_SIGNAL_INSUFFICIENT;
    }

    uint32_t snooze_ms = (uint32_t)s_alert.config.snooze_duration_minutes * 60000;
    s_alert.alerts[type].snooze_until_ms = get_uptime_ms() + snooze_ms;
    s_alert.alerts[type].active = false;

    return CGM_OK;
}

cgm_error_t alert_set_config(const alert_config_t *config)
{
    /* Validate threshold ranges */
    if (config->low_glucose_threshold < CONFIG_LOW_GLUCOSE_MIN ||
        config->low_glucose_threshold > CONFIG_LOW_GLUCOSE_MAX) {
        return CGM_ERR_CAL_REFERENCE_OOR;
    }
    if (config->high_glucose_threshold < CONFIG_HIGH_GLUCOSE_MIN ||
        config->high_glucose_threshold > CONFIG_HIGH_GLUCOSE_MAX) {
        return CGM_ERR_CAL_REFERENCE_OOR;
    }
    /* Predicted-low threshold must sit between the hard low alert and a
     * sensible upper bound; horizon must be non-zero (SWR-035). */
    if (config->predicted_low_threshold <= config->low_glucose_threshold ||
        config->predicted_low_threshold > 100) {
        return CGM_ERR_CAL_REFERENCE_OOR;
    }
    if (config->prediction_horizon_min == 0 ||
        config->prediction_horizon_min > 60) {
        return CGM_ERR_CAL_REFERENCE_OOR;
    }

    s_alert.config = *config;
    return CGM_OK;
}

const alert_config_t *alert_get_config(void)
{
    return &s_alert.config;
}

/**
 * @brief Check for signal loss condition (RC-006)
 * Activates SIGNAL_LOSS alert if no valid reading for 20 minutes.
 */
cgm_error_t alert_check_signal_loss(uint32_t ms_since_last_reading)
{
    if (ms_since_last_reading >= CONFIG_SIGNAL_LOSS_TIMEOUT_MS) {
        s_alert.alerts[ALERT_SIGNAL_LOSS].active = true;
    } else {
        s_alert.alerts[ALERT_SIGNAL_LOSS].active = false;
    }
    return CGM_OK;
}

void alert_clear_all(void)
{
    for (int i = 0; i < ALERT_TYPE_COUNT; i++) {
        memset(&s_alert.alerts[i], 0, sizeof(alert_state_t));
    }
}

/* --- Internal functions --- */

static bool is_snoozed(alert_type_t type)
{
    uint32_t now = get_uptime_ms();
    return s_alert.alerts[type].snooze_until_ms > now;
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
