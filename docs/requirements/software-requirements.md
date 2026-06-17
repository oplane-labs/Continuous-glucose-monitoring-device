# Software Requirements Specification

Document ID: SWRS-CGM3000-001
Revision: 4.0
Effective Date: 2025-09-10

## 1. Sensor Interface Requirements

### SWR-001: ADC Sampling
The firmware shall sample the electrochemical sensor via the 12-bit ADC at a rate of 10 Hz, collecting raw current measurements from the working electrode.
- **Traces to**: SYS-REQ-001, SYS-REQ-003
- **Verification**: Unit Test (SWR-VER-001)
- **Safety Class**: C

### SWR-002: Sensor Current Range
The firmware shall accept raw sensor current values in the range of 0.5 nA to 50 nA, corresponding to the glucose measurement range of 40-400 mg/dL.
- **Traces to**: SYS-REQ-001
- **Verification**: Unit Test (SWR-VER-002)
- **Safety Class**: C

### SWR-003: Sensor Fault Detection
The firmware shall detect sensor faults including open circuit (current < 0.1 nA for > 10 seconds), short circuit (current > 100 nA), and noise floor violation (signal-to-noise ratio < 3 dB), and set the sensor status to FAULT within 30 seconds of fault occurrence.
- **Traces to**: SYS-REQ-014
- **Verification**: Unit Test (SWR-VER-003)
- **Safety Class**: C
- **Risk Control**: RC-008

### SWR-004: Warm-Up Sequence
The firmware shall execute a sensor warm-up sequence consisting of:
1. Apply stabilization voltage (+0.535V) for 30 minutes
2. Run impedance check to verify sensor hydration
3. Collect initial calibration samples for 30 minutes
4. Transition to normal measurement mode only after calibration validates within ±20%
- **Traces to**: SYS-REQ-004
- **Verification**: Integration Test (SWR-VER-004)
- **Safety Class**: B

### SWR-005: Sensor Lifetime Tracking
The firmware shall track cumulative sensor insertion time and disable glucose reporting after 14 days (20,160 minutes), displaying an "end of sensor life" message.
- **Traces to**: SYS-REQ-005
- **Verification**: Unit Test (SWR-VER-005)
- **Safety Class**: B

## 2. Signal Processing Requirements

### SWR-010: Noise Filtering
The firmware shall apply a 4th-order Butterworth low-pass filter with a cutoff frequency of 0.1 Hz to raw sensor current readings to remove high-frequency noise.
- **Traces to**: SYS-REQ-002
- **Verification**: Unit Test (SWR-VER-010)
- **Safety Class**: C

### SWR-011: Outlier Rejection
The firmware shall reject individual sensor readings that deviate more than 3 standard deviations from the rolling 15-minute median and replace them with the median value.
- **Traces to**: SYS-REQ-002
- **Verification**: Unit Test (SWR-VER-011)
- **Safety Class**: C

### SWR-012: Glucose Conversion
The firmware shall convert filtered sensor current (nA) to glucose concentration (mg/dL) using the linear calibration equation: glucose = (current - offset) * sensitivity, where offset and sensitivity are device-specific calibration parameters stored in non-volatile memory.
- **Traces to**: SYS-REQ-001, SYS-REQ-002
- **Verification**: Unit Test (SWR-VER-012)
- **Safety Class**: C
- **Risk Control**: RC-002

### SWR-013: Rate of Change Calculation
The firmware shall calculate the glucose rate of change (mg/dL/min) using linear regression over the most recent 15 minutes of glucose readings (3 data points at 5-minute intervals).
- **Traces to**: SYS-REQ-012, SYS-REQ-013, SYS-REQ-020
- **Verification**: Unit Test (SWR-VER-013)
- **Safety Class**: C

### SWR-014: Range Clamping
The firmware shall clamp calculated glucose values to the valid measurement range [40, 400] mg/dL. Values below 40 shall be reported as "LOW" and values above 400 shall be reported as "HIGH".
- **Traces to**: SYS-REQ-001
- **Verification**: Unit Test (SWR-VER-014)
- **Safety Class**: C
- **Risk Control**: RC-001

## 3. Calibration Requirements

### SWR-020: Factory Calibration
The firmware shall store factory calibration parameters (sensitivity, offset, temperature coefficients) in a protected region of flash memory, programmed during manufacturing.
- **Traces to**: SYS-REQ-002
- **Verification**: Unit Test (SWR-VER-020)
- **Safety Class**: C

### SWR-021: In-Vivo Calibration Adjustment
The firmware shall support optional in-vivo calibration using a fingerstick blood glucose reference value. The calibration algorithm shall adjust the sensitivity parameter using a weighted least-squares fit with exponential forgetting (lambda = 0.95).
- **Traces to**: SYS-REQ-002, UN-007
- **Verification**: Unit Test (SWR-VER-021)
- **Safety Class**: C
- **Risk Control**: RC-009

