#!/usr/bin/env python3
"""
Post-Market Data Analyzer for GlucoSense CGM-3000

Analyzes exported glucose data for accuracy metrics, sensor performance
trending, and post-market surveillance reporting. Used by the quality
team to monitor fleet-wide device performance.

Metrics computed:
- MARD (Mean Absolute Relative Difference) per SYS-REQ-002
- Sensor lifetime distribution per SYS-REQ-005
- Alert frequency analysis per SWR-030 through SWR-034
- Signal quality metrics
"""

import csv
import math
import statistics
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


@dataclass
class GlucoseReading:
    """A single glucose measurement from device export."""
    timestamp: int          # Unix epoch seconds
    glucose_mgdl: int       # Measured glucose
    reference_mgdl: Optional[int]  # Paired YSI reference (if available)
    trend: str              # stable, rising, falling, etc.
    alert_active: bool
    sensor_id: str


@dataclass
class AccuracyReport:
    """MARD and accuracy zone analysis results."""
    mard_percent: float
    total_pairs: int
    pairs_in_zone_a: int    # Clarke Error Grid Zone A
    pairs_in_zone_b: int
    median_absolute_diff: float
    pct_within_15: float    # % of readings within ±15 mg/dL (for <100)
    pct_within_15pct: float # % of readings within ±15% (for >=100)


@dataclass
class SensorReport:
    """Per-sensor performance summary."""
    sensor_id: str
    total_readings: int
    valid_readings: int
    sensor_hours: float
    mean_glucose: float
    time_in_range_pct: float  # 70-180 mg/dL
    time_below_range_pct: float
    time_above_range_pct: float
    alert_count: int
    mard: Optional[float] = None


