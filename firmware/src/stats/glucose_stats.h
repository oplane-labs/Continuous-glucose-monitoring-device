/**
 * @file glucose_stats.h
 * @brief Rolling glucose statistics (Time-in-Range, GMI, CV, mean).
 *
 * Maintains rolling 24-hour and 7-day windows of glucose readings and
 * exposes the AGP/clinical metrics consumed by the companion app and
 * BLE Glucose Statistics characteristic:
 *
 *   - Time in Range (TIR):     % of readings in [low, high] mg/dL
 *   - Time Below Range (TBR):  % of readings <  low_threshold
 *   - Time Above Range (TAR):  % of readings >  high_threshold
 *   - Mean glucose            (mg/dL)
 *   - Glucose Management
 *     Indicator (GMI):         (3.31 + 0.02392 * mean_mgdl) %
 *   - Coefficient of Variation
 *     (CV):                    100 * stddev / mean  (%)
 *
 * The module is fed exactly one valid reading per measurement cycle
 * (every 5 minutes) and uses the reading's own timestamp to age out
 * stale samples — so wall-clock drift, watchdog recovery, and sensor
 * gaps do not corrupt the windows.
 *
 * Implements: SWR-080, SWR-081, SWR-082
 */

#ifndef GLUCOSE_STATS_H
#define GLUCOSE_STATS_H

#include "cgm_types.h"
#include "config/error_codes.h"

/**
 * @brief A computed snapshot of statistics for one rolling window.
 *
 * Percentages are reported in tenths of a percent (e.g. 873 == 87.3%) so
 * the values fit in two bytes and round-trip cleanly over BLE without a
 * floating-point payload. `sample_count` is the number of valid readings
 * that contributed to this snapshot — clients should treat the metrics
 * as not-yet-clinically-meaningful when it is below the configured
 * minimum sample threshold.
 */
typedef struct {
    uint16_t sample_count;     /* Number of readings in the window */
    uint16_t mean_mgdl;        /* Mean glucose (mg/dL), 0 if empty */
    uint16_t tir_pct_x10;      /* % time in range,    0..1000 */
    uint16_t tbr_pct_x10;      /* % time below range, 0..1000 */
    uint16_t tar_pct_x10;      /* % time above range, 0..1000 */
    uint16_t gmi_pct_x10;      /* GMI in % * 10 (clinical estimated A1c) */
    uint16_t cv_pct_x10;       /* Coefficient of variation, % * 10 */
} glucose_stats_t;

/**
 * @brief Selector for which rolling window to query.
 */
typedef enum {
    STATS_WINDOW_24H = 0,
    STATS_WINDOW_7D  = 1,
    STATS_WINDOW_COUNT = 2
} stats_window_t;

/**
 * @brief Initialise (or reset) the statistics module.
 * Clears both rolling windows and applies the default in-range band
 * (CONFIG_STATS_TIR_LOW .. CONFIG_STATS_TIR_HIGH).
 */
cgm_error_t stats_init(void);

/**
 * @brief Record a valid glucose reading into the rolling windows (SWR-080).
 *
 * Readings that are flagged invalid (GLUCOSE_INVALID) or whose value sits
 * outside the physiological measurement range are silently dropped.
 * Older entries that have rolled outside both windows are evicted before
 * the new sample is appended.
 *
 * @param[in] reading  A reading produced by the signal pipeline. Must
 *                     carry a monotonically non-decreasing `timestamp`
 *                     (Unix epoch seconds).
 */
cgm_error_t stats_record_reading(const glucose_reading_t *reading);

/**
 * @brief Compute statistics for one window (SWR-081).
 *
 * Returns CGM_ERR_SIGNAL_INSUFFICIENT (and zeroes `out`) when the window
 * holds fewer than CONFIG_STATS_MIN_SAMPLES readings — clinical metrics
 * computed from a near-empty window are misleading.
 *
 * @param[in]  window  STATS_WINDOW_24H or STATS_WINDOW_7D.
 * @param[in]  now_unix_s  Current Unix epoch time, used to evict stale
 *                         entries before the snapshot is taken.
 * @param[out] out     Populated with the computed snapshot on success.
 */
cgm_error_t stats_get(stats_window_t window, uint32_t now_unix_s,
                      glucose_stats_t *out);

/**
 * @brief Configure the in-range thresholds (mg/dL).
 *
 * Validates that low < high, that low >= 50 mg/dL, and that high <= 300
 * mg/dL. Standard clinical defaults are 70 / 180 mg/dL (ATTD 2019).
 */
cgm_error_t stats_set_range(uint16_t low_mgdl, uint16_t high_mgdl);

/**
 * @brief Discard all buffered samples (e.g. after sensor replacement).
 */
void stats_clear(void);

#endif /* GLUCOSE_STATS_H */
