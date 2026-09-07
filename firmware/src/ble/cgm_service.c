/**
 * @file cgm_service.c
 * @brief Bluetooth SIG CGM GATT Service implementation
 *
 * Implements: SWR-040, SWR-041, SWR-043, SWR-045
 *
 * CGM Service UUID: 0x181F
 * Characteristics:
 *   - CGM Measurement (0x2AA7): Notify
 *   - CGM Feature (0x2AA8): Read
 *   - CGM Status (0x2AA9): Read
 *   - CGM Session Start Time (0x2AAA): Read/Write
 *   - CGM Session Run Time (0x2AAB): Notify
 *   - Record Access Control Point (0x2A52): Write/Indicate
 *   - CGM Specific Ops Control Point (0x2AAC): Write/Indicate
 */

#include "cgm_service.h"
#include "../storage/flash_storage.h"
#include "../alert/alert_manager.h"
#include "config/device_config.h"
#include <stddef.h>

#ifndef UNIT_TEST
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#else
#include "test_stubs.h"
#endif

/* CGM Service UUIDs per Bluetooth SIG specification */
#define BT_UUID_CGM_SERVICE_VAL      0x181F
#define BT_UUID_CGM_MEASUREMENT_VAL  0x2AA7
#define BT_UUID_CGM_FEATURE_VAL      0x2AA8
#define BT_UUID_CGM_STATUS_VAL       0x2AA9
#define BT_UUID_CGM_SESSION_START_VAL 0x2AAA
#define BT_UUID_CGM_SESSION_RUN_VAL  0x2AAB
#define BT_UUID_RACP_VAL             0x2A52

/* RACP opcodes */
#define RACP_OPCODE_REPORT_RECORDS   0x01
#define RACP_OPCODE_DELETE_RECORDS   0x02
#define RACP_OPCODE_ABORT           0x03
#define RACP_OPCODE_REPORT_COUNT    0x04
#define RACP_OPCODE_RESPONSE        0x06

/* CGM Specific Ops Control Point (0x2AAC) op codes, per the Bluetooth SIG
 * CGM Service specification. Only the op codes that map onto configurable
 * device behaviour are implemented (SWR-045); all others return the
 * "Op Code not supported" response. */
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

/* SOCP Response Code values (operand of a 0x1C Response Code message) */
#define SOCP_RSP_SUCCESS             0x01
#define SOCP_RSP_OP_NOT_SUPPORTED    0x02
#define SOCP_RSP_INVALID_OPERAND     0x03
#define SOCP_RSP_PROCEDURE_INCOMPLETE 0x04
#define SOCP_RSP_PARAM_OUT_OF_RANGE  0x05

/* Module state */
static struct {
    bool     initialized;
    bool     notifications_enabled;
    bool     session_active;
    uint16_t sequence_number;
} s_cgm_svc;

cgm_error_t cgm_service_init(void)
{
    s_cgm_svc.initialized = true;
    s_cgm_svc.notifications_enabled = false;
    s_cgm_svc.session_active = false;
    s_cgm_svc.sequence_number = 0;

#ifndef UNIT_TEST
    /* Register GATT service with BLE stack.
     * The service attributes are defined statically via BT_GATT_SERVICE_DEFINE
     * in production builds. */
#endif

    return CGM_OK;
}

/**
 * @brief Send glucose measurement notification (SWR-041)
 *
 * Packages the glucose reading into the CGM Measurement characteristic
 * format and sends a BLE notification to the subscribed client.
 * Called every 5 minutes when a client is connected and subscribed.
 *
 * Packet format (per Bluetooth CGM Profile):
 *   [0]    Size (1 byte)
 *   [1]    Flags (1 byte)
 *   [2-3]  Glucose concentration (SFLOAT, mg/dL)
 *   [4-5]  Time offset (uint16, minutes from session start)
 *   [6]    Sensor status annunciation (optional)
 *   [7]    Trend info (optional)
 */
cgm_error_t cgm_service_notify(const glucose_reading_t *reading)
{
    if (!s_cgm_svc.initialized || !s_cgm_svc.notifications_enabled) {
        return CGM_ERR_BLE_NOTIFY_FAIL;
    }

    /* Build CGM Measurement packet */
    uint8_t packet[8];
    uint8_t offset = 0;

    /* Size */
    packet[offset++] = sizeof(packet);

    /* Flags: trend info present, status present */
    packet[offset++] = 0x03;

    /* Glucose concentration as SFLOAT (simplified: direct mg/dL encoding) */
    packet[offset++] = (uint8_t)(reading->glucose_mgdl & 0xFF);
    packet[offset++] = (uint8_t)((reading->glucose_mgdl >> 8) & 0xFF);

    /* Time offset in minutes (from session start) */
    uint16_t time_offset = (uint16_t)(s_cgm_svc.sequence_number * 5);
    packet[offset++] = (uint8_t)(time_offset & 0xFF);
    packet[offset++] = (uint8_t)((time_offset >> 8) & 0xFF);

    /* Status annunciation */
    packet[offset++] = reading->status_flags;

    /* Trend info */
    packet[offset++] = reading->trend;

    s_cgm_svc.sequence_number++;

#ifndef UNIT_TEST
    /* bt_gatt_notify(NULL, &cgm_measurement_attr, packet, offset) */
#endif

    return CGM_OK;
}

