"""SDACS backend module: Builds deterministic multi-node feature windows for the Edge Impulse model and exports model-ready feature records.

The module is part of the Raspberry Pi middleware/API layer used by the final SDACS system.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Mapping

SDACS_V3_SOURCE_FIELDS = (
    "dbfs",
    "f_peak_acoustic_hz",
    "low_rumble_ratio",
    "band_bass_ratio",
    "band_low_mid_ratio",
    "band_mid_ratio",
    "band_presence_ratio",
    "band_high_ratio",
    "band_bass_peak_hz",
    "band_low_mid_peak_hz",
    "band_mid_peak_hz",
    "band_presence_peak_hz",
    "band_high_peak_hz",
    "dominant_band_ratio",
    "dominant_band_peak_hz",
    "fft_low_ratio",
    "fft_mid_ratio",
    "fft_high_ratio",
    "fft_total_energy",
)
SDACS_NODE_IDS = ("node01", "node02", "node03", "node04")
SDACS_V3_FEATURE_NAMES = tuple(
    f"{source}_{statistic}"
    for source in SDACS_V3_SOURCE_FIELDS
    for statistic in ("mean", "std", "range")
)
SDACS_V3_FEATURE_SCHEMA = "sdacs_v3_57_feature_room_window_scene_hpf_v2"
SDACS_V3_SOURCE_PATH = "scene_hpf_150hz_gain16"
SDACS_V3_FEATURE_PROVENANCE_VERIFIED = True
SDACS_V3_FEATURE_PROVENANCE_ERROR = None

assert len(SDACS_V3_SOURCE_FIELDS) == 19
assert len(SDACS_V3_FEATURE_NAMES) == 57


class FeatureWindowError(ValueError):
    """Raised when one synchronized four-node window cannot be aggregated."""


def _sample_index(row: Mapping[str, Any]) -> int:
    value = row.get("sample_index", row.get("seq"))
    if isinstance(value, bool) or value is None:
        raise FeatureWindowError("Missing sample_index/seq.")
    try:
        index = int(value)
    except (TypeError, ValueError) as exc:
        raise FeatureWindowError(f"Invalid sample_index/seq: {value!r}.") from exc
    if index < 0:
        raise FeatureWindowError(f"Invalid negative sample index: {index}.")
    return index


def _capture_id(row: Mapping[str, Any]) -> str | None:
    value = row.get("capture_id", row.get("run_id", row.get("request_id")))
    return str(value) if value not in (None, "") else None


def _finite_number(row: Mapping[str, Any], field: str, node_id: str) -> float:
    scene_field = f"scene_{field}"
    scene_value = row.get(scene_field)
    scene_valid = row.get("scene_metrics_valid")
    # A firmware record that explicitly marks scene metrics invalid must fall
    # back to the unfiltered spectral path. Older/imported rows may omit the
    # validity flag, in which case a populated scene field is accepted.
    if scene_valid is True:
        use_scene = True
    elif scene_valid is False:
        use_scene = False
    else:
        use_scene = scene_value not in (None, "")
    selected_field = scene_field if use_scene else field
    value = scene_value if use_scene else row.get(field)
    if isinstance(value, bool) or value in (None, ""):
        raise FeatureWindowError(f"{node_id} is missing {selected_field}.")
    try:
        number = float(value)
    except (TypeError, ValueError) as exc:
        raise FeatureWindowError(f"{node_id} has a non-numeric {field}.") from exc
    if not math.isfinite(number):
        raise FeatureWindowError(f"{node_id} has a non-finite {field}.")
    return number


def build_feature_window(
    rows: Iterable[Mapping[str, Any]],
    *,
    capture_id: str,
    sample_index: int,
) -> dict[str, Any]:
    """Build one 57-feature room-level input vector from a synchronized four-node instant.

    A window is accepted only when node01-node04 each contribute exactly one finite
    source row. For every source feature the backend computes mean, population
    standard deviation, and range, preserving the feature order expected by the
    deployed Edge Impulse model.
    """
    selected = [
        dict(row)
        for row in rows
        if _capture_id(row) == capture_id and _sample_index(row) == sample_index
    ]
    counts = Counter(str(row.get("node_id", "")) for row in selected)
    duplicates = sorted(node for node, count in counts.items() if node and count > 1)
    if duplicates:
        raise FeatureWindowError(f"Duplicate node rows: {', '.join(duplicates)}.")
    missing = sorted(set(SDACS_NODE_IDS) - set(counts))
    unexpected = sorted(set(counts) - set(SDACS_NODE_IDS) - {""})
    if missing:
        raise FeatureWindowError(f"Missing node rows: {', '.join(missing)}.")
    if unexpected or "" in counts:
        raise FeatureWindowError("Unexpected or missing node_id.")
    if len(selected) != len(SDACS_NODE_IDS):
        raise FeatureWindowError("Expected exactly four source rows.")

    by_node = {str(row["node_id"]): row for row in selected}
    named: dict[str, float] = {}
    ordered_values: list[float] = []
    for field in SDACS_V3_SOURCE_FIELDS:
        values = [_finite_number(by_node[node], field, node) for node in SDACS_NODE_IDS]
        mean = sum(values) / len(values)
        population_std = math.sqrt(
            sum((value - mean) ** 2 for value in values) / len(values)
        )
        statistics = (mean, population_std, max(values) - min(values))
        for suffix, value in zip(("mean", "std", "range"), statistics, strict=True):
            name = f"{field}_{suffix}"
            named[name] = value
            ordered_values.append(value)

    return {
        "capture_id": capture_id,
        "sample_index": sample_index,
        "feature_schema": SDACS_V3_FEATURE_SCHEMA,
        "source_path": SDACS_V3_SOURCE_PATH,
        "source_node_ids": list(SDACS_NODE_IDS),
        "source_rows": [by_node[node] for node in SDACS_NODE_IDS],
        "aggregation_rules": {
            "group_by": ["capture_id", "sample_index"],
            "required_node_ids": list(SDACS_NODE_IDS),
            "mean": "arithmetic mean across four nodes",
            "standard_deviation": "population standard deviation (ddof=0)",
            "range": "maximum minus minimum",
            "feature_source": "prefer scene_* HPF fields; fall back to legacy unfiltered fields",
            "missing_values": "reject window",
            "duplicate_nodes": "reject window",
            "non_finite_values": "reject window",
        },
        "feature_names": list(SDACS_V3_FEATURE_NAMES),
        "feature_values": ordered_values,
        "features": named,
    }


def build_capture_windows(
    capture_id: str, rows: Iterable[Mapping[str, Any]]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    capture_rows = [
        dict(row)
        for row in rows
        if _capture_id(row) == capture_id and row.get("record_type", "features") == "features"
    ]
    indexes: set[int] = set()
    excluded: list[dict[str, Any]] = []
    for row in capture_rows:
        try:
            indexes.add(_sample_index(row))
        except FeatureWindowError as exc:
            excluded.append({"sample_index": None, "reason": str(exc)})
    windows: list[dict[str, Any]] = []
    for index in sorted(indexes):
        try:
            windows.append(
                build_feature_window(
                    capture_rows, capture_id=capture_id, sample_index=index
                )
            )
        except FeatureWindowError as exc:
            excluded.append({"sample_index": index, "reason": str(exc)})
    return windows, excluded


def export_feature_window(
    csv_path: Path, capture_id: str, sample_index: int, output_path: Path
) -> dict[str, Any]:
    with Path(csv_path).open(encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))
    result = build_feature_window(
        rows, capture_id=capture_id, sample_index=sample_index
    )
    result["source_csv"] = str(Path(csv_path))
    result["generated_at"] = datetime.now(timezone.utc).isoformat()
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(result, indent=2, allow_nan=False), encoding="utf-8"
    )
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Export one SDACS_V3 feature window.")
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--capture-id", required=True)
    parser.add_argument("--sample-index", required=True, type=int)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args(argv)
    export_feature_window(
        arguments.csv,
        arguments.capture_id,
        arguments.sample_index,
        arguments.output,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
