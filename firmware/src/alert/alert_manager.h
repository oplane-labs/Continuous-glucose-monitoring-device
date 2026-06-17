/**
 * @file alert_manager.h
 * @brief Glucose alert and alarm management
 *
 * Evaluates glucose readings against configurable thresholds and generates
 * prioritized alerts for hypo/hyperglycemia, rapid rate changes, sensor
 * faults, and system conditions.
 *
 * Implements: SWR-006, SWR-030, SWR-031, SWR-032, SWR-033, SWR-034
 */

#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include "cgm_types.h"
#include "config/error_codes.h"

/**
 * @brief Initialize alert manager with default thresholds.
 */
cgm_error_t alert_init(void);

/**
 * @brief Evaluate a new glucose reading against all alert conditions.
 * Checks low glucose, high glucose, rapid fall/rise, and signal loss.
 * @param[in] glucose_mgdl      Current glucose reading in mg/dL.
 * @param[in] rate_mgdl_per_min Current rate of change.
 * @param[in] sensor_fault      Current sensor fault status.
 */
cgm_error_t alert_evaluate(uint16_t glucose_mgdl, float rate_mgdl_per_min,
                            sensor_fault_t sensor_fault);

/**
 * @brief Check if a specific alert is currently active.
 */
bool alert_is_active(alert_type_t type);

/**
 * @brief Get the highest-priority active alert (SWR-033).
 * @param[out] type  The highest priority active alert type.
 * @return CGM_OK if an alert is active, CGM_ERR_SIGNAL_INSUFFICIENT if none.
 */
cgm_error_t alert_get_highest_priority(alert_type_t *type);

/**
 * @brief Snooze an alert for the configured duration (SWR-034).
 * Critical alerts (SENSOR_FAULT, LOW_GLUCOSE < 45 mg/dL) cannot be snoozed.
 * @return CGM_OK or error if the alert is not snoozable.
 */
cgm_error_t alert_snooze(alert_type_t type);

/**
 * @brief Update alert configuration thresholds.
 */
cgm_error_t alert_set_config(const alert_config_t *config);

/**
 * @brief Get current alert configuration.
 */
const alert_config_t *alert_get_config(void);

/**
 * @brief Update sensor lifetime status; raises ALERT_SENSOR_EXPIRING when
 * the remaining sensor runtime falls below the configured warning lead time
 * (SWR-006). The alert clears automatically once the sensor is replaced
 * (runtime resets) and is replaced by ALERT_SENSOR_FAULT/EXPIRED at EOL.
 * @param[in] runtime_minutes  Cumulative sensor runtime in minutes.
 */
cgm_error_t alert_check_sensor_lifetime(uint32_t runtime_minutes);

/**
 * @brief Notify alert manager that a signal loss condition may exist.
 * Called when no valid reading has been received for a period.
 * @param[in] ms_since_last_reading Milliseconds since last valid reading.
 */
cgm_error_t alert_check_signal_loss(uint32_t ms_since_last_reading);

/**
 * @brief Clear all active alerts. Used during sensor replacement.
 */
void alert_clear_all(void);

#endif /* ALERT_MANAGER_H */