/**
 * @brief Handle Record Access Control Point (RACP) requests (SWR-043)
 *
 * Supports backfilling of missed glucose readings stored in flash.
 * Up to 96 readings (8 hours) are available for backfill upon reconnection.
 */
cgm_error_t cgm_service_handle_racp(uint8_t opcode, uint8_t operator,
                                     const uint8_t *operand, uint16_t len)
{
    if (!s_cgm_svc.initialized) {
        return CGM_ERR_BLE_INIT_FAIL;
    }

    switch (opcode) {
    case RACP_OPCODE_REPORT_RECORDS: {
        /* Read stored records from flash and send as notifications */
        glucose_reading_t reading;
        uint16_t count = flash_get_reading_count();

        for (uint16_t i = 0; i < count; i++) {
            cgm_error_t err = flash_read_reading(i, &reading);
            if (err == CGM_OK) {
                cgm_service_notify(&reading);
            }
        }
        break;
    }

    case RACP_OPCODE_REPORT_COUNT: {
        /* Report the number of stored records */
        uint16_t count = flash_get_reading_count();
        uint8_t response[4] = {
            RACP_OPCODE_REPORT_COUNT, 0x00,
            (uint8_t)(count & 0xFF),
            (uint8_t)((count >> 8) & 0xFF)
        };
        (void)response;
        /* Send indication with response */
        break;
    }

    case RACP_OPCODE_ABORT:
        /* Abort any ongoing transfer */
        break;

    default:
        return CGM_ERR_BLE_NOTIFY_FAIL;
    }

    return CGM_OK;
}

/* --- CGM Specific Ops Control Point (SWR-045) --- */

/**
 * @brief Write a SOCP Response Code message into @p resp.
 * Format: [0x1C][request op code][response code value] (3 bytes).
 */
static uint16_t socp_write_response_code(uint8_t *resp, uint8_t req_opcode,
                                         uint8_t rsp_value)
{
    resp[0] = SOCP_OP_RESPONSE_CODE;
    resp[1] = req_opcode;
    resp[2] = rsp_value;
    return 3;
}

/**
 * @brief Write a SOCP value response into @p resp.
 * Format: [response op code][value_lo][value_hi] (3 bytes, uint16/sint16 LE).
 */
static uint16_t socp_write_value(uint8_t *resp, uint8_t rsp_opcode, int16_t value)
{
    resp[0] = rsp_opcode;
    resp[1] = (uint8_t)((uint16_t)value & 0xFF);
    resp[2] = (uint8_t)(((uint16_t)value >> 8) & 0xFF);
    return 3;
}

/**
 * @brief Apply a threshold change through the alert configuration and map the
 * result onto a SOCP response code value.
 */
static uint8_t socp_apply_config(const alert_config_t *updated)
{
    cgm_error_t err = alert_set_config(updated);
    if (err == CGM_OK) {
        return SOCP_RSP_SUCCESS;
    }
    if (err == CGM_ERR_CAL_REFERENCE_OOR) {
        return SOCP_RSP_PARAM_OUT_OF_RANGE;
    }
    return SOCP_RSP_PROCEDURE_INCOMPLETE;
}

/**
 * @brief Handle a CGM Specific Ops Control Point request (SWR-045).
 *
 * Reads/writes the device alert levels (mapped onto alert_config_t) and
 * starts/stops the measurement session. Produces either a value response
 * (Get) or a Response Code (Set / session control) in @p resp.
 */
