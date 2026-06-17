/**
 * @file cgm_types.h
 * @brief Common type definitions for the GlucoSense CGM-3000 firmware
 *
 * This header defines the shared data types used across all firmware modules.
 * All glucose values use fixed-point Q16.16 representation internally and
 * are converted to mg/dL integers at the application boundary.
 */

#ifndef CGM_TYPES_H
#define CGM_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Fixed-point Q16.16 type for internal calculations */
typedef int32_t q16_16_t;

#define Q16_16_ONE      (1 << 16)
#define Q16_16_HALF     (1 << 15)
#define Q16_16_FROM_INT(x)   ((q16_16_t)((x) << 16))
#define Q16_16_TO_INT(x)     ((int16_t)((x) >> 16))
#define Q16_16_FROM_FLOAT(x) ((q16_16_t)((x) * 65536.0f))
#define Q16_16_MUL(a, b)     ((q16_16_t)(((int64_t)(a) * (b)) >> 16))
#define Q16_16_DIV(a, b)     ((q16_16_t)(((int64_t)(a) << 16) / (b)))

/* Glucose measurement range (mg/dL) */
#define GLUCOSE_MIN_MGDL        40
#define GLUCOSE_MAX_MGDL        400
#define GLUCOSE_INVALID         0xFFFF

/* Sensor operational limits */
#define SENSOR_CURRENT_MIN_NA   0.5f    /* Minimum valid sensor current (nA) */
#define SENSOR_CURRENT_MAX_NA   50.0f   /* Maximum valid sensor current (nA) */
#define SENSOR_FAULT_LOW_NA     0.1f    /* Open circuit threshold (nA) */
#define SENSOR_FAULT_HIGH_NA    100.0f  /* Short circuit threshold (nA) */
#define SENSOR_LIFETIME_MINUTES 20160   /* 14 days */

/* ADC configuration */
#define ADC_RESOLUTION_BITS     12
#define ADC_SAMPLE_RATE_HZ      10
#define ADC_REFERENCE_MV        3300

/* Measurement timing */
#define MEASUREMENT_INTERVAL_MS     300000  /* 5 minutes */
#define WARMUP_DURATION_MS          3600000 /* 60 minutes */
#define SAMPLES_PER_MEASUREMENT     3000    /* 10 Hz * 300 seconds */

/**
 * @brief Glucose trend direction indicators
 * Implements SYS-REQ-020 trend arrow categories.
 */
typedef enum {
    TREND_FALLING_RAPIDLY = 0,  /* < -3 mg/dL/min */
    TREND_FALLING         = 1,  /* -3 to -1 mg/dL/min */
    TREND_STABLE          = 2,  /* -1 to +1 mg/dL/min */
    TREND_RISING          = 3,  /* +1 to +3 mg/dL/min */
    TREND_RISING_RAPIDLY  = 4,  /* > +3 mg/dL/min */
    TREND_UNKNOWN         = 5
} glucose_trend_t;

/**
 * @brief Sensor operational state
 */
typedef enum {
    SENSOR_STATE_IDLE      = 0,
    SENSOR_STATE_WARMUP    = 1,
    SENSOR_STATE_ACTIVE    = 2,
    SENSOR_STATE_FAULT     = 3,
    SENSOR_STATE_EXPIRED   = 4,
    SENSOR_STATE_SHUTDOWN  = 5
} sensor_state_t;

/**
 * @brief Sensor fault type codes
 */
typedef enum {
    FAULT_NONE          = 0x00,
    FAULT_OPEN_CIRCUIT  = 0x01,
    FAULT_SHORT_CIRCUIT = 0x02,
    FAULT_NOISE         = 0x04,
    FAULT_CALIBRATION   = 0x08,
    FAULT_TEMPERATURE   = 0x10
} sensor_fault_t;

/**
 * @brief Alert type codes, ordered by priority (highest first)
 * Implements SWR-033 alert prioritization.
 */
typedef enum {
    ALERT_SENSOR_FAULT       = 0,   /* Priority: Critical */
    ALERT_LOW_GLUCOSE        = 1,   /* Priority: Urgent */
    ALERT_RAPID_FALL         = 2,   /* Priority: Urgent */
    ALERT_HIGH_GLUCOSE       = 3,   /* Priority: Warning */
    ALERT_RAPID_RISE         = 4,   /* Priority: Warning */
    ALERT_SENSOR_EXPIRING    = 5,   /* Priority: Warning (SWR-006) */
    ALERT_LOW_BATTERY        = 6,   /* Priority: Info */
    ALERT_SIGNAL_LOSS        = 7,   /* Priority: Info */
    ALERT_TYPE_COUNT         = 8
} alert_type_t;

/**
 * @brief Alert priority levels
 */
typedef enum {
    ALERT_PRIORITY_INFO     = 0,
    ALERT_PRIORITY_WARNING  = 1,
    ALERT_PRIORITY_URGENT   = 2,
    ALERT_PRIORITY_CRITICAL = 3
} alert_priority_t;

/**
 * @brief A single glucose measurement record
 * Stored in flash and transmitted over BLE.
 * Total size: 8 bytes (packed) per SWR-050.
 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;         /* Unix epoch seconds */
    uint16_t glucose_mgdl;      /* Glucose in mg/dL or GLUCOSE_INVALID */
    uint8_t  status_flags;      /* Bit field: [0]=valid, [1]=calibrated, [2]=alert_active */
    uint8_t  trend;             /* glucose_trend_t */
} glucose_reading_t;

/**
 * @brief Calibration parameters stored in flash (SWR-020)
 */
typedef struct {
    float    sensitivity;       /* nA per mg/dL */
    float    offset;            /* Baseline current offset (nA) */
    float    temp_coefficient;  /* Temperature correction coefficient */
    uint32_t timestamp;         /* When calibration was last updated */
    uint32_t crc32;             /* Integrity check */
} calibration_params_t;

/**
 * @brief Alert configuration thresholds
 */
typedef struct {
    uint16_t low_glucose_threshold;     /* Default: 55 mg/dL */
    uint16_t high_glucose_threshold;    /* Default: 250 mg/dL */
    int16_t  rapid_fall_rate;           /* Default: -2 mg/dL/min (Q8.8) */
    int16_t  rapid_rise_rate;           /* Default: +3 mg/dL/min (Q8.8) */
    uint16_t snooze_duration_minutes;   /* 0 = disabled */
    uint16_t expiring_soon_warning_min; /* Minutes before expiry to warn (SWR-006) */
} alert_config_t;

/**
 * @brief Device runtime status
 */
typedef struct {
    sensor_state_t  sensor_state;
    sensor_fault_t  active_faults;
    uint32_t        sensor_start_time;
    uint32_t        last_reading_time;
    uint16_t        last_glucose;
    glucose_trend_t last_trend;
    uint8_t         battery_percent;
    bool            ble_connected;
    uint16_t        readings_stored;
} device_status_t;

#endif /* CGM_TYPES_H */