### SWR-022: Calibration Validity Check
The firmware shall reject calibration reference values that differ from the current sensor reading by more than 40% and prompt the user to retry with a new fingerstick measurement.
- **Traces to**: SYS-REQ-002
- **Verification**: Unit Test (SWR-VER-022)
- **Safety Class**: C

### SWR-023: Temperature Compensation
The firmware shall apply temperature compensation to sensor readings using the equation: compensated_current = raw_current * (1 + temp_coeff * (temperature - 37.0)), where temp_coeff is a factory-calibrated parameter.
- **Traces to**: SYS-REQ-002, SYS-REQ-060
- **Verification**: Unit Test (SWR-VER-023)
- **Safety Class**: C

## 4. Alert Management Requirements

### SWR-030: Low Glucose Alert Generation
The firmware shall generate a LOW_GLUCOSE alert when 2 consecutive glucose readings (10 minutes) fall below the configured low threshold.
- **Traces to**: SYS-REQ-010
- **Verification**: Unit Test (SWR-VER-030)
- **Safety Class**: C
- **Risk Control**: RC-003

### SWR-031: High Glucose Alert Generation
The firmware shall generate a HIGH_GLUCOSE alert when 3 consecutive glucose readings (15 minutes) exceed the configured high threshold.
- **Traces to**: SYS-REQ-011
- **Verification**: Unit Test (SWR-VER-031)
- **Safety Class**: B

### SWR-032: Rapid Fall Alert
The firmware shall generate a RAPID_FALL alert when the calculated rate of change is less than -2 mg/dL/min for 3 consecutive calculations.
- **Traces to**: SYS-REQ-012
- **Verification**: Unit Test (SWR-VER-032)
- **Safety Class**: C
- **Risk Control**: RC-005

### SWR-033: Alert Prioritization
The firmware shall prioritize alerts in the following order (highest first):
1. SENSOR_FAULT (Priority: Critical)
2. LOW_GLUCOSE (Priority: Urgent)
3. RAPID_FALL (Priority: Urgent)
4. HIGH_GLUCOSE (Priority: Warning)
5. RAPID_RISE (Priority: Warning)
6. LOW_BATTERY (Priority: Info)
7. SIGNAL_LOSS (Priority: Info)
- **Traces to**: SYS-REQ-010, SYS-REQ-011, SYS-REQ-014
- **Verification**: Unit Test (SWR-VER-033)
- **Safety Class**: C

### SWR-034: Alert Snooze
The firmware shall support a configurable snooze period (15, 30, 60, or 120 minutes) for non-critical alerts. Critical alerts (SENSOR_FAULT, LOW_GLUCOSE with reading < 45 mg/dL) shall not be snoozable.
- **Traces to**: UN-003, UN-004
- **Verification**: Unit Test (SWR-VER-034)
- **Safety Class**: B

## 5. BLE Communication Requirements

### SWR-040: CGM GATT Service
The firmware shall implement a BLE GATT service conforming to the Bluetooth SIG CGM Profile (UUID: 0x181F) with the following characteristics:
- CGM Measurement (UUID: 0x2AA7)
- CGM Feature (UUID: 0x2AA8)
- CGM Status (UUID: 0x2AA9)
- CGM Session Start Time (UUID: 0x2AAA)
- CGM Session Run Time (UUID: 0x2AAB)
- Record Access Control Point (UUID: 0x2A52)
- **Traces to**: SYS-REQ-030
- **Verification**: Integration Test (SWR-VER-040)
- **Safety Class**: B

### SWR-041: Data Notification
The firmware shall send a BLE notification containing the latest glucose measurement, trend indicator, and status flags every 5 minutes when a client is connected and subscribed.
- **Traces to**: SYS-REQ-003, SYS-REQ-030
- **Verification**: Integration Test (SWR-VER-041)
- **Safety Class**: B

### SWR-042: Connection Security
The firmware shall require BLE Secure Connections (LE Secure Connections pairing with numeric comparison) and AES-128-CCM encryption for all data exchanges.
- **Traces to**: SYS-REQ-032
- **Verification**: Integration Test (SWR-VER-042)
- **Safety Class**: B
- **Risk Control**: RC-010

### SWR-043: Data Backfill
The firmware shall support backfilling of missed glucose readings (stored in flash) upon reconnection via the Record Access Control Point characteristic. Up to 96 readings (8 hours) shall be available for backfill.
- **Traces to**: SYS-REQ-040
- **Verification**: Integration Test (SWR-VER-043)
- **Safety Class**: B

### SWR-044: Advertising Behavior
The firmware shall advertise with a 1-second interval when no device is connected, and reduce to a 2-second interval after 5 minutes of advertising without a connection to conserve power.
- **Traces to**: SYS-REQ-050
- **Verification**: Unit Test (SWR-VER-044)
- **Safety Class**: A

## 6. Storage Requirements

