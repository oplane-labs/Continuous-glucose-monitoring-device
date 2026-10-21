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
#include "../sensor/calibration.h"
#include "../sensor/glucose_sensor.h"
#include "config/device_config.h"

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

/* CGM Specific Ops Control Point (0x2AAC) opcodes (SWR-045) */
#define SOCP_OPCODE_SET_CALIBRATION  0x04
#define SOCP_OPCODE_RESPONSE         0x1C

/* Module state */
static struct {
    bool     initialized;
    bool     notifications_enabled;
    uint16_t sequence_number;
} s_cgm_svc;

cgm_error_t cgm_service_init(void)
{
    s_cgm_svc.initialized = true;
    s_cgm_svc.notifications_enabled = false;
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

/**
 * @brief Handle a remote in-vivo calibration request (SWR-045)
 *
 * The companion mobile app writes a fingerstick blood-glucose reference to the
 * CGM Specific Ops Control Point (0x2AAC) so the user can recalibrate the sensor
 * from the phone instead of the manufacturing UART tool. The reference is applied
 * against the live sensor current via the calibration module.
 *
 * Operand layout (SET_CALIBRATION):
 *   [0]    Op code (SOCP_OPCODE_SET_CALIBRATION)
 *   [1-2]  Fingerstick reference glucose, mg/dL (uint16, little-endian)
 */
cgm_error_t cgm_service_handle_calibration(const uint8_t *operand, uint16_t len)
{
    if (!s_cgm_svc.initialized) {
        return CGM_ERR_BLE_INIT_FAIL;
    }

    /* Need at least the op code plus the 2-byte reference value */
    if (len < 3) {
        return CGM_ERR_BLE_NOTIFY_FAIL;
    }

    if (operand[0] != SOCP_OPCODE_SET_CALIBRATION) {
        return CGM_ERR_BLE_NOTIFY_FAIL;
    }

    uint16_t reference_mgdl = (uint16_t)(operand[1] | (operand[2] << 8));

    /* Pair the reference with the current live sensor reading and hand it to
     * the calibration module, which performs the weighted-least-squares update. */
    float current_na;
    cgm_error_t err = sensor_read_current(&current_na);
    if (err != CGM_OK) {
        return err;
    }

    return calibration_update(reference_mgdl, current_na);
}

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
