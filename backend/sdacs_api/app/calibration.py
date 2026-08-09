"""SDACS backend module: Computes SPL calibration previews and eligibility checks from synchronized four-node capture telemetry.

The module is part of the Raspberry Pi middleware/API layer used by the final SDACS system.
"""

from __future__ import annotations

import math
from datetime import datetime, timezone
from statistics import median
from typing import Any


CALIBRATION_OFFSET_MIN_DB = 60.0
CALIBRATION_OFFSET_MAX_DB = 180.0
CALIBRATION_TOLERANCE_DB = 1.0
MIN_CALIBRATION_SAMPLES = 3
MIN_TONE_DETECTION_RATE = 0.60
TONE_FREQUENCY_MIN_HZ = 900.0
TONE_FREQUENCY_MAX_HZ = 1100.0


def _finite(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    number = float(value)
    return number if math.isfinite(number) else None


def _field(row: dict[str, Any], name: str) -> Any:
    value = row.get(name)
    if value is not None:
        return value
    raw = row.get("raw_json")
    return raw.get(name) if isinstance(raw, dict) else None


def _finite_values(rows: list[dict[str, Any]], name: str) -> list[float]:
    values: list[float] = []
    for row in rows:
        value = _finite(_field(row, name))
        if value is not None:
            values.append(value)
    return values


def _bool_values(rows: list[dict[str, Any]], name: str) -> list[bool]:
    values: list[bool] = []
    for row in rows:
        value = _field(row, name)
        if isinstance(value, bool):
            values.append(value)
        elif isinstance(value, (int, float)) and not isinstance(value, bool):
            values.append(value != 0)
        elif isinstance(value, str):
            normalized = value.strip().lower()
            if normalized in {"true", "1", "yes"}:
                values.append(True)
            elif normalized in {"false", "0", "no"}:
                values.append(False)
    return values


def build_calibration_preview(
    capture_id: str,
    reference_spl_db: float,
    telemetry: list[dict[str, Any]],
    expected_nodes: list[str],
) -> dict[str, Any]:
    """Build a safe, backend-authoritative SPL calibration proposal.

    The persisted firmware calibration value is an additive dB offset:

        dB SPL = dBFS + cal_offset_db

    Therefore the proposed value is reference_spl_db - measured_dbfs.  The
    dedicated firmware 1 kHz detector is used instead of the broad FFT peak,
    because the broad low-frequency peak estimate is not reliable enough to
    validate a 1 kHz calibration tone.
    """

    grouped: dict[str, list[dict[str, Any]]] = {
        node_id: [] for node_id in expected_nodes
    }
    for row in telemetry:
        if row.get("record_type") != "features":
            continue
        node_id = str(row.get("node_id") or "")
        if node_id in grouped:
            grouped[node_id].append(row)

    node_results: list[dict[str, Any]] = []
    global_warnings: list[str] = []
    eligible_count = 0

    for node_id in expected_nodes:
        rows = grouped[node_id]
        dbfs_values = _finite_values(rows, "dbfs")
        spl_values = _finite_values(rows, "db_spl")
        offset_values = _finite_values(rows, "cal_offset_db")
        tone_peak_values = [
            value
            for value in _finite_values(rows, "tone_1khz_peak_hz")
            if value > 0
        ]
        tone_detected_values = _bool_values(rows, "tone_1khz_detected")
        clipped_values = [
            value
            for field in (
                "clipped_sample_count",
                "spectral_clipped_sample_count",
            )
            for value in _finite_values(rows, field)
        ]

        measured_dbfs = median(dbfs_values) if dbfs_values else None
        current_offset = median(offset_values) if offset_values else None
        if current_offset is None and dbfs_values and spl_values:
            paired_offsets = []
            for row in rows:
                dbfs = _finite(_field(row, "dbfs"))
                spl = _finite(_field(row, "db_spl"))
                if dbfs is not None and spl is not None:
                    paired_offsets.append(spl - dbfs)
            current_offset = median(paired_offsets) if paired_offsets else None

        measured_spl = (
            measured_dbfs + current_offset
            if measured_dbfs is not None and current_offset is not None
            else median(spl_values)
            if spl_values
            else None
        )
        suggested_offset = (
            reference_spl_db - measured_dbfs if measured_dbfs is not None else None
        )
        adjustment = (
            suggested_offset - current_offset
            if suggested_offset is not None and current_offset is not None
            else None
        )
        measurement_error = (
            measured_spl - reference_spl_db if measured_spl is not None else None
        )
        representative_frequency = (
            median(tone_peak_values) if tone_peak_values else None
        )
        tone_detected_count = sum(tone_detected_values)
        tone_detection_rate = (
            tone_detected_count / len(tone_detected_values)
            if tone_detected_values
            else None
        )

        warnings: list[str] = []
        eligible = True
        if len(dbfs_values) < MIN_CALIBRATION_SAMPLES:
            warnings.append(
                f"At least {MIN_CALIBRATION_SAMPLES} valid dBFS samples are required."
            )
            eligible = False
        if not tone_detected_values:
            warnings.append(
                "The firmware did not report dedicated 1 kHz tone-detection results."
            )
            eligible = False
        elif tone_detection_rate is None or tone_detection_rate < MIN_TONE_DETECTION_RATE:
            warnings.append(
                f"The 1 kHz tone was detected in only {tone_detected_count} of "
                f"{len(tone_detected_values)} samples; at least "
                f"{MIN_TONE_DETECTION_RATE:.0%} is required."
            )
            eligible = False
        if representative_frequency is None:
            warnings.append("No valid 1 kHz detector peak-frequency samples were captured.")
            eligible = False
        elif not TONE_FREQUENCY_MIN_HZ <= representative_frequency <= TONE_FREQUENCY_MAX_HZ:
            warnings.append(
                "The dedicated tone-detector peak was not close enough to 1 kHz."
            )
            eligible = False
        if any(value > 0 for value in clipped_values):
            warnings.append(
                "Clipped samples were detected; reduce the playback level and repeat."
            )
            eligible = False
        if suggested_offset is None:
            warnings.append("A suggested calibration offset could not be calculated.")
            eligible = False
        elif not CALIBRATION_OFFSET_MIN_DB <= suggested_offset <= CALIBRATION_OFFSET_MAX_DB:
            warnings.append(
                f"Suggested offset is outside the firmware range "
                f"{CALIBRATION_OFFSET_MIN_DB:.0f}–{CALIBRATION_OFFSET_MAX_DB:.0f} dB."
            )
            eligible = False

        if eligible:
            eligible_count += 1

        node_results.append(
            {
                "node_id": node_id,
                "sample_count": len(dbfs_values),
                "measured_dbfs": measured_dbfs,
                "measured_spl_db": measured_spl,
                "current_offset_db": current_offset,
                "suggested_offset_db": suggested_offset,
                "adjustment_db": adjustment,
                "measurement_error_db": measurement_error,
                "representative_frequency_hz": representative_frequency,
                "tone_detected_count": tone_detected_count,
                "tone_sample_count": len(tone_detected_values),
                "tone_detection_rate": tone_detection_rate,
                "eligible": eligible,
                "within_tolerance": (
                    abs(measurement_error) <= CALIBRATION_TOLERANCE_DB
                    if measurement_error is not None
                    else None
                ),
                "warnings": warnings,
            }
        )

    if eligible_count == len(expected_nodes):
        status = "ready"
    elif eligible_count:
        status = "partial"
        global_warnings.append(
            f"Only {eligible_count} of {len(expected_nodes)} nodes have valid calibration data."
        )
    else:
        status = "invalid"
        global_warnings.append("No node has valid calibration data for this capture.")

    return {
        "capture_id": capture_id,
        "reference_spl_db": reference_spl_db,
        "status": status,
        "tolerance_db": CALIBRATION_TOLERANCE_DB,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "nodes": node_results,
        "warnings": global_warnings,
    }