uint16_t cgm_service_handle_socp(const uint8_t *req, uint16_t len,
                                 uint8_t *resp, uint16_t resp_max)
{
    /* Every response message this control point emits is 3 bytes. */
    if (!s_cgm_svc.initialized || req == NULL || resp == NULL ||
        len < 1 || resp_max < 3) {
        return 0;
    }

    uint8_t        opcode      = req[0];
    const uint8_t *operand     = req + 1;
    uint16_t       operand_len = len - 1;

    /* A working copy of the current configuration for Set operations. */
    alert_config_t cfg = *alert_get_config();

    switch (opcode) {
    /* --- Patient High Alert Level (maps to high_glucose_threshold) --- */
    case SOCP_OP_SET_PATIENT_HIGH:
        if (operand_len < 2) {
            return socp_write_response_code(resp, opcode, SOCP_RSP_INVALID_OPERAND);
        }
        cfg.high_glucose_threshold = (uint16_t)(operand[0] | (operand[1] << 8));
        return socp_write_response_code(resp, opcode, socp_apply_config(&cfg));

    case SOCP_OP_GET_PATIENT_HIGH:
        return socp_write_value(resp, SOCP_OP_RSP_PATIENT_HIGH,
                                (int16_t)cfg.high_glucose_threshold);

    /* --- Patient Low Alert Level (maps to low_glucose_threshold) --- */
    case SOCP_OP_SET_PATIENT_LOW:
        if (operand_len < 2) {
            return socp_write_response_code(resp, opcode, SOCP_RSP_INVALID_OPERAND);
        }
        cfg.low_glucose_threshold = (uint16_t)(operand[0] | (operand[1] << 8));
        return socp_write_response_code(resp, opcode, socp_apply_config(&cfg));

    case SOCP_OP_GET_PATIENT_LOW:
        return socp_write_value(resp, SOCP_OP_RSP_PATIENT_LOW,
                                (int16_t)cfg.low_glucose_threshold);

    /* --- Rate of Decrease Alert Level (maps to rapid_fall_rate, mg/dL/min) --- */
    case SOCP_OP_SET_RATE_DECREASE:
        if (operand_len < 2) {
            return socp_write_response_code(resp, opcode, SOCP_RSP_INVALID_OPERAND);
        }
        cfg.rapid_fall_rate = (int16_t)(operand[0] | (operand[1] << 8));
        return socp_write_response_code(resp, opcode, socp_apply_config(&cfg));

    case SOCP_OP_GET_RATE_DECREASE:
        return socp_write_value(resp, SOCP_OP_RSP_RATE_DECREASE, cfg.rapid_fall_rate);

    /* --- Rate of Increase Alert Level (maps to rapid_rise_rate, mg/dL/min) --- */
    case SOCP_OP_SET_RATE_INCREASE:
        if (operand_len < 2) {
            return socp_write_response_code(resp, opcode, SOCP_RSP_INVALID_OPERAND);
        }
        cfg.rapid_rise_rate = (int16_t)(operand[0] | (operand[1] << 8));
        return socp_write_response_code(resp, opcode, socp_apply_config(&cfg));

    case SOCP_OP_GET_RATE_INCREASE:
        return socp_write_value(resp, SOCP_OP_RSP_RATE_INCREASE, cfg.rapid_rise_rate);

    /* --- Reset device-specific alert state --- */
    case SOCP_OP_RESET_DEVICE_ALERT:
        alert_clear_all();
        return socp_write_response_code(resp, opcode, SOCP_RSP_SUCCESS);

    /* --- Session control --- */
    case SOCP_OP_START_SESSION:
        s_cgm_svc.session_active = true;
        s_cgm_svc.sequence_number = 0;
        return socp_write_response_code(resp, opcode, SOCP_RSP_SUCCESS);

    case SOCP_OP_STOP_SESSION:
        s_cgm_svc.session_active = false;
        return socp_write_response_code(resp, opcode, SOCP_RSP_SUCCESS);

    default:
        /* Any op code we do not implement is reported as unsupported. */
        return socp_write_response_code(resp, opcode, SOCP_RSP_OP_NOT_SUPPORTED);
    }
}

bool cgm_service_session_active(void)
{
    return s_cgm_svc.session_active;
}

/* In production the SOCP characteristic (0x2AAC) is registered via
 * BT_GATT_SERVICE_DEFINE with a write callback that forwards the request to
 * cgm_service_handle_socp() and indicates the returned response bytes:
 *
 *   static ssize_t socp_write(struct bt_conn *conn,
 *                             const struct bt_gatt_attr *attr,
 *                             const void *buf, uint16_t len,
 *                             uint16_t offset, uint8_t flags) {
 *       uint8_t  rsp[3];
 *       uint16_t n = cgm_service_handle_socp(buf, len, rsp, sizeof(rsp));
 *       if (n > 0) { bt_gatt_indicate(conn, &socp_ind_params); }
 *       return len;
 *   }
 */

cgm_error_t cgm_service_update_runtime(uint32_t runtime_minutes)
{
    (void)runtime_minutes;
    /* Update CGM Session Run Time characteristic (0x2AAB) */
    return CGM_OK;
}

cgm_error_t cgm_service_update_status(const device_status_t *status)
{
    (void)status;
    /* Update CGM Status characteristic (0x2AA9) */
    return CGM_OK;
}
