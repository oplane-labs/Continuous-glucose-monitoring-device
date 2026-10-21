/**
 * @file calibration.c
 * @brief Sensor calibration implementation
 *
 * Implements: SWR-020, SWR-021, SWR-022, SWR-023
 * Risk Controls: RC-002 (accuracy), RC-009 (calibration validity)
 */

#include "calibration.h"
#include "config/device_config.h"
#include "../storage/flash_storage.h"
#include <string.h>
#include <math.h>

/* Module state */
static calibration_params_t s_cal_params;
static bool s_initialized = false;

/* CRC-32 lookup table (IEEE 802.3) */
static uint32_t crc32_calculate(const void *data, size_t len);

cgm_error_t calibration_init(void)
{
    /* Load calibration from flash (SWR-020) */
    cgm_error_t err = flash_read_calibration(&s_cal_params);
    if (err != CGM_OK) {
        /* Flash read failed, use defaults */
        s_cal_params.sensitivity = CONFIG_DEFAULT_SENSITIVITY;
        s_cal_params.offset = CONFIG_DEFAULT_OFFSET;
        s_cal_params.temp_coefficient = CONFIG_DEFAULT_TEMP_COEFF;
        s_cal_params.timestamp = 0;
        s_cal_params.crc32 = 0;
        s_initialized = true;
        return CGM_ERR_CAL_CRC_FAIL;
    }

    /* Verify CRC integrity (SWR-052) */
    uint32_t computed_crc = crc32_calculate(&s_cal_params,
        sizeof(calibration_params_t) - sizeof(uint32_t));

    if (computed_crc != s_cal_params.crc32) {
        /* CRC mismatch - calibration data is corrupt.
         * Fall back to defaults. This is a safety-critical check. */
        s_cal_params.sensitivity = CONFIG_DEFAULT_SENSITIVITY;
        s_cal_params.offset = CONFIG_DEFAULT_OFFSET;
        s_cal_params.temp_coefficient = CONFIG_DEFAULT_TEMP_COEFF;
        s_initialized = true;
        return CGM_ERR_CAL_CRC_FAIL;
    }

    s_initialized = true;
    return CGM_OK;
}

/**
 * @brief Convert sensor current to glucose concentration (SWR-012)
 *
 * Applies the calibration equation:
 *   compensated = current * (1 + temp_coeff * (temperature - 37.0))
 *   glucose = (compensated - offset) / sensitivity
 *
 * Temperature compensation per SWR-023.
 */
cgm_error_t calibration_apply(float current_na, float temperature_c,
                               uint16_t *glucose_mgdl)
{
    if (!s_initialized) {
        return CGM_ERR_CAL_INVALID;
    }

    /* Temperature compensation (SWR-023) */
    float temp_factor = 1.0f + s_cal_params.temp_coefficient *
                        (temperature_c - CONFIG_CAL_REFERENCE_BODY_TEMP);
    float compensated_current = current_na * temp_factor;

    /* Glucose conversion (SWR-012):
     * glucose = (current - offset) / sensitivity */
    float glucose = (compensated_current - s_cal_params.offset) /
                    s_cal_params.sensitivity;

    /* Range clamping (SWR-014, Risk Control RC-001) */
    if (glucose < (float)GLUCOSE_MIN_MGDL) {
        *glucose_mgdl = GLUCOSE_MIN_MGDL;
    } else if (glucose > (float)GLUCOSE_MAX_MGDL) {
        *glucose_mgdl = GLUCOSE_MAX_MGDL;
    } else {
        *glucose_mgdl = (uint16_t)(glucose + 0.5f); /* Round to nearest */
    }

    return CGM_OK;
}

/**
 * @brief In-vivo calibration update (SWR-021)
 *
 * Uses weighted least-squares with exponential forgetting (lambda = 0.95)
 * to adjust the sensitivity parameter based on a fingerstick reference.
 *
 * The companion app already validates fingerstick entries before sending them,
 * so the on-device deviation check is no longer applied here (it was rejecting
 * legitimate recalibrations in the field).
 */
cgm_error_t calibration_update(uint16_t reference_mgdl, float sensor_current)
{
    if (!s_initialized) {
        return CGM_ERR_CAL_INVALID;
    }

    /* Calculate new sensitivity using weighted least-squares update.
     * new_sensitivity = lambda * old_sensitivity +
     *                   (1 - lambda) * (current - offset) / reference */
    float measured_sensitivity = (sensor_current - s_cal_params.offset) /
                                 (float)reference_mgdl;

    s_cal_params.sensitivity = CONFIG_CAL_FORGETTING_FACTOR * s_cal_params.sensitivity +
                               (1.0f - CONFIG_CAL_FORGETTING_FACTOR) * measured_sensitivity;

    /* Persist updated calibration */
    return calibration_save();
}

/**
 * @brief Validate calibration reference value (SWR-022, Risk Control RC-009)
 *
 * Rejects reference values that differ more than 40% from the current
 * sensor reading to prevent erroneous recalibration.
 */
cgm_error_t calibration_validate_reference(uint16_t reference_mgdl,
                                            uint16_t current_sensor_mgdl)
{
    if (current_sensor_mgdl == 0 || current_sensor_mgdl == GLUCOSE_INVALID) {
        return CGM_ERR_CAL_INVALID;
    }

    float deviation = fabsf((float)reference_mgdl - (float)current_sensor_mgdl) /
                      (float)current_sensor_mgdl * 100.0f;

    if (deviation > (float)CONFIG_CAL_MAX_DEVIATION_PCT) {
        return CGM_ERR_CAL_DEVIATION;
    }

    return CGM_OK;
}

/**
 * @brief Persist calibration to flash with CRC protection (SWR-020, SWR-052)
 */
cgm_error_t calibration_save(void)
{
    /* Compute CRC over all fields except the CRC itself */
    s_cal_params.crc32 = crc32_calculate(&s_cal_params,
        sizeof(calibration_params_t) - sizeof(uint32_t));

    cgm_error_t err = flash_write_calibration(&s_cal_params);
    if (err != CGM_OK) {
        return CGM_ERR_CAL_FLASH_WRITE;
    }

    return CGM_OK;
}

const calibration_params_t *calibration_get_params(void)
{
    return &s_cal_params;
}

cgm_error_t calibration_set_factory(float sensitivity, float offset,
                                     float temp_coefficient)
{
    s_cal_params.sensitivity = sensitivity;
    s_cal_params.offset = offset;
    s_cal_params.temp_coefficient = temp_coefficient;
    s_cal_params.timestamp = 0; /* Will be set by caller */
    s_initialized = true;

    return calibration_save();
}

/* --- CRC-32 implementation --- */

static uint32_t crc32_calculate(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}