### SWR-050: Flash Data Storage
The firmware shall store glucose readings in a circular buffer in flash memory with capacity for at least 96 entries (8 hours at 5-minute intervals). Each entry shall contain: timestamp (4 bytes), glucose value (2 bytes), status flags (1 byte), trend (1 byte).
- **Traces to**: SYS-REQ-040
- **Verification**: Unit Test (SWR-VER-050)
- **Safety Class**: B

### SWR-051: Flash Wear Leveling
The firmware shall implement wear leveling across flash memory pages to ensure a minimum of 100,000 write cycles per page over the device lifetime.
- **Traces to**: SYS-REQ-005
- **Verification**: Unit Test (SWR-VER-051)
- **Safety Class**: A

### SWR-052: Calibration Data Protection
The firmware shall store calibration parameters in a write-protected flash region with CRC-32 integrity verification. The firmware shall verify calibration data integrity at startup and after each calibration update.
- **Traces to**: SWR-020, SYS-REQ-002
- **Verification**: Unit Test (SWR-VER-052)
- **Safety Class**: C

## 7. Power Management Requirements

### SWR-060: Low Power Sleep Mode
The firmware shall enter a low-power sleep mode (< 5 uA current draw) between measurement cycles, waking only for scheduled ADC sampling, BLE events, and periodic housekeeping.
- **Traces to**: SYS-REQ-050
- **Verification**: Test (SWR-VER-060)
- **Safety Class**: A

### SWR-061: Battery Monitoring
The firmware shall measure battery voltage via ADC at least once per hour and calculate remaining battery life based on a discharge curve model. The firmware shall generate a LOW_BATTERY alert when estimated remaining life falls below 24 hours.
- **Traces to**: SYS-REQ-051
- **Verification**: Unit Test (SWR-VER-061)
- **Safety Class**: B
- **Risk Control**: RC-007

### SWR-062: Critical Battery Shutdown
The firmware shall perform a graceful shutdown sequence (save current state to flash, notify connected device, disable sensor) when battery voltage drops below 2.0V to prevent data corruption.
- **Traces to**: SYS-REQ-050
- **Verification**: Unit Test (SWR-VER-062)
- **Safety Class**: B

## 8. Glucose Statistics Requirements

### SWR-080: Rolling Glucose Sample Buffer
The firmware shall maintain rolling 24-hour and 7-day buffers of valid glucose readings, fed by the measurement pipeline and indexed by reading timestamp. Readings flagged invalid (`GLUCOSE_INVALID`) or outside the physiological range [40, 400] mg/dL shall be dropped. Stale entries shall be evicted from the buffers based on reading timestamps so that wall-clock drift, watchdog recovery, and sensor gaps do not corrupt the windows.
- **Traces to**: SYS-REQ-022 (clinical reporting)
- **Verification**: Unit Test (SWR-VER-080)
- **Safety Class**: B

### SWR-081: Time-in-Range / Below / Above Reporting
The firmware shall compute, on demand, the percentages of readings inside, below, and above a configurable in-range band (default 70–180 mg/dL per ATTD 2019), reported in tenths of a percent. Snapshots taken from windows with fewer than 12 samples (1 hour of data) shall return `CGM_ERR_SIGNAL_INSUFFICIENT` rather than misleading values.
- **Traces to**: UN-005 (clinical metrics), SYS-REQ-022
- **Verification**: Unit Test (SWR-VER-081)
- **Safety Class**: B

### SWR-082: Glucose Management Indicator and Coefficient of Variation
The firmware shall compute the Glucose Management Indicator (GMI) per the Bergenstal et al. (2018) formula `GMI(%) = 3.31 + 0.02392 × mean_glucose_mgdl` and the Coefficient of Variation `CV(%) = 100 × stddev / mean` for both the 24-hour and 7-day windows. Both metrics shall be reported in % × 10.
- **Traces to**: UN-005 (clinical metrics)
- **Verification**: Unit Test (SWR-VER-082)
- **Safety Class**: B

## 9. Watchdog and Self-Test Requirements

### SWR-070: Watchdog Timer
The firmware shall configure a hardware watchdog timer with a 4-second timeout. The main loop shall service the watchdog at least once per iteration. Failure to service the watchdog shall trigger a system reset.
- **Traces to**: Safety
- **Verification**: Unit Test (SWR-VER-070)
- **Safety Class**: C

### SWR-071: Power-On Self-Test
The firmware shall execute a power-on self-test (POST) at startup that verifies:
1. RAM integrity (walking ones test on reserved region)
2. Flash integrity (CRC check of firmware image)
3. ADC functionality (reference voltage measurement within ±5%)
4. BLE radio initialization
5. Sensor connectivity (impedance check)
The system shall not enter normal operation if any POST check fails.
- **Traces to**: Safety
- **Verification**: Integration Test (SWR-VER-071)
- **Safety Class**: C
