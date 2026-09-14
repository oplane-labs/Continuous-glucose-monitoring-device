/**
 * @file test_cgm_service.c
 * @brief Unit tests for the CGM GATT service control points
 *
 * Verifies: SWR-045 (CGM Specific Ops Control Point, 0x2AAC)
 */

#include "unity.h"
#include "ble/cgm_service.h"
#include "alert/alert_manager.h"
#include "storage/flash_storage.h"
#include "config/device_config.h"
#include "test_stubs.h"

/* SOCP op codes (mirrors cgm_service.c, per Bluetooth SIG CGM Service spec) */
#define SOCP_OP_SET_PATIENT_HIGH     0x07
#define SOCP_OP_GET_PATIENT_HIGH     0x08
#define SOCP_OP_RSP_PATIENT_HIGH     0x09
#define SOCP_OP_SET_PATIENT_LOW      0x0A
#define SOCP_OP_GET_PATIENT_LOW      0x0B
#define SOCP_OP_RSP_PATIENT_LOW      0x0C
#define SOCP_OP_SET_RATE_DECREASE    0x13
#define SOCP_OP_GET_RATE_DECREASE    0x14
#define SOCP_OP_RSP_RATE_DECREASE    0x15
#define SOCP_OP_SET_RATE_INCREASE    0x16
#define SOCP_OP_GET_RATE_INCREASE    0x17
#define SOCP_OP_RSP_RATE_INCREASE    0x18
#define SOCP_OP_RESET_DEVICE_ALERT   0x19
#define SOCP_OP_START_SESSION        0x1A
#define SOCP_OP_STOP_SESSION         0x1B
#define SOCP_OP_RESPONSE_CODE        0x1C

#define SOCP_RSP_SUCCESS             0x01
#define SOCP_RSP_OP_NOT_SUPPORTED    0x02
#define SOCP_RSP_INVALID_OPERAND     0x03
#define SOCP_RSP_PARAM_OUT_OF_RANGE  0x05

void setUp(void)
{
    test_stubs_reset();
    flash_init();
    alert_init();
    cgm_service_init();
}

void tearDown(void) {}

/* Helper: issue a Set request carrying a 16-bit little-endian value. */
static void socp_set_value(uint8_t opcode, int16_t value, uint8_t *resp,
                           uint16_t *rsp_len)
{
    uint8_t req[3] = {
        opcode,
        (uint8_t)((uint16_t)value & 0xFF),
        (uint8_t)(((uint16_t)value >> 8) & 0xFF),
    };
    *rsp_len = cgm_service_handle_socp(req, sizeof(req), resp, 8);
}

/* Helper: decode a 16-bit little-endian value from a response payload. */
static int16_t socp_decode_value(const uint8_t *resp)
{
    return (int16_t)(resp[1] | (resp[2] << 8));
}

/**
 * @test SWR-VER-045: Get patient high alert level returns the configured default
 */
void test_socp_get_patient_high_default(void)
{
    uint8_t req[1] = { SOCP_OP_GET_PATIENT_HIGH };
    uint8_t resp[8];

    uint16_t n = cgm_service_handle_socp(req, sizeof(req), resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_OP_RSP_PATIENT_HIGH, resp[0]);
    TEST_ASSERT_EQUAL_INT16(CONFIG_HIGH_GLUCOSE_DEFAULT, socp_decode_value(resp));
}

/**
 * @test SWR-VER-045: Setting a valid patient high level updates the alert config
 */
void test_socp_set_patient_high_valid(void)
{
    uint8_t  resp[8];
    uint16_t n;

    socp_set_value(SOCP_OP_SET_PATIENT_HIGH, 300, resp, &n);

    /* Response Code: success */
    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_OP_RESPONSE_CODE, resp[0]);
    TEST_ASSERT_EQUAL_UINT8(SOCP_OP_SET_PATIENT_HIGH, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);

    /* The alert configuration reflects the new threshold */
    TEST_ASSERT_EQUAL_UINT16(300, alert_get_config()->high_glucose_threshold);

    /* A subsequent Get returns the updated value */
    uint8_t req[1] = { SOCP_OP_GET_PATIENT_HIGH };
    cgm_service_handle_socp(req, sizeof(req), resp, sizeof(resp));
    TEST_ASSERT_EQUAL_INT16(300, socp_decode_value(resp));
}

/**
 * @test SWR-VER-045: Out-of-range high level is rejected and config is unchanged
 */
void test_socp_set_patient_high_out_of_range(void)
{
    uint8_t  resp[8];
    uint16_t n;

    /* CONFIG_HIGH_GLUCOSE_MAX is 400 mg/dL */
    socp_set_value(SOCP_OP_SET_PATIENT_HIGH, 500, resp, &n);

    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_OP_RESPONSE_CODE, resp[0]);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_PARAM_OUT_OF_RANGE, resp[2]);
    TEST_ASSERT_EQUAL_UINT16(CONFIG_HIGH_GLUCOSE_DEFAULT,
                             alert_get_config()->high_glucose_threshold);
}

