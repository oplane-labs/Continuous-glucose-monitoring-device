/**
 * @file device_config.h
 * @brief Compile-time configuration for the GlucoSense CGM-3000
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

/* --- Product identification --- */
#define DEVICE_MODEL            "CGM-3000"
#define DEVICE_MANUFACTURER     "GlucoSense Medical"
#define FIRMWARE_VERSION_MAJOR  2
#define FIRMWARE_VERSION_MINOR  4
#define FIRMWARE_VERSION_PATCH  1

/* --- Sensor configuration --- */
#define CONFIG_ADC_CHANNEL              0
#define CONFIG_ADC_GAIN                 4       /* Transimpedance amplifier gain */
#define CONFIG_SENSOR_STABILIZE_MV      535     /* Stabilization voltage (mV) */
#define CONFIG_WARMUP_STABILIZE_MS      1800000 /* 30 min stabilization phase */
#define CONFIG_WARMUP_CALIBRATE_MS      1800000 /* 30 min calibration phase */
#define CONFIG_FAULT_DETECT_TIMEOUT_MS  10000   /* Fault detection window */
#define CONFIG_SNR_THRESHOLD_DB         3.0f    /* Minimum signal-to-noise ratio */

/* --- Signal processing --- */
#define CONFIG_FILTER_ORDER             4       /* Butterworth filter order */
#define CONFIG_FILTER_CUTOFF_HZ         0.1f    /* Low-pass cutoff frequency */
#define CONFIG_OUTLIER_WINDOW_SAMPLES   180     /* 15 minutes at 10 Hz / 5 min */
#define CONFIG_OUTLIER_SIGMA_THRESHOLD  3.0f    /* Standard deviation threshold */
#define CONFIG_RATE_CALC_POINTS         3       /* Data points for rate regression */

/* --- Calibration defaults --- */
#define CONFIG_DEFAULT_SENSITIVITY      0.12f   /* nA per mg/dL (typical) */
#define CONFIG_DEFAULT_OFFSET           1.5f    /* Baseline offset (nA) */
#define CONFIG_DEFAULT_TEMP_COEFF       0.002f  /* %/°C deviation from 37°C */
#define CONFIG_CAL_REFERENCE_BODY_TEMP  37.0f   /* Reference body temperature (°C) */
#define CONFIG_CAL_FORGETTING_FACTOR    0.95f   /* Exponential forgetting lambda */
#define CONFIG_CAL_MAX_DEVIATION_PCT    40      /* Max reference deviation (%) */

/* --- Alert defaults --- */
#define CONFIG_LOW_GLUCOSE_DEFAULT      55      /* mg/dL */
#define CONFIG_LOW_GLUCOSE_MIN          50      /* mg/dL */
#define CONFIG_LOW_GLUCOSE_MAX          80      /* mg/dL */
#define CONFIG_HIGH_GLUCOSE_DEFAULT     250     /* mg/dL */
#define CONFIG_HIGH_GLUCOSE_MIN         120     /* mg/dL */
#define CONFIG_HIGH_GLUCOSE_MAX         400     /* mg/dL */
#define CONFIG_RAPID_FALL_RATE          (-2)    /* mg/dL/min */
#define CONFIG_RAPID_RISE_RATE          3       /* mg/dL/min */
/* Device-supported bounds for the remotely-configurable rate thresholds
 * (SWR-045 SOCP). The fall threshold must stay strictly negative and the
 * rise threshold strictly positive, otherwise the corresponding rate alert
 * is either disabled (extreme magnitude) or fires on normal drift (magnitude
 * near zero). Magnitudes are clamped to a clinically plausible 1..10 mg/dL/min. */
#define CONFIG_RAPID_FALL_RATE_MIN      (-10)   /* mg/dL/min (least sensitive) */
#define CONFIG_RAPID_FALL_RATE_MAX      (-1)    /* mg/dL/min (most sensitive) */
#define CONFIG_RAPID_RISE_RATE_MIN      1       /* mg/dL/min (most sensitive) */
#define CONFIG_RAPID_RISE_RATE_MAX      10      /* mg/dL/min (least sensitive) */
#define CONFIG_LOW_ALERT_CONSECUTIVE    2       /* Readings before alert */
#define CONFIG_HIGH_ALERT_CONSECUTIVE   3       /* Readings before alert */
#define CONFIG_RATE_ALERT_CONSECUTIVE   3       /* Readings before rate alert */
#define CONFIG_CRITICAL_LOW_MGDL        45      /* Non-snoozable threshold */
#define CONFIG_SIGNAL_LOSS_TIMEOUT_MS   1200000 /* 20 minutes */

/* --- BLE configuration --- */
#define CONFIG_BLE_DEVICE_NAME          "GlucoSense CGM"
#define CONFIG_BLE_ADV_INTERVAL_FAST_MS 1000
#define CONFIG_BLE_ADV_INTERVAL_SLOW_MS 2000
#define CONFIG_BLE_ADV_SLOWDOWN_MS      300000  /* 5 min until slow advertising */
#define CONFIG_BLE_TX_POWER_DBM         0
#define CONFIG_BLE_CONN_INTERVAL_MIN_MS 50
#define CONFIG_BLE_CONN_INTERVAL_MAX_MS 200

/* --- Flash storage --- */
#define CONFIG_FLASH_BUFFER_CAPACITY    96      /* 8 hours at 5-min intervals */
#define CONFIG_FLASH_PAGE_SIZE          4096
#define CONFIG_FLASH_SECTOR_SIZE        65536
#define CONFIG_FLASH_CAL_OFFSET         0x00000 /* Calibration data region */
#define CONFIG_FLASH_DATA_OFFSET        0x10000 /* Measurement data region */
#define CONFIG_FLASH_DATA_SIZE          0x40000 /* 256 KB for measurements */

/* --- Power management --- */
#define CONFIG_BATTERY_CHECK_INTERVAL_MS    3600000 /* 1 hour */
#define CONFIG_BATTERY_LOW_THRESHOLD_PCT    10      /* ~24 hours remaining */
#define CONFIG_BATTERY_CRITICAL_MV          2000    /* Shutdown threshold */
#define CONFIG_BATTERY_FULL_MV              3000    /* CR2032 nominal voltage */
#define CONFIG_SLEEP_CURRENT_UA             5       /* Target sleep current */

/* --- Watchdog --- */
#define CONFIG_WATCHDOG_TIMEOUT_MS      4000

#endif /* DEVICE_CONFIG_H */
