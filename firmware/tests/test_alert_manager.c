/**
 * @file test_alert_manager.c
 * @brief Unit tests for the alert management module
 *
 * Verifies: SWR-030, SWR-031, SWR-032, SWR-033, SWR-034
 * Risk Controls: RC-003, RC-004, RC-005, RC-006
 */

#include "unity.h"
#include "alert/alert_manager.h"
#include "config/device_config.h"
#include "test_stubs.h"

void setUp(void)
{
    test_stubs_reset();
    alert_init();
}

void tearDown(void) {}

/**
 * @test SWR-VER-030: Low glucose alert with consecutive confirmation (RC-003)
 * Low alert requires 2 consecutive readings below threshold.
 * Single reading below threshold should NOT trigger alert.
 */
void test_low_glucose_alert(void)
{
    /* First reading below threshold - alert should NOT be active yet */
    alert_evaluate(50, 0.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_LOW_GLUCOSE));

    /* Second consecutive reading below threshold - alert should activate */
    alert_evaluate(48, 0.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_LOW_GLUCOSE));
}

/**
 * @test SWR-VER-030: Low glucose alert clears when reading recovers
 */
void test_low_glucose_alert_clears(void)
{
    alert_evaluate(50, 0.0f, FAULT_NONE);
    alert_evaluate(48, 0.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_LOW_GLUCOSE));

    /* Reading returns to normal - alert should clear */
    alert_evaluate(120, 0.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_LOW_GLUCOSE));
}

/**
 * @test SWR-VER-031: High glucose alert with 3-reading confirmation (RC-004)
 * High alert requires 3 consecutive readings above threshold.
 */
void test_high_glucose_alert(void)
{
    /* Default high threshold: 250 mg/dL */
    alert_evaluate(260, 0.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_HIGH_GLUCOSE));

    alert_evaluate(270, 0.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_HIGH_GLUCOSE));

    /* Third consecutive reading above threshold -> alert activates */
    alert_evaluate(280, 0.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_HIGH_GLUCOSE));
}

/**
 * @test SWR-VER-031: Interrupted high glucose sequence resets counter
 */
void test_high_glucose_interrupted(void)
{
    alert_evaluate(260, 0.0f, FAULT_NONE);
    alert_evaluate(270, 0.0f, FAULT_NONE);
    /* Normal reading interrupts the sequence */
    alert_evaluate(200, 0.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_HIGH_GLUCOSE));

    /* Restarting count - need 3 more consecutive */
    alert_evaluate(260, 0.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_HIGH_GLUCOSE));
}

/**
 * @test SWR-VER-032: Rapid fall alert (RC-005)
 * Rate alert requires 3 consecutive rate calculations below -2 mg/dL/min.
 */
void test_rapid_fall_alert(void)
{
    float rapid_fall = -3.0f; /* Below threshold of -2 mg/dL/min */

    alert_evaluate(120, rapid_fall, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_RAPID_FALL));

    alert_evaluate(110, rapid_fall, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_RAPID_FALL));

    alert_evaluate(100, rapid_fall, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_RAPID_FALL));
}

/**
 * @test SWR-VER-033: Alert priority ordering
 * SENSOR_FAULT > LOW_GLUCOSE > RAPID_FALL > HIGH_GLUCOSE
 */
void test_alert_priority(void)
{
    /* Trigger multiple alerts simultaneously */
    /* Low glucose (2 consecutive) */
    alert_evaluate(50, -3.0f, FAULT_NONE);
    alert_evaluate(48, -3.0f, FAULT_NONE);

    alert_type_t highest;
    cgm_error_t err = alert_get_highest_priority(&highest);
    TEST_ASSERT_EQUAL(CGM_OK, err);
    /* LOW_GLUCOSE should be highest (RAPID_FALL needs 3 consecutive) */
    TEST_ASSERT_EQUAL(ALERT_LOW_GLUCOSE, highest);

    /* Now add a sensor fault - it should take priority */
    alert_evaluate(48, -3.0f, FAULT_OPEN_CIRCUIT);
    err = alert_get_highest_priority(&highest);
    TEST_ASSERT_EQUAL(CGM_OK, err);
    TEST_ASSERT_EQUAL(ALERT_SENSOR_FAULT, highest);
}

/**
 * @test SWR-VER-034: Snooze behavior for non-critical alerts
 */
void test_alert_snooze(void)
{
    /* Trigger high glucose alert */
    alert_evaluate(260, 0.0f, FAULT_NONE);
    alert_evaluate(270, 0.0f, FAULT_NONE);
    alert_evaluate(280, 0.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_HIGH_GLUCOSE));

    /* Snooze the alert */
    cgm_error_t err = alert_snooze(ALERT_HIGH_GLUCOSE);
    TEST_ASSERT_EQUAL(CGM_OK, err);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_HIGH_GLUCOSE));
}

/**
 * @test SWR-VER-034: Critical alerts cannot be snoozed
 * SENSOR_FAULT and LOW_GLUCOSE below 45 mg/dL are non-snoozable.
 */