/**
 * @test SWR-VER-045: Set/Get patient low alert level round-trips
 */
void test_socp_set_get_patient_low(void)
{
    uint8_t  resp[8];
    uint16_t n;

    socp_set_value(SOCP_OP_SET_PATIENT_LOW, 70, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);
    TEST_ASSERT_EQUAL_UINT16(70, alert_get_config()->low_glucose_threshold);

    uint8_t req[1] = { SOCP_OP_GET_PATIENT_LOW };
    n = cgm_service_handle_socp(req, sizeof(req), resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_OP_RSP_PATIENT_LOW, resp[0]);
    TEST_ASSERT_EQUAL_INT16(70, socp_decode_value(resp));
}

/**
 * @test SWR-VER-045: Out-of-range low level (below CONFIG_LOW_GLUCOSE_MIN) rejected
 */
void test_socp_set_patient_low_out_of_range(void)
{
    uint8_t  resp[8];
    uint16_t n;

    /* CONFIG_LOW_GLUCOSE_MIN is 50 mg/dL */
    socp_set_value(SOCP_OP_SET_PATIENT_LOW, 40, resp, &n);

    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_PARAM_OUT_OF_RANGE, resp[2]);
    TEST_ASSERT_EQUAL_UINT16(CONFIG_LOW_GLUCOSE_DEFAULT,
                             alert_get_config()->low_glucose_threshold);
}

/**
 * @test SWR-VER-045: Rate-of-decrease/increase levels round-trip as signed values
 */
void test_socp_rate_levels_signed(void)
{
    uint8_t  resp[8];
    uint16_t n;

    /* Rate of decrease is negative (mg/dL/min) */
    socp_set_value(SOCP_OP_SET_RATE_DECREASE, -3, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);

    uint8_t req_dec[1] = { SOCP_OP_GET_RATE_DECREASE };
    cgm_service_handle_socp(req_dec, sizeof(req_dec), resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT8(SOCP_OP_RSP_RATE_DECREASE, resp[0]);
    TEST_ASSERT_EQUAL_INT16(-3, socp_decode_value(resp));

    socp_set_value(SOCP_OP_SET_RATE_INCREASE, 4, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);

    uint8_t req_inc[1] = { SOCP_OP_GET_RATE_INCREASE };
    cgm_service_handle_socp(req_inc, sizeof(req_inc), resp, sizeof(resp));
    TEST_ASSERT_EQUAL_INT16(4, socp_decode_value(resp));
}

/**
 * @test SWR-VER-045: Out-of-range rate-of-decrease is rejected, config unchanged
 *
 * A rate-of-decrease threshold of 0 (or positive) would make the rapid-fall
 * alert fire on normal drift; an extreme negative value would disable it.
 * Both must be rejected with PARAM_OUT_OF_RANGE and leave the default intact.
 */
void test_socp_set_rate_decrease_out_of_range(void)
{
    uint8_t  resp[8];
    uint16_t n;

    /* Zero / wrong-sign: outside [CONFIG_RAPID_FALL_RATE_MIN, MAX] = [-10,-1] */
    socp_set_value(SOCP_OP_SET_RATE_DECREASE, 0, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_OP_RESPONSE_CODE, resp[0]);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_PARAM_OUT_OF_RANGE, resp[2]);
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_FALL_RATE,
                            alert_get_config()->rapid_fall_rate);

    /* Extreme magnitude (int16 min) would disable the alert entirely */
    socp_set_value(SOCP_OP_SET_RATE_DECREASE, -32768, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_PARAM_OUT_OF_RANGE, resp[2]);
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_FALL_RATE,
                            alert_get_config()->rapid_fall_rate);
}

/**
 * @test SWR-VER-045: Out-of-range rate-of-increase is rejected, config unchanged
 */
void test_socp_set_rate_increase_out_of_range(void)
{
    uint8_t  resp[8];
    uint16_t n;

    /* Extreme magnitude (int16 max) would make the rapid-rise alert unreachable */
    socp_set_value(SOCP_OP_SET_RATE_INCREASE, 32767, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_OP_RESPONSE_CODE, resp[0]);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_PARAM_OUT_OF_RANGE, resp[2]);
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_RISE_RATE,
                            alert_get_config()->rapid_rise_rate);

    /* Zero: below CONFIG_RAPID_RISE_RATE_MIN (1) */
    socp_set_value(SOCP_OP_SET_RATE_INCREASE, 0, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_PARAM_OUT_OF_RANGE, resp[2]);
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_RISE_RATE,
                            alert_get_config()->rapid_rise_rate);
}

