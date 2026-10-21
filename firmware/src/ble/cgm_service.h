/**
 * @file cgm_service.h
 * @brief Bluetooth SIG CGM GATT Service implementation
 *
 * Implements the CGM Profile (UUID 0x181F) with characteristics for
 * glucose measurement notifications and historical data backfill.
 *
 * Implements: SWR-040, SWR-041, SWR-043
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
 * @brief Handle a remote in-vivo calibration request (SWR-045).
 * Applies a fingerstick reference written by the mobile app over the CGM
 * Specific Ops Control Point characteristic (0x2AAC).
 * @param[in] operand  SOCP request payload (op code + reference value).
 * @param[in] len      Length of the operand in bytes.
 */
cgm_error_t cgm_service_handle_calibration(const uint8_t *operand, uint16_t len);

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
