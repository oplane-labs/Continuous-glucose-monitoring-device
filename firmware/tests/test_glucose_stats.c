/**
 * @file test_glucose_stats.c
 * @brief Unit tests for the rolling glucose statistics module.
 *
 * Verifies: SWR-080, SWR-081, SWR-082
 */

#include "unity.h"
#include "stats/glucose_stats.h"
#include "config/device_config.h"
#include "test_stubs.h"

#include <string.h>

static glucose_reading_t make_reading(uint32_t ts, uint16_t mgdl)
{
    glucose_reading_t r;
    memset(&r, 0, sizeof(r));
    r.timestamp    = ts;
    r.glucose_mgdl = mgdl;
    return r;
}

void setUp(void)
{
    test_stubs_reset();
    stats_init();
}

void tearDown(void) {}

/**
 * @test SWR-VER-081: With fewer than CONFIG_STATS_MIN_SAMPLES readings the
 * module reports CGM_ERR_SIGNAL_INSUFFICIENT and zeroed metrics.
 */
void test_insufficient_samples_returns_error(void)
{
    glucose_stats_t s;
    /* No samples at all. */
    TEST_ASSERT_NOT_EQUAL(CGM_OK, stats_get(STATS_WINDOW_24H, 0, &s));

    /* A handful of samples is still below the 1-hour minimum. */
    for (int i = 0; i < 5; i++) {
        glucose_reading_t r = make_reading((uint32_t)(i * 300), 120);
        stats_record_reading(&r);
    }
    TEST_ASSERT_NOT_EQUAL(CGM_OK, stats_get(STATS_WINDOW_24H, 1500, &s));
    TEST_ASSERT_EQUAL_UINT16(5, s.sample_count);
    TEST_ASSERT_EQUAL_UINT16(0, s.mean_mgdl);
}

/**
 * @test SWR-VER-080: A flat reading stream produces a clean 100% TIR with
 * the mean equal to the constant value and CV near zero.
 */
void test_flat_in_range_yields_100_percent_tir(void)
{
    /* 24 samples spaced 5 min apart at 120 mg/dL — squarely in range. */
    for (int i = 0; i < 24; i++) {
        glucose_reading_t r = make_reading((uint32_t)(i * 300), 120);
        stats_record_reading(&r);
    }

    glucose_stats_t s;
    uint32_t now = 24u * 300u;
    TEST_ASSERT_EQUAL(CGM_OK, stats_get(STATS_WINDOW_24H, now, &s));
    TEST_ASSERT_EQUAL_UINT16(24, s.sample_count);
    TEST_ASSERT_EQUAL_UINT16(120, s.mean_mgdl);
    TEST_ASSERT_EQUAL_UINT16(1000, s.tir_pct_x10); /* 100.0% */
    TEST_ASSERT_EQUAL_UINT16(0,    s.tbr_pct_x10);
    TEST_ASSERT_EQUAL_UINT16(0,    s.tar_pct_x10);
    TEST_ASSERT_EQUAL_UINT16(0,    s.cv_pct_x10);  /* No variability */
}

/**
 * @test SWR-VER-080: Mixed readings split into in/below/above buckets in
 * the proportions actually fed to the module.
 */
void test_mixed_distribution(void)
{
    /* 20 in-range (120), 4 below (60), 6 above (220) = 30 total */
    uint32_t ts = 0;
    for (int i = 0; i < 20; i++) { glucose_reading_t r = make_reading(ts, 120); stats_record_reading(&r); ts += 300; }
    for (int i = 0; i < 4;  i++) { glucose_reading_t r = make_reading(ts, 60);  stats_record_reading(&r); ts += 300; }
    for (int i = 0; i < 6;  i++) { glucose_reading_t r = make_reading(ts, 220); stats_record_reading(&r); ts += 300; }

    glucose_stats_t s;
    TEST_ASSERT_EQUAL(CGM_OK, stats_get(STATS_WINDOW_24H, ts, &s));
    TEST_ASSERT_EQUAL_UINT16(30, s.sample_count);
    /* 20/30 = 66.666...% -> 667 in tenths-of-percent. */
    TEST_ASSERT_EQUAL_UINT16(667, s.tir_pct_x10);
    /*  4/30 = 13.333...% -> 133 */
    TEST_ASSERT_EQUAL_UINT16(133, s.tbr_pct_x10);
    /*  6/30 = 20.0%      -> 200 */
    TEST_ASSERT_EQUAL_UINT16(200, s.tar_pct_x10);
}

/**
 * @test SWR-VER-082: GMI tracks the published Bergenstal regression.
 * GMI(154 mg/dL) = 3.31 + 0.02392*154 = 6.99% -> reported as 70.
 */
void test_gmi_matches_bergenstal_formula(void)
{
    for (int i = 0; i < 24; i++) {
        glucose_reading_t r = make_reading((uint32_t)(i * 300), 154);
        stats_record_reading(&r);
    }
    glucose_stats_t s;
    TEST_ASSERT_EQUAL(CGM_OK, stats_get(STATS_WINDOW_24H, 24u * 300u, &s));
    /* Allow ±1 unit tolerance to absorb integer-mean rounding. */
    TEST_ASSERT_INT_WITHIN(1, 70, s.gmi_pct_x10);
}

/**
 * @test SWR-VER-080: Samples that have rolled outside the 24-hour window
 * are excluded from the 24h snapshot but still feed the 7-day snapshot.
 */
