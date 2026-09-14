/**
 * @file cgm_service.h
 * @brief Bluetooth SIG CGM GATT Service implementation
 *
 * Implements the CGM Profile (UUID 0x181F) with characteristics for
 * glucose measurement notifications and historical data backfill.
 *
 * Implements: SWR-040, SWR-041, SWR-043, SWR-045
 */

#ifndef CGM_SERVICE_H
#define CGM_SERVICE_H

#include "cgm_types.h"
#include "config/error_codes.h"

/**
 * @brief Register the CGM GATT service with the BLE stack (SWR-040).
 */
cgm_error_t cgm_service_init(void);

/**
 * @brief Send a glucose reading notification to connected client (SWR-041).
 * @param[in] reading  The glucose reading to transmit.
 */
cgm_error_t cgm_service_notify(const glucose_reading_t *reading);

/**
 * @brief Handle a Record Access Control Point request (SWR-043).
 * Supports backfilling missed readings from flash storage.
 * @param[in] opcode    RACP opcode (report records, delete, etc.)
 * @param[in] operator  Filter operator
 * @param[in] operand   Filter operand (e.g., sequence number range)
 */
cgm_error_t cgm_service_handle_racp(uint8_t opcode, uint8_t operator,
                                     const uint8_t *operand, uint16_t len);

/**
 * @brief Handle a CGM Specific Ops Control Point (SOCP) request (SWR-045).
 *
 * Implements the CGM Profile control point characteristic (UUID 0x2AAC) that
 * lets a connected client read and configure the device's alert levels
 * (patient high/low glucose, rate-of-decrease/increase) and start or stop the
 * measurement session. Each request produces a response message (a value
 * response for a Get, or a Response Code for a Set/session op) that is written
 * into @p resp and indicated back to the client.
 *
 * This function is pure with respect to the BLE stack so it can be unit tested:
 * it parses @p req, applies the effect via the alert configuration, and writes
 * the response bytes into @p resp.
 *
 * @param[in]  req       Raw request bytes (op code followed by operand).
 * @param[in]  len       Length of @p req in bytes.
 * @param[out] resp      Buffer to receive the response message.
 * @param[in]  resp_max  Capacity of @p resp in bytes.
 * @return Number of response bytes written (0 if no response can be produced).
 */
uint16_t cgm_service_handle_socp(const uint8_t *req, uint16_t len,
                                 uint8_t *resp, uint16_t resp_max);

/**
 * @brief Check whether a measurement session is currently active.
 * Controlled via the SOCP Start/Stop Session op codes (SWR-045).
 */
bool cgm_service_session_active(void);

/**
 * @brief Update the CGM session runtime characteristic.
 * @param[in] runtime_minutes  Minutes since session start.
 */
cgm_error_t cgm_service_update_runtime(uint32_t runtime_minutes);

/**
 * @brief Update the CGM status characteristic.
 * @param[in] status  Current device status.
 */
cgm_error_t cgm_service_update_status(const device_status_t *status);

#endif /* CGM_SERVICE_H */
