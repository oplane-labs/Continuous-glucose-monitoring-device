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
#include <math.h>

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
 * @test SWR-VER-035: Predicted low alert fires when projection crosses
 * threshold while glucose is still in-range.
 *
 * 100 mg/dL falling at -2 mg/dL/min projects to 60 mg/dL in 20 min,
 * which is below the default 70 mg/dL predicted-low threshold.
 */
void test_predicted_low_alert_fires(void)
{
    alert_evaluate(100, -2.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_PREDICTED_LOW));
    /* Real low not active yet — predicted is the highest priority. */
    TEST_ASSERT_FALSE(alert_is_active(ALERT_LOW_GLUCOSE));
}

/**
 * @test SWR-VER-035: Stable glucose (no fall) must not fire predicted-low
 * even if the absolute level is moderate.
 */
void test_predicted_low_no_fire_when_stable(void)
{
    alert_evaluate(75, 0.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));
}

/**
 * @test SWR-VER-035: Rising glucose must not fire predicted-low.
 */
void test_predicted_low_no_fire_when_rising(void)
{
    alert_evaluate(80, +2.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));
}

/**
 * @test SWR-VER-035: Predicted-low is suppressed when LOW_GLUCOSE is
 * already active (no double-firing).
 */
void test_predicted_low_suppressed_by_low_glucose(void)
{
    /* Drive LOW_GLUCOSE active (2 consecutive < 55). */
    alert_evaluate(50, -2.0f, FAULT_NONE);
    alert_evaluate(48, -2.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_LOW_GLUCOSE));
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));
}

/**
 * @test SWR-VER-035: Hysteresis — alert clears only after projection
 * recovers above threshold + 10 mg/dL.
 */
void test_predicted_low_hysteresis(void)
{
    /* Fire it. */
    alert_evaluate(100, -2.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_PREDICTED_LOW));

    /* Projection = 110 + 0*20 = 110, well above 70+10=80 — clears. */
    alert_evaluate(110, 0.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));
}

/**
 * @test SWR-VER-035: Predicted-low ranks above HIGH_GLUCOSE / RAPID_RISE
 * but below LOW_GLUCOSE and SENSOR_FAULT in priority ordering.
 */
void test_predicted_low_priority(void)
{
    alert_evaluate(100, -2.0f, FAULT_NONE);
    alert_type_t highest;
    TEST_ASSERT_EQUAL(CGM_OK, alert_get_highest_priority(&highest));
    TEST_ASSERT_EQUAL(ALERT_PREDICTED_LOW, highest);
}

/**
 * @test SWR-VER-035: GLUCOSE_INVALID input must not trigger predicted-low.
 */
void test_predicted_low_rejects_invalid_glucose(void)
{
    alert_evaluate(GLUCOSE_INVALID, -2.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));
}

/**
 * @test SWR-VER-035: Glucose above physiological max must not enter the
 * prediction path (uint16_t can carry corrupt values up to 0xFFFE).
 */
void test_predicted_low_rejects_implausible_high_glucose(void)
{
    /* 0xFFFE is not GLUCOSE_INVALID but is wildly out of range. With a
     * fast fall a naive predictor would still compute a low projection. */
    alert_evaluate(0xFFFE, -10.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));
}

/**
 * @test SWR-VER-035: Implausibly large negative rate (-100 mg/dL/min) must
 * be rejected, not used to fabricate a low projection.
 */
void test_predicted_low_rejects_extreme_negative_rate(void)
{
    alert_evaluate(200, -100.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));
}

/**
 * @test SWR-VER-035: Implausibly large positive rate is rejected as
 * untrusted input (independent of the falling-rate gate).
 */
void test_predicted_low_rejects_extreme_positive_rate(void)
{
    alert_evaluate(120, 100.0f, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));
}

/**
 * @test SWR-VER-035: NaN / Inf rate must not trigger predicted-low even
 * if the comparison would otherwise sneak through.
 */
void test_predicted_low_rejects_nonfinite_rate(void)
{
    alert_evaluate(100, NAN, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));

    alert_evaluate(100, -INFINITY, FAULT_NONE);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));
}

/**
 * @test SWR-VER-035: Active sensor fault suppresses predicted-low — both
 * the upstream signal pipeline and this guard must agree the data is
 * trustworthy before a prediction-based alert can fire.
 */
void test_predicted_low_suppressed_by_sensor_fault(void)
{
    alert_evaluate(100, -2.0f, FAULT_SHORT_CIRCUIT);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_PREDICTED_LOW));
    TEST_ASSERT_TRUE(alert_is_active(ALERT_SENSOR_FAULT));
}

/**
 * @test SWR-VER-035: A single corrupt sample must not clear an existing
 * predicted-low alert — prior state is preserved when input fails
 * validation, so attackers/faults cannot silently suppress the warning.
 */
void test_predicted_low_corrupt_sample_does_not_clear(void)
{
    /* Drive predicted-low active. */
    alert_evaluate(100, -2.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_PREDICTED_LOW));

    /* Corrupt sample: invalid glucose. Must NOT clear the existing alert. */
    alert_evaluate(GLUCOSE_INVALID, -2.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_PREDICTED_LOW));
}

/**
 * @test SWR-VER-035: Boundary — glucose at GLUCOSE_MIN_MGDL with a modest
 * fall rate is accepted and should fire (boundaries are inclusive).
 */
void test_predicted_low_accepts_boundary_glucose(void)
{
    /* 40 + (-2)*20 = 0 < 70 — fires. */
    alert_evaluate(GLUCOSE_MIN_MGDL, -2.0f, FAULT_NONE);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_PREDICTED_LOW));
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
    RUN_TEST(test_predicted_low_alert_fires);
    RUN_TEST(test_predicted_low_no_fire_when_stable);
    RUN_TEST(test_predicted_low_no_fire_when_rising);
    RUN_TEST(test_predicted_low_suppressed_by_low_glucose);
    RUN_TEST(test_predicted_low_hysteresis);
    RUN_TEST(test_predicted_low_priority);
    RUN_TEST(test_predicted_low_rejects_invalid_glucose);
    RUN_TEST(test_predicted_low_rejects_implausible_high_glucose);
    RUN_TEST(test_predicted_low_rejects_extreme_negative_rate);
    RUN_TEST(test_predicted_low_rejects_extreme_positive_rate);
    RUN_TEST(test_predicted_low_rejects_nonfinite_rate);
    RUN_TEST(test_predicted_low_suppressed_by_sensor_fault);
    RUN_TEST(test_predicted_low_corrupt_sample_does_not_clear);
    RUN_TEST(test_predicted_low_accepts_boundary_glucose);
    RUN_TEST(test_no_active_alerts);

    return UNITY_END();
}