def load_glucose_data(filepath: Path) -> list[GlucoseReading]:
    """Load glucose readings from a CSV export file."""
    readings = []
    with open(filepath, newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            ref = int(row['reference_mgdl']) if row.get('reference_mgdl') else None
            readings.append(GlucoseReading(
                timestamp=int(row['timestamp']),
                glucose_mgdl=int(row['glucose_mgdl']),
                reference_mgdl=ref,
                trend=row.get('trend', 'unknown'),
                alert_active=row.get('alert_active', '0') == '1',
                sensor_id=row.get('sensor_id', 'unknown'),
            ))
    return readings


def compute_mard(readings: list[GlucoseReading]) -> AccuracyReport:
    """
    Compute Mean Absolute Relative Difference (SYS-REQ-002).

    MARD is the primary accuracy metric for CGM devices.
    Only paired readings (with YSI reference) are included.

    Formula: MARD = mean(|CGM - Reference| / Reference) * 100%
    Target: < 10% per SYS-REQ-002
    """
    paired = [r for r in readings if r.reference_mgdl is not None and r.reference_mgdl > 0]

    if not paired:
        return AccuracyReport(
            mard_percent=0.0, total_pairs=0, pairs_in_zone_a=0,
            pairs_in_zone_b=0, median_absolute_diff=0.0,
            pct_within_15=0.0, pct_within_15pct=0.0
        )

    absolute_relative_diffs = []
    absolute_diffs = []
    within_15_count = 0
    within_15pct_count = 0
    zone_a_count = 0
    zone_b_count = 0

    for r in paired:
        diff = abs(r.glucose_mgdl - r.reference_mgdl)
        relative_diff = diff / r.reference_mgdl

        absolute_relative_diffs.append(relative_diff)
        absolute_diffs.append(diff)

        # Clarke Error Grid Zone classification (simplified)
        if relative_diff <= 0.20 or diff <= 15:
            zone_a_count += 1
        elif relative_diff <= 0.40 or diff <= 30:
            zone_b_count += 1

        # Consensus accuracy criteria
        if r.reference_mgdl < 100:
            if diff <= 15:
                within_15_count += 1
        else:
            if relative_diff <= 0.15:
                within_15pct_count += 1

    mard = statistics.mean(absolute_relative_diffs) * 100.0

    low_ref_count = sum(1 for r in paired if r.reference_mgdl < 100)
    high_ref_count = len(paired) - low_ref_count

    return AccuracyReport(
        mard_percent=mard,
        total_pairs=len(paired),
        pairs_in_zone_a=zone_a_count,
        pairs_in_zone_b=zone_b_count,
        median_absolute_diff=statistics.median(absolute_diffs),
        pct_within_15=within_15_count / max(low_ref_count, 1) * 100,
        pct_within_15pct=within_15pct_count / max(high_ref_count, 1) * 100,
    )


def compute_sensor_report(readings: list[GlucoseReading],
                          sensor_id: str) -> SensorReport:
    """Generate a performance summary for a single sensor."""
    sensor_readings = [r for r in readings if r.sensor_id == sensor_id]

    if not sensor_readings:
        return SensorReport(sensor_id=sensor_id, total_readings=0,
                            valid_readings=0, sensor_hours=0, mean_glucose=0,
                            time_in_range_pct=0, time_below_range_pct=0,
                            time_above_range_pct=0, alert_count=0)

    valid = [r for r in sensor_readings if 40 <= r.glucose_mgdl <= 400]
    glucose_values = [r.glucose_mgdl for r in valid]

    # Time calculations (assume 5-minute intervals)
    duration_hours = len(sensor_readings) * 5.0 / 60.0

    # Time-in-range analysis (70-180 mg/dL target range)
    in_range = sum(1 for g in glucose_values if 70 <= g <= 180)
    below = sum(1 for g in glucose_values if g < 70)
    above = sum(1 for g in glucose_values if g > 180)
    total_valid = len(glucose_values) if glucose_values else 1

    # Paired accuracy if references available
    paired = [r for r in sensor_readings if r.reference_mgdl]
    mard = None
    if paired:
        report = compute_mard(paired)
        mard = report.mard_percent

    return SensorReport(
        sensor_id=sensor_id,
        total_readings=len(sensor_readings),
        valid_readings=len(valid),
        sensor_hours=duration_hours,
        mean_glucose=statistics.mean(glucose_values) if glucose_values else 0,
        time_in_range_pct=in_range / total_valid * 100,
        time_below_range_pct=below / total_valid * 100,
        time_above_range_pct=above / total_valid * 100,
        alert_count=sum(1 for r in sensor_readings if r.alert_active),
        mard=mard,
    )


def generate_fleet_summary(readings: list[GlucoseReading]) -> dict:
    """Generate fleet-wide summary for post-market surveillance."""
    sensor_ids = list(set(r.sensor_id for r in readings))

    sensor_reports = [compute_sensor_report(readings, sid) for sid in sensor_ids]

    lifetimes = [s.sensor_hours for s in sensor_reports if s.sensor_hours > 0]
    mards = [s.mard for s in sensor_reports if s.mard is not None]

    return {
        'total_sensors': len(sensor_ids),
        'total_readings': len(readings),
        'fleet_mard': statistics.mean(mards) if mards else None,
        'mean_sensor_life_hours': statistics.mean(lifetimes) if lifetimes else 0,
        'median_sensor_life_hours': statistics.median(lifetimes) if lifetimes else 0,
        'sensors_reaching_14_days': sum(1 for h in lifetimes if h >= 336),
        'mean_time_in_range': statistics.mean(
            [s.time_in_range_pct for s in sensor_reports]
        ) if sensor_reports else 0,
    }


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description='GlucoSense CGM-3000 Post-Market Data Analyzer'
    )
    parser.add_argument('input_file', type=Path,
                        help='CSV file with glucose readings')
    parser.add_argument('--report-type', choices=['accuracy', 'sensor', 'fleet'],
                        default='fleet', help='Type of report to generate')
    parser.add_argument('--sensor-id', help='Sensor ID for single-sensor report')

    args = parser.parse_args()

    readings = load_glucose_data(args.input_file)
    print(f"Loaded {len(readings)} readings")

    if args.report_type == 'accuracy':
        report = compute_mard(readings)
        print(f"\n=== Accuracy Report (SYS-REQ-002) ===")
        print(f"MARD:              {report.mard_percent:.1f}%")
        print(f"Total paired:      {report.total_pairs}")
        print(f"Zone A:            {report.pairs_in_zone_a} "
              f"({report.pairs_in_zone_a/max(report.total_pairs,1)*100:.1f}%)")
        print(f"Median abs diff:   {report.median_absolute_diff:.1f} mg/dL")
        target = "PASS" if report.mard_percent < 10.0 else "FAIL"
        print(f"\nMARD < 10% target: {target}")

    elif args.report_type == 'sensor' and args.sensor_id:
        report = compute_sensor_report(readings, args.sensor_id)
        print(f"\n=== Sensor Report: {report.sensor_id} ===")
        print(f"Duration:         {report.sensor_hours:.1f} hours")
        print(f"Valid readings:   {report.valid_readings}/{report.total_readings}")
        print(f"Mean glucose:     {report.mean_glucose:.0f} mg/dL")
        print(f"Time in range:    {report.time_in_range_pct:.1f}%")
        print(f"Time below range: {report.time_below_range_pct:.1f}%")
        print(f"Time above range: {report.time_above_range_pct:.1f}%")
        print(f"Alert count:      {report.alert_count}")

    elif args.report_type == 'fleet':
        summary = generate_fleet_summary(readings)
        print(f"\n=== Fleet Summary (Post-Market Surveillance) ===")
        print(f"Total sensors:        {summary['total_sensors']}")
        print(f"Total readings:       {summary['total_readings']}")
        if summary['fleet_mard'] is not None:
            print(f"Fleet MARD:           {summary['fleet_mard']:.1f}%")
        print(f"Mean sensor life:     {summary['mean_sensor_life_hours']:.0f} hours")
        print(f"Median sensor life:   {summary['median_sensor_life_hours']:.0f} hours")
        print(f"Sensors reaching 14d: {summary['sensors_reaching_14_days']}")
        print(f"Mean time-in-range:   {summary['mean_time_in_range']:.1f}%")


if __name__ == '__main__':
    main()
