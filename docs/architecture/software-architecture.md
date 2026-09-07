# Software Architecture Description

Document ID: SAD-CGM3000-001 | Rev 2.0

## Target Platform

- MCU: Nordic nRF52840 (ARM Cortex-M4F, 1MB Flash, 256KB RAM)
- RTOS: Zephyr RTOS 3.4
- BLE Stack: Nordic SoftDevice S140

## Software Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Application Layer                    │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐ │
│  │  Alert    │ │  Glucose │ │  Session │ │  Data  │ │
│  │  Manager  │ │  Engine  │ │  Manager │ │  Logger│ │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └───┬────┘ │
├───────┼─────────────┼───────────┼────────────┼──────┤
│       │        Service Layer    │            │       │
│  ┌────┴─────┐ ┌────┴─────┐ ┌──┴──────┐ ┌──┴────┐  │
│  │  Signal  │ │Calibration│ │   BLE   │ │ Flash │  │
│  │Processing│ │  Module   │ │ Service │ │Storage│  │
│  └────┬─────┘ └────┬─────┘ └────┬────┘ └──┬────┘  │
├───────┼─────────────┼───────────┼──────────┼────────┤
│       │        Driver Layer     │          │        │
│  ┌────┴─────┐ ┌────┴─────┐ ┌──┴──────┐ ┌┴──────┐ │
│  │   ADC    │ │  Temp    │ │SoftDevice│ │  SPI  │ │
│  │  Driver  │ │  Sensor  │ │  (BLE)   │ │ Flash │ │
│  └──────────┘ └──────────┘ └─────────┘ └───────┘  │
├─────────────────────────────────────────────────────┤
│              Hardware Abstraction Layer (Zephyr)     │
├─────────────────────────────────────────────────────┤
│              nRF52840 Hardware                       │
└─────────────────────────────────────────────────────┘
```

## Module Descriptions

### Glucose Engine (main control loop)
Orchestrates the measurement cycle: triggers ADC sampling, runs signal processing, applies calibration, evaluates alerts, and pushes data to BLE and flash storage. Runs on a 5-minute RTOS timer.

### Signal Processing
Implements the filtering pipeline: raw ADC -> temperature compensation -> Butterworth low-pass filter -> outlier rejection -> glucose conversion. All processing uses fixed-point Q16.16 arithmetic to avoid floating-point overhead.

### Calibration Module
Manages factory and in-vivo calibration parameters. Stores parameters in protected flash with CRC integrity checks. Supports weighted least-squares recalibration from fingerstick reference values.

### Alert Manager
Evaluates glucose values and rate-of-change against configurable thresholds. Implements alert prioritization, snooze logic, and hysteresis to prevent alert oscillation near threshold boundaries.

### BLE Service
Implements the Bluetooth SIG CGM Profile (0x181F). Handles GATT characteristic updates, notifications, connection management, bonding, and data backfill via Record Access Control Point. The Specific Ops Control Point (0x2AAC) lets an authenticated client read and configure the device alert levels (patient high/low glucose, rate-of-decrease/increase) and start or stop the measurement session; configuration writes are validated through the Alert Manager before being applied.

### Flash Storage
Circular buffer implementation for glucose readings on external SPI flash. Provides wear leveling and power-loss-safe writes using a write-ahead log pattern.

### Power Manager
Controls sleep/wake transitions based on scheduled events. Manages peripheral power gating, BLE advertising intervals, and battery monitoring.

## Data Flow

1. RTOS timer fires every ~100ms for ADC sampling (10 Hz)
2. Every 5 minutes, signal processing pipeline runs on accumulated samples
3. Calibration converts filtered current to glucose
4. Alert manager evaluates the new reading
5. Reading + alerts stored to flash circular buffer
6. If BLE connected, notification sent to mobile app
7. System returns to low-power sleep
