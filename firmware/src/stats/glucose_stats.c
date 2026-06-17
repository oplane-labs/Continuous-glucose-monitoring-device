/**
 * @file glucose_stats.c
 * @brief Rolling glucose statistics implementation.
 *
 * Implements: SWR-080, SWR-081, SWR-082
 */

#include "glucose_stats.h"
#include "config/device_config.h"
#include <math.h>
#include <string.h>

/* One sample slot in the ring buffer.
 * 6 bytes per slot keeps the 7-day buffer at ~12 KB
 * (2016 samples) — well within the application RAM budget. */
typedef struct {
    uint32_t timestamp;   /* Unix epoch seconds */
    uint16_t glucose;     /* mg/dL */
} stats_sample_t;

#define STATS_BUF_CAPACITY  CONFIG_STATS_BUFFER_CAPACITY

/* Module state */
static struct {
    stats_sample_t buf[STATS_BUF_CAPACITY];
    uint16_t       head;     /* Next write slot */
    uint16_t       count;    /* Number of valid entries (<= capacity) */
    uint16_t       low_mgdl;
    uint16_t       high_mgdl;
    uint32_t       last_ts;  /* Most recent reading timestamp */
    bool           initialized;
} s_stats;

static const uint32_t WINDOW_SECONDS[STATS_WINDOW_COUNT] = {
    [STATS_WINDOW_24H] = 24u * 3600u,
    [STATS_WINDOW_7D]  = 7u  * 24u * 3600u,
};

static void evict_older_than(uint32_t cutoff_ts);

cgm_error_t stats_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.low_mgdl  = CONFIG_STATS_TIR_LOW_DEFAULT;
    s_stats.high_mgdl = CONFIG_STATS_TIR_HIGH_DEFAULT;
    s_stats.initialized = true;
    return CGM_OK;
}

cgm_error_t stats_set_range(uint16_t low_mgdl, uint16_t high_mgdl)
{
    if (low_mgdl < CONFIG_STATS_TIR_LOW_MIN ||
        high_mgdl > CONFIG_STATS_TIR_HIGH_MAX ||
        low_mgdl >= high_mgdl) {
        return CGM_ERR_CAL_REFERENCE_OOR;
    }
    s_stats.low_mgdl  = low_mgdl;
    s_stats.high_mgdl = high_mgdl;
    return CGM_OK;
}

void stats_clear(void)
{
    s_stats.head    = 0;
    s_stats.count   = 0;
    s_stats.last_ts = 0;
}

cgm_error_t stats_record_reading(const glucose_reading_t *reading)
{
    if (!s_stats.initialized || reading == NULL) {
        return CGM_ERR_SIGNAL_INSUFFICIENT;
    }
    if (reading->glucose_mgdl == GLUCOSE_INVALID ||
        reading->glucose_mgdl < GLUCOSE_MIN_MGDL ||
        reading->glucose_mgdl > GLUCOSE_MAX_MGDL) {
        return CGM_ERR_SIGNAL_INSUFFICIENT;
    }

    /* Reject non-monotonic timestamps. The signal pipeline drives this
     * module from a wall-clock-anchored reading; a timestamp regression
     * implies a clock reset, which would corrupt the rolling windows. */
    if (reading->timestamp + 1u < s_stats.last_ts) {
        return CGM_ERR_SIGNAL_INSUFFICIENT;
    }
    s_stats.last_ts = reading->timestamp;

    /* Evict everything that has rolled out of the largest window. */
    uint32_t cutoff = (reading->timestamp > WINDOW_SECONDS[STATS_WINDOW_7D])
                    ?  reading->timestamp - WINDOW_SECONDS[STATS_WINDOW_7D]
                    :  0u;
    evict_older_than(cutoff);

    /* Append. Ring buffer overwrites only when an unrealistic burst of
     * samples arrives faster than the window — the eviction step above
     * normally keeps `count` well below capacity. */
    s_stats.buf[s_stats.head].timestamp = reading->timestamp;
    s_stats.buf[s_stats.head].glucose   = reading->glucose_mgdl;
    s_stats.head = (s_stats.head + 1u) % STATS_BUF_CAPACITY;
    if (s_stats.count < STATS_BUF_CAPACITY) {
        s_stats.count++;
    }
    return CGM_OK;
}

