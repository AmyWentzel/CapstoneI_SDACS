from __future__ import annotations

import csv
import json
import math
import os
from datetime import datetime, timezone
from pathlib import Path
from statistics import median
from typing import Any

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Patch, Rectangle, Wedge
from matplotlib.lines import Line2D

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


def acoustic_analysis(
    capture_id: str,
    telemetry: list[dict[str, Any]],
    directory: Path,
    layout_path: Path,
) -> dict[str, Any]:
    layout = json.loads(layout_path.read_text(encoding="utf-8"))
    feature_rows = [row for row in telemetry if row.get("record_type") == "features"]
    fields = [
        "capture_id", "node_id", "timestamp_iso", "normalized_x", "normalized_y",
        "x_position_m", "y_position_m", "z_position_m",
        "node_x_m", "node_y_m", "node_z_m",
        "speaker_x_m", "speaker_y_m", "speaker_z_m",
        "source_x_m", "source_y_m", "source_z_m", *MODEL_FEATURES,
    ]
    csv_rows = []
    for row in feature_rows:
        position = layout["nodes"].get(str(row.get("node_id")), {})
        source = layout["source"]
        csv_rows.append({
            "capture_id": capture_id,
            **row,
            "normalized_x": position.get("normalized_x"),
            "normalized_y": position.get("normalized_y"),
            "x_position_m": position.get("x_m"),
            "y_position_m": position.get("y_m"),
            "z_position_m": position.get("z_m"),
            "node_x_m": position.get("x_m"),
            "node_y_m": position.get("y_m"),
            "node_z_m": position.get("z_m"),
            "speaker_x_m": source.get("x_m"),
            "speaker_y_m": source.get("y_m"),
            "speaker_z_m": source.get("z_m"),
            "source_x_m": source.get("x_m"),
            "source_y_m": source.get("y_m"),
            "source_z_m": source.get("z_m"),
        })
    write_csv_atomic(directory / "acoustic_input.csv", csv_rows, fields)
    node_levels: dict[str, list[float]] = {}
    frequencies: list[float] = []
    bands = {"low": 0.0, "mid": 0.0, "high": 0.0}
    rows_by_node: dict[str, list[dict[str, Any]]] = {}
    for row in feature_rows:
        rows_by_node.setdefault(str(row.get("node_id")), []).append(row)
        level = row.get("db_spl")
        if (
            isinstance(level, (int, float))
            and not isinstance(level, bool)
            and math.isfinite(float(level))
        ):
            node_levels.setdefault(str(row["node_id"]), []).append(float(level))
        if (
            isinstance(row.get("f_peak_hz"), (int, float))
            and not isinstance(row.get("f_peak_hz"), bool)
            and math.isfinite(float(row["f_peak_hz"]))
            and float(row["f_peak_hz"]) > 0
        ):
            frequencies.append(float(row["f_peak_hz"]))
    means = {node: sum(values) / len(values) for node, values in node_levels.items() if values}
    node_metrics: dict[str, dict[str, Any]] = {}
    for node_id, rows in rows_by_node.items():
        def finite_values(field: str) -> list[float]:
            return [
                float(row[field]) for row in rows
                if isinstance(row.get(field), (int, float))
                and not isinstance(row.get(field), bool)
                and math.isfinite(float(row[field]))
            ]

        rms_values = finite_values("rms")
        dbfs_values = finite_values("dbfs")
        spl_values = finite_values("db_spl")
        peak_values = [value for value in finite_values("f_peak_hz") if value > 0]
        band_energies = {"low": 0.0, "mid": 0.0, "high": 0.0}
        usable_band_rows = 0
        for row in rows:
            energy = row.get("fft_total_energy")
            ratios = {
                "low": row.get("fft_low_ratio"),
                "mid": row.get("fft_mid_ratio"),
                "high": row.get("fft_high_ratio"),
            }
            if (
                not isinstance(energy, (int, float))
                or isinstance(energy, bool)
                or not math.isfinite(float(energy))
                or float(energy) <= 0
                or not all(
                    isinstance(value, (int, float))
                    and not isinstance(value, bool)
                    and math.isfinite(float(value))
                    and float(value) >= 0
                    for value in ratios.values()
                )
            ):
                continue
            for band, ratio in ratios.items():
                band_energies[band] += float(energy) * float(ratio)
            usable_band_rows += 1
        total_band_energy = sum(band_energies.values())
        band_ratios = (
            {
                band: energy / total_band_energy
                for band, energy in band_energies.items()
            }
            if usable_band_rows and total_band_energy > 0
            else {"low": None, "mid": None, "high": None}
        )
        if band_ratios["low"] is not None:
            for band in bands:
                bands[band] += band_energies[band]
        valid_rows = sum(
            1 for row in rows
            if any(
                isinstance(row.get(field), (int, float))
                and not isinstance(row.get(field), bool)
                and math.isfinite(float(row[field]))
                for field in ("rms", "dbfs", "db_spl", "f_peak_hz")
            )
        )
        if not valid_rows:
            continue
        position = layout["nodes"].get(node_id, {})
        warnings = []
        missing = [
            name for name, values in (
                ("rms", rms_values), ("dbfs", dbfs_values),
                ("db_spl", spl_values), ("f_peak_hz", peak_values),
            ) if not values
        ]
        if missing:
            warnings.append(f"No valid values for: {', '.join(missing)}.")
        if band_ratios["low"] is None:
            warnings.append("Band ratios unavailable for this node.")
        if any(value < 2000 for value in peak_values):
            warnings.append(
                "Low-frequency FFT peak estimates below approximately 2 kHz "
                "should be interpreted cautiously."
            )
        mean = lambda values: sum(values) / len(values) if values else None
        node_metrics[node_id] = {
            "node_id": node_id,
            "sample_count": valid_rows,
            "excluded_sample_count": len(rows) - valid_rows,
            "mean_rms": mean(rms_values),
            "mean_dbfs": mean(dbfs_values),
            "mean_estimated_spl_db": mean(spl_values),
            "peak_frequency_hz": median(peak_values) if peak_values else None,
            "representative_peak_frequency_hz": median(peak_values) if peak_values else None,
            "mean_fft_low_ratio": band_ratios["low"],
            "mean_fft_mid_ratio": band_ratios["mid"],
            "mean_fft_high_ratio": band_ratios["high"],
            "normalized_x": position.get("normalized_x"),
            "normalized_y": position.get("normalized_y"),
            "x_position_m": position.get("x_m"),
            "y_position_m": position.get("y_m"),
            "z_position_m": position.get("z_m"),
            "warnings": warnings,
        }
    loudest = max(means, key=means.get) if means else None
    quietest = min(means, key=means.get) if means else None
    summary = {
        "capture_id": capture_id,
        "status": "complete" if feature_rows else "failed",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "processor": "python_capture_acoustic_analysis",
        "layout_revision": layout["layout_revision"],
        "coordinate_system": layout["coordinate_system"],
        "units": layout["units"],
        "layout_snapshot_filename": "room_layout.json",
        "nodes_used": sorted(means),
        "missing_nodes": sorted(set(layout["nodes"]) - set(means)),
        "node_metrics": node_metrics,
        "mean_estimated_spl_db": sum(means.values()) / len(means) if means else None,
        "node_to_node_spl_range_db": max(means.values()) - min(means.values()) if means else None,
        "dominant_band": max(bands, key=bands.get) if any(bands.values()) else None,
        "dominant_frequency_hz": sum(frequencies) / len(frequencies) if frequencies else None,
        "loudest_node_id": loudest,
        "quietest_node_id": quietest,
        "spatial_variation_db": max(means.values()) - min(means.values()) if means else None,
        "plot_filename": "acoustic_map.png" if means else None,
        "plot_title": f"Spatial Acoustic Profile\n{capture_id}",
        "warnings": (
            (
                [] if len(means) == 4
                else ["Single-node summary only; this is not a reliable room field."] if len(means) == 1
                else ["Limited two-node comparison; no room field interpolation was performed."] if len(means) == 2
                else ["Partial three-node profile; no missing node values were invented."]
            )
            + (["Low-frequency FFT peak estimates below approximately 2 kHz should be interpreted cautiously."]
               if any(frequency < 2000 for frequency in frequencies) else [])
        ),
        "plot_coordinates": {
            "source": {
                "normalized_x": layout["source"].get("normalized_x"),
                "normalized_y": layout["source"].get("normalized_y"),
                "x_position_m": layout["source"].get("x_m"),
                "y_position_m": layout["source"].get("y_m"),
            },
            "nodes": {
                node_id: {
                    "normalized_x": metric["normalized_x"],
                    "normalized_y": metric["normalized_y"],
                    "x_position_m": metric["x_position_m"],
                    "y_position_m": metric["y_position_m"],
                }
                for node_id, metric in node_metrics.items()
            },
        },
    }
    if means:
        plt.style.use("dark_background")
        fig, axis = plt.subplots(figsize=(12, 7))
        fig.subplots_adjust(top=0.84, bottom=0.22, left=0.08, right=0.97)
        fig.patch.set_facecolor("#101018")
        axis.set_facecolor("#171722")
        physical = layout["units"] == "meters"
        x_key, y_key = ("x_m", "y_m") if physical else ("normalized_x", "normalized_y")
        room_width = layout.get("room_width_m") if physical else 1.0
        room_depth = layout.get("room_depth_m") if physical else 1.0
        room_width = float(room_width or 1.0)
        room_depth = float(room_depth or 1.0)
        axis.add_patch(
            Rectangle(
                (0, 0), room_width, room_depth,
                fill=False, edgecolor="#A78BFA", linewidth=2.0,
            )
        )
        glyph_radius = max(min(room_width, room_depth) * 0.055, 0.035)
        colors = {"low": "#3B82F6", "mid": "#22C55E", "high": "#EF4444"}
        for node_id, metric in sorted(node_metrics.items()):
            x = metric["x_position_m"] if physical else metric["normalized_x"]
            y = metric["y_position_m"] if physical else metric["normalized_y"]
            if x is None or y is None:
                continue
            ratios = {
                "low": metric["mean_fft_low_ratio"],
                "mid": metric["mean_fft_mid_ratio"],
                "high": metric["mean_fft_high_ratio"],
            }
            if all(value is not None for value in ratios.values()):
                start = 90.0
                for band in ("low", "mid", "high"):
                    sweep = 360.0 * float(ratios[band])
                    axis.add_patch(
                        Wedge(
                            (x, y), glyph_radius, start, start + sweep,
                            facecolor=colors[band], edgecolor="#E5E7EB", linewidth=0.7,
                            zorder=4,
                        )
                    )
                    start += sweep
                axis.add_patch(
                    Circle(
                        (x, y), glyph_radius * 0.36,
                        facecolor="#18181B", edgecolor="#D4D4D8",
                        linewidth=0.6, zorder=5,
                    )
                )
            else:
                axis.add_patch(
                    Circle(
                        (x, y), glyph_radius,
                        facecolor="#52525B", edgecolor="#E5E7EB", zorder=4,
                    )
                )
            spl = metric["mean_estimated_spl_db"]
            if spl is not None:
                # Fixed 20-80 dB display scale; never normalized per capture.
                spl_fraction = min(max((float(spl) - 20.0) / 60.0, 0.0), 1.0)
                axis.add_patch(
                    Circle(
                        (x, y), glyph_radius * (1.18 + 0.22 * spl_fraction),
                        fill=False, edgecolor="#FBBF24",
                        linewidth=1.5 + 3.0 * spl_fraction, alpha=0.9,
                        zorder=3,
                    )
                )
            frequency = metric["representative_peak_frequency_hz"]
            frequency_text = f"{frequency:.0f} Hz" if frequency is not None else "frequency unavailable"
            spl_text = f"{spl:.1f} dB est." if spl is not None else "Estimated SPL unavailable"
            normalized_x = float(metric["normalized_x"] or 0.5)
            normalized_y = float(metric["normalized_y"] or 0.5)
            horizontal_offset = 14 if normalized_x < 0.5 else -14
            vertical_offset = 18 if normalized_y < 0.5 else -18
            axis.annotate(
                f"{node_id}\n{spl_text}\n{frequency_text}",
                (x, y), textcoords="offset points",
                xytext=(horizontal_offset, vertical_offset),
                ha="left" if normalized_x < 0.5 else "right",
                va="bottom" if normalized_y < 0.5 else "top",
                fontsize=9, color="#F4F4F5",
                bbox={"boxstyle": "round,pad=0.25", "facecolor": "#09090B", "alpha": 0.78, "edgecolor": "none"},
                zorder=7,
            )
        source = layout["source"]
        axis.scatter(
            source[x_key], source[y_key], marker="*", s=360,
            color="#C084FC", edgecolors="#FFFFFF", linewidths=1.0, zorder=6,
        )
        axis.annotate(
            "Test Source\nBroker", (source[x_key], source[y_key]),
            textcoords="offset points", xytext=(12, -24), color="#E9D5FF",
            fontweight="bold",
        )
        axis.invert_yaxis()
        axis.set_xlim(-0.04 * room_width, 1.04 * room_width)
        axis.set_ylim(1.04 * room_depth, -0.04 * room_depth)
        axis.set_aspect("equal", adjustable="box")
        axis.set_xlabel("X (m)" if layout["units"] == "meters" else "Normalized X")
        axis.set_ylabel("Y (m)" if layout["units"] == "meters" else "Normalized Y")
        dimensions = (
            f"{room_width:.2f} m × {room_depth:.2f} m"
            if physical else "Normalized room coordinates"
        )
        axis.set_title(
            f"{summary['plot_title']} · {dimensions}",
            fontsize=15, pad=16,
        )
        axis.text(
            0.01, 0.01, "Origin: upper-left; +Y points downward",
            transform=axis.transAxes, fontsize=8, color="#A1A1AA",
            ha="left", va="bottom",
        )
        axis.legend(
            handles=[
                Patch(color=colors["low"], label="Blue wedge — Low-frequency energy"),
                Patch(color=colors["mid"], label="Green wedge — Mid-frequency energy"),
                Patch(color=colors["high"], label="Red wedge — High-frequency energy"),
                Patch(facecolor="none", edgecolor="#FBBF24", label="Outer halo — Estimated SPL (fixed 20–80 dB scale)"),
                Line2D(
                    [0], [0], marker="*", color="none",
                    markerfacecolor="#C084FC", markeredgecolor="#FFFFFF",
                    markersize=12, label="Purple star — Test Source / Broker",
                ),
            ],
            loc="upper center", bbox_to_anchor=(0.5, -0.11), ncol=3, frameon=False,
        )
        fig.savefig(
            directory / "acoustic_map.png", dpi=150,
            bbox_inches="tight", pad_inches=0.15, facecolor=fig.get_facecolor(),
        )
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
