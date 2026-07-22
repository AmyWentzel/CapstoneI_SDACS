from __future__ import annotations

import csv
import json
import math
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import matplotlib.pyplot as plt

from .edge_impulse import APPROVED_CLASSES, EdgeImpulseRunner


MODEL_FEATURES = (
    "rms", "dbfs", "db_spl", "f_peak_hz", "fft_low_ratio",
    "fft_mid_ratio", "fft_high_ratio", "fft_total_energy",
)


def atomic_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, default=str), encoding="utf-8")
    os.replace(temporary, path)


def write_csv_atomic(path: Path, rows: list[dict[str, Any]], fields: list[str]) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    os.replace(temporary, path)


def build_model_input(capture_id: str, telemetry: list[dict[str, Any]], output: Path) -> dict[str, Any]:
    rows: list[dict[str, float]] = []
    for item in telemetry:
        if item.get("record_type") != "features":
            continue
        row: dict[str, float] = {}
        for name in MODEL_FEATURES:
            value = item.get(name)
            if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(float(value)):
                row[name] = float(value)
            else:
                row[name] = 0.0
        rows.append(row)
    write_csv_atomic(output, rows, list(MODEL_FEATURES))
    return {"capture_id": capture_id, "schema_version": "preliminary-v1", "features": list(MODEL_FEATURES), "rows": len(rows)}


def acoustic_analysis(capture_id: str, telemetry: list[dict[str, Any]], directory: Path) -> dict[str, Any]:
    feature_rows = [row for row in telemetry if row.get("record_type") == "features"]
    fields = ["capture_id", "node_id", "timestamp_iso", *MODEL_FEATURES]
    csv_rows = [{"capture_id": capture_id, **row} for row in feature_rows]
    write_csv_atomic(directory / "acoustic_input.csv", csv_rows, fields)
    node_levels: dict[str, list[float]] = {}
    frequencies: list[float] = []
    bands = {"low": 0.0, "mid": 0.0, "high": 0.0}
    for row in feature_rows:
        level = row.get("db_spl")
        if isinstance(level, (int, float)):
            node_levels.setdefault(str(row["node_id"]), []).append(float(level))
        if isinstance(row.get("f_peak_hz"), (int, float)):
            frequencies.append(float(row["f_peak_hz"]))
        for band, key in (("low", "fft_low_ratio"), ("mid", "fft_mid_ratio"), ("high", "fft_high_ratio")):
            if isinstance(row.get(key), (int, float)):
                bands[band] += float(row[key])
    means = {node: sum(values) / len(values) for node, values in node_levels.items() if values}
    loudest = max(means, key=means.get) if means else None
    quietest = min(means, key=means.get) if means else None
    summary = {
        "capture_id": capture_id,
        "status": "complete" if feature_rows else "failed",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "processor": "python_capture_acoustic_analysis",
        "nodes_used": sorted(means),
        "mean_estimated_spl_db": sum(means.values()) / len(means) if means else None,
        "node_to_node_spl_range_db": max(means.values()) - min(means.values()) if means else None,
        "dominant_band": max(bands, key=bands.get) if any(bands.values()) else None,
        "dominant_frequency_hz": sum(frequencies) / len(frequencies) if frequencies else None,
        "loudest_node_id": loudest,
        "quietest_node_id": quietest,
        "spatial_variation_db": max(means.values()) - min(means.values()) if means else None,
        "plot_filename": "acoustic_map.png" if means else None,
        "warnings": [] if len(means) == 4 else ["Reduced spatial reliability: fewer than four nodes supplied acoustic data."],
    }
    if means:
        fig, axis = plt.subplots(figsize=(7, 4))
        axis.bar(list(means), list(means.values()))
        axis.set_ylabel("Mean estimated SPL (dB)")
        axis.set_title(f"SDACS capture {capture_id}")
        fig.tight_layout()
        fig.savefig(directory / "acoustic_map.png", dpi=150)
        plt.close(fig)
    atomic_json(directory / "acoustic_summary.json", summary)
    return summary


def fuse(capture: dict[str, Any], acoustic: dict[str, Any], edge: dict[str, Any]) -> dict[str, Any]:
    model_complete = edge.get("status") == "complete"
    if model_complete and edge.get("predicted_label") not in APPROVED_CLASSES:
        raise ValueError(f"Unexpected model output class: {edge.get('predicted_label')}")
    evidence: list[dict[str, str]] = []
    if model_complete:
        evidence.append({"source": "edge_impulse", "statement": f"The model predicted {edge['predicted_label']} with {edge['confidence']:.0%} confidence."})
    if acoustic.get("loudest_node_id"):
        evidence.append({"source": "acoustic_analysis", "statement": f"The strongest measured level occurred at {acoustic['loudest_node_id']}."})
    if acoustic.get("status") != "complete":
        status = "failed"
    else:
        status = "complete" if model_complete else "acoustic_only"
    return {
        "capture_id": capture["capture_id"], "status": status,
        "completed_nodes": capture["completed_nodes"], "missing_nodes": capture["missing_nodes"],
        "acoustic_analysis": acoustic, "edge_impulse": edge,
        "fusion": {"agreement": "neutral" if model_complete else "unavailable", "recommendation_confidence": "uncertain" if model_complete else "unavailable", "supporting_evidence": evidence, "conflicting_evidence": [], "method_version": "rules-v1-provisional"},
        "recommendation": {"summary": "Acoustic analysis is complete. The final Edge Impulse model is not currently configured." if not model_complete else f"The model classified this capture as {edge['predicted_label']}; review the attributed acoustic evidence below.", "evidence": evidence, "warnings": acoustic.get("warnings", []) + edge.get("warnings", [])},
    }