void test_rolling_24h_window_excludes_stale_samples(void)
{
    /* Day 0: 12 samples at 250 mg/dL (above range). */
    for (int i = 0; i < 12; i++) {
        glucose_reading_t r = make_reading((uint32_t)(i * 300), 250);
        stats_record_reading(&r);
    }
    /* Day 2 (well past the 24-hour window): 24 samples at 120 mg/dL. */
    uint32_t day2_start = 2u * 24u * 3600u;
    for (int i = 0; i < 24; i++) {
        glucose_reading_t r = make_reading(day2_start + (uint32_t)(i * 300), 120);
        stats_record_reading(&r);
    }

    glucose_stats_t s24, s7d;
    uint32_t now = day2_start + 24u * 300u;

    /* 24h window: only the day-2 in-range samples count. */
    TEST_ASSERT_EQUAL(CGM_OK, stats_get(STATS_WINDOW_24H, now, &s24));
    TEST_ASSERT_EQUAL_UINT16(24, s24.sample_count);
    TEST_ASSERT_EQUAL_UINT16(1000, s24.tir_pct_x10);

    /* 7-day window: all 36 samples participate, 24/36 = 66.7% TIR. */
    TEST_ASSERT_EQUAL(CGM_OK, stats_get(STATS_WINDOW_7D, now, &s7d));
    TEST_ASSERT_EQUAL_UINT16(36, s7d.sample_count);
    TEST_ASSERT_EQUAL_UINT16(667, s7d.tir_pct_x10);
}

/**
 * @test SWR-VER-080: Invalid readings (sentinel value or out-of-range)
 * are silently dropped without disturbing the windows.
 */
void test_invalid_readings_dropped(void)
{
    /* 24 valid samples first. */
    for (int i = 0; i < 24; i++) {
        glucose_reading_t r = make_reading((uint32_t)(i * 300), 120);
        stats_record_reading(&r);
    }
    /* Then a stream of garbage: invalid sentinel, below physiological min,
     * above physiological max. */
    glucose_reading_t bad = make_reading(24u * 300u, GLUCOSE_INVALID);
    stats_record_reading(&bad);
    bad = make_reading(24u * 300u + 300u, 10);   /* below 40 mg/dL min */
    stats_record_reading(&bad);
    bad = make_reading(24u * 300u + 600u, 500);  /* above 400 mg/dL max */
    stats_record_reading(&bad);

    glucose_stats_t s;
    TEST_ASSERT_EQUAL(CGM_OK, stats_get(STATS_WINDOW_24H, 24u * 300u, &s));
    TEST_ASSERT_EQUAL_UINT16(24, s.sample_count);
    TEST_ASSERT_EQUAL_UINT16(120, s.mean_mgdl);
}

/**
 * @test SWR-VER-080: A timestamp regression is rejected — clock resets
 * must not corrupt the rolling windows.
 */
void test_timestamp_regression_rejected(void)
{
    glucose_reading_t r = make_reading(10000, 120);
    TEST_ASSERT_EQUAL(CGM_OK, stats_record_reading(&r));

    glucose_reading_t back_in_time = make_reading(5000, 120);
    TEST_ASSERT_NOT_EQUAL(CGM_OK, stats_record_reading(&back_in_time));
}

/**
 * @test SWR-VER-081: stats_set_range rejects inverted/out-of-range bands
 * and accepts a valid configuration.
 */
void test_set_range_validation(void)
{
    /* Inverted: low >= high. */
    TEST_ASSERT_NOT_EQUAL(CGM_OK, stats_set_range(180, 70));
    /* Below physiological floor. */
    TEST_ASSERT_NOT_EQUAL(CGM_OK, stats_set_range(30, 180));
    /* Above physiological ceiling. */
    TEST_ASSERT_NOT_EQUAL(CGM_OK, stats_set_range(70, 400));
    /* Valid clinical band. */
    TEST_ASSERT_EQUAL(CGM_OK, stats_set_range(70, 180));
}

/**
 * @test SWR-VER-082: CV reflects actual variability — alternating between
 * 100 and 200 mg/dL gives mean 150 and ~33% CV (stddev=50, 50/150=33.3%).
 */
void test_cv_reflects_variability(void)
{
    for (int i = 0; i < 24; i++) {
        uint16_t g = (i & 1) ? 200 : 100;
        glucose_reading_t r = make_reading((uint32_t)(i * 300), g);
        stats_record_reading(&r);
    }
    glucose_stats_t s;
    TEST_ASSERT_EQUAL(CGM_OK, stats_get(STATS_WINDOW_24H, 24u * 300u, &s));
    TEST_ASSERT_EQUAL_UINT16(150, s.mean_mgdl);
    /* Population stddev is exactly 50, CV = 333. Allow ±2 for rounding. */
    TEST_ASSERT_INT_WITHIN(2, 333, s.cv_pct_x10);
}

/**
 * @test stats_clear discards all buffered samples (sensor replacement).
 */
void test_clear_drops_all_samples(void)
{
    for (int i = 0; i < 24; i++) {
        glucose_reading_t r = make_reading((uint32_t)(i * 300), 120);
        stats_record_reading(&r);
    }
    stats_clear();

    glucose_stats_t s;
    TEST_ASSERT_NOT_EQUAL(CGM_OK, stats_get(STATS_WINDOW_24H, 24u * 300u, &s));
    TEST_ASSERT_EQUAL_UINT16(0, s.sample_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_insufficient_samples_returns_error);
    RUN_TEST(test_flat_in_range_yields_100_percent_tir);
    RUN_TEST(test_mixed_distribution);
    RUN_TEST(test_gmi_matches_bergenstal_formula);
    RUN_TEST(test_rolling_24h_window_excludes_stale_samples);
    RUN_TEST(test_invalid_readings_dropped);
    RUN_TEST(test_timestamp_regression_rejected);
    RUN_TEST(test_set_range_validation);
    RUN_TEST(test_cv_reflects_variability);
    RUN_TEST(test_clear_drops_all_samples);
    return UNITY_END();
}