/**
 * @brief Walk the active samples and compute the metrics for `window`.
 *
 * The buffer is logically a ring; samples are stored in append order so
 * the oldest live entry is at `(head - count) mod capacity`. We iterate
 * over only the entries that fall inside `window` — a single pass is
 * enough to compute mean, in/below/above counts, and the running sum of
 * squared deviations needed for CV.
 */
cgm_error_t stats_get(stats_window_t window, uint32_t now_unix_s,
                      glucose_stats_t *out)
{
    if (out == NULL || window >= STATS_WINDOW_COUNT) {
        return CGM_ERR_SIGNAL_INSUFFICIENT;
    }
    memset(out, 0, sizeof(*out));

    if (!s_stats.initialized || s_stats.count == 0) {
        return CGM_ERR_SIGNAL_INSUFFICIENT;
    }

    uint32_t cutoff = (now_unix_s > WINDOW_SECONDS[window])
                    ?  now_unix_s - WINDOW_SECONDS[window]
                    :  0u;

    uint32_t sum    = 0;
    uint64_t sum_sq = 0;
    uint16_t in_count = 0, below_count = 0, above_count = 0, n = 0;

    /* Walk only the live entries. */
    uint16_t idx = (uint16_t)((s_stats.head + STATS_BUF_CAPACITY -
                               s_stats.count) % STATS_BUF_CAPACITY);
    for (uint16_t i = 0; i < s_stats.count; i++) {
        const stats_sample_t *s = &s_stats.buf[idx];
        idx = (uint16_t)((idx + 1u) % STATS_BUF_CAPACITY);

        if (s->timestamp < cutoff) {
            continue; /* Outside this window */
        }
        n++;
        sum    += s->glucose;
        sum_sq += (uint64_t)s->glucose * (uint64_t)s->glucose;

        if (s->glucose < s_stats.low_mgdl) {
            below_count++;
        } else if (s->glucose > s_stats.high_mgdl) {
            above_count++;
        } else {
            in_count++;
        }
    }

    out->sample_count = n;
    if (n < CONFIG_STATS_MIN_SAMPLES) {
        return CGM_ERR_SIGNAL_INSUFFICIENT;
    }

    /* Mean (rounded). */
    uint16_t mean = (uint16_t)((sum + n / 2u) / n);
    out->mean_mgdl = mean;

    /* Percentages in tenths of a percent — multiply numerator by 1000
     * before division so we keep one decimal of precision without
     * touching floats. */
    out->tir_pct_x10 = (uint16_t)(((uint32_t)in_count    * 1000u + n / 2u) / n);
    out->tbr_pct_x10 = (uint16_t)(((uint32_t)below_count * 1000u + n / 2u) / n);
    out->tar_pct_x10 = (uint16_t)(((uint32_t)above_count * 1000u + n / 2u) / n);

    /* GMI = 3.31 + 0.02392 * mean_mgdl  (Bergenstal et al., 2018).
     * Stored as % * 10. Use a small float here — the runtime cost is
     * negligible compared with a 5-minute measurement cadence. */
    float gmi = 3.31f + 0.02392f * (float)mean;
    out->gmi_pct_x10 = (uint16_t)(gmi * 10.0f + 0.5f);

    /* Population variance via E[X^2] - (E[X])^2. The buffered glucose
     * range (40..400 mg/dL) and worst-case n (~2016 samples) keep
     * sum_sq below 2^28 — fits comfortably in uint64_t. */
    if (n > 1u && mean > 0u) {
        uint64_t mean_sq = (uint64_t)mean * (uint64_t)mean;
        uint64_t mean_of_sq = sum_sq / n;
        uint64_t variance = (mean_of_sq > mean_sq) ? mean_of_sq - mean_sq : 0u;
        float    stddev   = sqrtf((float)variance);
        float    cv       = (stddev / (float)mean) * 1000.0f; /* % * 10 */
        out->cv_pct_x10   = (uint16_t)(cv + 0.5f);
    }

    return CGM_OK;
}

/* --- internals --- */

static void evict_older_than(uint32_t cutoff_ts)
{
    while (s_stats.count > 0u) {
        uint16_t tail = (uint16_t)((s_stats.head + STATS_BUF_CAPACITY -
                                    s_stats.count) % STATS_BUF_CAPACITY);
        if (s_stats.buf[tail].timestamp >= cutoff_ts) {
            break;
        }
        s_stats.count--;
    }
}