void test_critical_alert_not_snoozable(void)
{
    /* Sensor fault cannot be snoozed */
    alert_evaluate(100, 0.0f, FAULT_SHORT_CIRCUIT);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_SENSOR_FAULT));

    cgm_error_t err = alert_snooze(ALERT_SENSOR_FAULT);
    TEST_ASSERT_NOT_EQUAL(CGM_OK, err);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_SENSOR_FAULT));
}

/**
 * @test SWR-VER-034: LOW_GLUCOSE below 45 mg/dL cannot be snoozed
 */
void test_critical_low_not_snoozable(void)
{
    /* Trigger low glucose below critical threshold (45 mg/dL) */
    alert_evaluate(42, 0.0f, FAULT_NONE);
    alert_evaluate(40, 0.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_LOW_GLUCOSE));

    /* Should NOT be snoozable because glucose < 45 */
    cgm_error_t err = alert_snooze(ALERT_LOW_GLUCOSE);
    TEST_ASSERT_NOT_EQUAL(CGM_OK, err);
}

/**
 * @test Signal loss alert (RC-006)
 * Activates when no valid reading received for 20 minutes.
 */
void test_signal_loss_alert(void)
{
    /* Less than 20 minutes - no alert */
    alert_check_signal_loss(1000000); /* 16.7 minutes */
    TEST_ASSERT_FALSE(alert_is_active(ALERT_SIGNAL_LOSS));

    /* Exactly 20 minutes -> alert */
    alert_check_signal_loss(1200000);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_SIGNAL_LOSS));
}

/**
 * @test No active alerts returns appropriate error
 */
void test_no_active_alerts(void)
{
    alert_type_t highest;
    cgm_error_t err = alert_get_highest_priority(&highest);
    TEST_ASSERT_NOT_EQUAL(CGM_OK, err);
}

/**
 * @test SWR-VER-034: alert_set_config rejects out-of-range rapid rate thresholds
 *
 * Guards the safety-critical rate alerts (RC-005) against remote SOCP
 * misconfiguration: an extreme magnitude disables the alert, a near-zero or
 * wrong-sign value makes it fire on normal glucose drift.
 */
void test_set_config_rejects_out_of_range_rates(void)
{
    alert_config_t cfg = *alert_get_config();

    /* Baseline: the defaults are valid and accepted */
    TEST_ASSERT_EQUAL(CGM_OK, alert_set_config(&cfg));

    /* rapid_fall_rate must stay strictly negative and bounded */
    cfg = *alert_get_config();
    cfg.rapid_fall_rate = 0;
    TEST_ASSERT_NOT_EQUAL(CGM_OK, alert_set_config(&cfg));
    cfg.rapid_fall_rate = -32768;
    TEST_ASSERT_NOT_EQUAL(CGM_OK, alert_set_config(&cfg));

    /* rapid_rise_rate must stay strictly positive and bounded */
    cfg = *alert_get_config();
    cfg.rapid_rise_rate = 0;
    TEST_ASSERT_NOT_EQUAL(CGM_OK, alert_set_config(&cfg));
    cfg.rapid_rise_rate = 32767;
    TEST_ASSERT_NOT_EQUAL(CGM_OK, alert_set_config(&cfg));

    /* A rejected write must not mutate the stored configuration */
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_FALL_RATE,
                            alert_get_config()->rapid_fall_rate);
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_RISE_RATE,
                            alert_get_config()->rapid_rise_rate);
}

/**
 * @test SWR-VER-034: alert_set_config accepts rate thresholds at the boundaries
 */
void test_set_config_accepts_rate_boundaries(void)
{
    alert_config_t cfg = *alert_get_config();

    cfg.rapid_fall_rate = CONFIG_RAPID_FALL_RATE_MIN;
    cfg.rapid_rise_rate = CONFIG_RAPID_RISE_RATE_MAX;
    TEST_ASSERT_EQUAL(CGM_OK, alert_set_config(&cfg));

    cfg.rapid_fall_rate = CONFIG_RAPID_FALL_RATE_MAX;
    cfg.rapid_rise_rate = CONFIG_RAPID_RISE_RATE_MIN;
    TEST_ASSERT_EQUAL(CGM_OK, alert_set_config(&cfg));
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_FALL_RATE_MAX,
                            alert_get_config()->rapid_fall_rate);
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_RISE_RATE_MIN,
                            alert_get_config()->rapid_rise_rate);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_low_glucose_alert);
    RUN_TEST(test_low_glucose_alert_clears);
    RUN_TEST(test_high_glucose_alert);
    RUN_TEST(test_high_glucose_interrupted);
    RUN_TEST(test_rapid_fall_alert);
    RUN_TEST(test_alert_priority);
    RUN_TEST(test_alert_snooze);
    RUN_TEST(test_critical_alert_not_snoozable);
    RUN_TEST(test_critical_low_not_snoozable);
    RUN_TEST(test_signal_loss_alert);
    RUN_TEST(test_no_active_alerts);
    RUN_TEST(test_set_config_rejects_out_of_range_rates);
    RUN_TEST(test_set_config_accepts_rate_boundaries);

    return UNITY_END();
}