/**
 * @test SWR-VER-045: Rate thresholds at the supported boundaries are accepted
 */
void test_socp_set_rate_boundary_values(void)
{
    uint8_t  resp[8];
    uint16_t n;

    socp_set_value(SOCP_OP_SET_RATE_DECREASE, CONFIG_RAPID_FALL_RATE_MIN, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_FALL_RATE_MIN,
                            alert_get_config()->rapid_fall_rate);

    socp_set_value(SOCP_OP_SET_RATE_DECREASE, CONFIG_RAPID_FALL_RATE_MAX, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_FALL_RATE_MAX,
                            alert_get_config()->rapid_fall_rate);

    socp_set_value(SOCP_OP_SET_RATE_INCREASE, CONFIG_RAPID_RISE_RATE_MIN, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_RISE_RATE_MIN,
                            alert_get_config()->rapid_rise_rate);

    socp_set_value(SOCP_OP_SET_RATE_INCREASE, CONFIG_RAPID_RISE_RATE_MAX, resp, &n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);
    TEST_ASSERT_EQUAL_INT16(CONFIG_RAPID_RISE_RATE_MAX,
                            alert_get_config()->rapid_rise_rate);
}

/**
 * @test SWR-VER-045: A Set with a missing operand yields "Invalid Operand"
 */
void test_socp_set_missing_operand(void)
{
    uint8_t req[1] = { SOCP_OP_SET_PATIENT_HIGH }; /* no value bytes */
    uint8_t resp[8];

    uint16_t n = cgm_service_handle_socp(req, sizeof(req), resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_OP_RESPONSE_CODE, resp[0]);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_INVALID_OPERAND, resp[2]);
}

/**
 * @test SWR-VER-045: Unsupported op codes are reported, not silently ignored
 */
void test_socp_unsupported_opcode(void)
{
    uint8_t req[1] = { 0x7F }; /* not implemented */
    uint8_t resp[8];

    uint16_t n = cgm_service_handle_socp(req, sizeof(req), resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_OP_RESPONSE_CODE, resp[0]);
    TEST_ASSERT_EQUAL_UINT8(0x7F, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_OP_NOT_SUPPORTED, resp[2]);
}

/**
 * @test SWR-VER-045: Start/Stop session op codes toggle session state
 */
void test_socp_session_control(void)
{
    uint8_t req_start[1] = { SOCP_OP_START_SESSION };
    uint8_t req_stop[1]  = { SOCP_OP_STOP_SESSION };
    uint8_t resp[8];

    TEST_ASSERT_FALSE(cgm_service_session_active());

    cgm_service_handle_socp(req_start, sizeof(req_start), resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);
    TEST_ASSERT_TRUE(cgm_service_session_active());

    cgm_service_handle_socp(req_stop, sizeof(req_stop), resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);
    TEST_ASSERT_FALSE(cgm_service_session_active());
}

/**
 * @test SWR-VER-045: Reset device-specific alert clears active alerts
 */
void test_socp_reset_device_alert(void)
{
    /* Raise a sensor fault alert, then reset it via the control point */
    alert_evaluate(100, 0.0f, FAULT_SHORT_CIRCUIT);
    TEST_ASSERT_TRUE(alert_is_active(ALERT_SENSOR_FAULT));

    uint8_t req[1] = { SOCP_OP_RESET_DEVICE_ALERT };
    uint8_t resp[8];
    uint16_t n = cgm_service_handle_socp(req, sizeof(req), resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_UINT8(SOCP_RSP_SUCCESS, resp[2]);
    TEST_ASSERT_FALSE(alert_is_active(ALERT_SENSOR_FAULT));
}

/**
 * @test SWR-VER-045: A malformed (empty) request produces no response
 */
void test_socp_empty_request(void)
{
    uint8_t resp[8];
    uint16_t n = cgm_service_handle_socp(resp, 0, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(0, n);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_socp_get_patient_high_default);
    RUN_TEST(test_socp_set_patient_high_valid);
    RUN_TEST(test_socp_set_patient_high_out_of_range);
    RUN_TEST(test_socp_set_get_patient_low);
    RUN_TEST(test_socp_set_patient_low_out_of_range);
    RUN_TEST(test_socp_rate_levels_signed);
    RUN_TEST(test_socp_set_rate_decrease_out_of_range);
    RUN_TEST(test_socp_set_rate_increase_out_of_range);
    RUN_TEST(test_socp_set_rate_boundary_values);
    RUN_TEST(test_socp_set_missing_operand);
    RUN_TEST(test_socp_unsupported_opcode);
    RUN_TEST(test_socp_session_control);
    RUN_TEST(test_socp_reset_device_alert);
    RUN_TEST(test_socp_empty_request);

    return UNITY_END();
}
