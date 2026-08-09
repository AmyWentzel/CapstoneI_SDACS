"""SDACS backend module: Room-level acoustic analysis and rendering utilities that summarize four-node captures into spatial metrics and visual maps.

The module is part of the Raspberry Pi middleware/API layer used by the final SDACS system.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


# Stable IDs are written to CSV/API payloads. display_name is shown in Flutter.
LABEL_CATALOG: tuple[dict[str, Any], ...] = (
    {
        "id": "noisy",
        "display_name": "Noisy",
        "description": "General high-noise room condition.",
        "aliases": ("Noisy", "noise", "noisy_room"),
    },
    {
        "id": "speech",
        "display_name": "Speech",
        "description": "Spoken voice in the monitored room.",
        "aliases": ("Speech", "voice", "spoken_voice"),
    },
    {
        "id": "low",
        "display_name": "low (40-500Hz)",
        "description": "Project-defined low-frequency condition from 40 Hz to 500 Hz.",
        "aliases": (
            "low (40-500Hz)",
            "low (40–500 Hz)",
            "low_40_500hz",
            "test_tone_100hz",
            "test_tone_500hz",
        ),
    },
    {
        "id": "mid",
        "display_name": "mid (400-4kHz)",
        "description": "Project-defined mid-frequency condition from 400 Hz to 4 kHz.",
        "aliases": (
            "mid (400-4kHz)",
            "mid (400 Hz–4 kHz)",
            "mid_400_4khz",
            "test_tone_2khz",
        ),
    },
    {
        "id": "high",
        "display_name": "high (4k-20kHz)",
        "description": "Project-defined high-frequency condition from 4 kHz to 20 kHz.",
        "aliases": (
            "high (4k-20kHz)",
            "high (4 kHz–20 kHz)",
            "high_4k_20khz",
            "test_tone_5khz",
            "test_tone_15khz",
        ),
    },
    {
        "id": "quiet_room_white_noise",
        "display_name": "Quiet Room (white noise)",
        "description": "Quiet-room baseline capture using white noise.",
        "aliases": (
            "Quiet Room (white noise)",
            "quiet room",
            "quiet_room",
            "white_noise",
            "quiet_white_noise",
        ),
    },
)

REQUIRED_SUMMARY_COLUMNS = {
    "run_id",
    "node_id",
    "label",
    "rows",
    "mean_band_bass_ratio",
    "mean_band_low_mid_ratio",
    "mean_band_mid_ratio",
    "mean_band_presence_ratio",
    "mean_band_high_ratio",
}
REQUIRED_RAW_COLUMNS = {
    "node_id",
    "label",
    "band_bass_ratio",
    "band_low_mid_ratio",
    "band_mid_ratio",
    "band_presence_ratio",
    "band_high_ratio",
}
REQUIRED_POSITION_COLUMNS = {"node_id", "x_m", "y_m", "height_m"}


@dataclass(frozen=True)
class RoomDimensions:
    width: float = 6.0
    depth: float = 5.0
    height: float = 3.5


def _normalization_key(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", value.strip().lower())


def _label_alias_map() -> dict[str, str]:
    aliases: dict[str, str] = {}
    for item in LABEL_CATALOG:
        label_id = str(item["id"])
        candidates: Iterable[str] = (label_id, str(item["display_name"]), *item["aliases"])
        for candidate in candidates:
            aliases[_normalization_key(candidate)] = label_id
    return aliases


_LABEL_ALIASES = _label_alias_map()
_LABELS_BY_ID = {str(item["id"]): item for item in LABEL_CATALOG}


def canonical_label_id(value: str, *, allow_latest: bool = True) -> str:
    text = str(value).strip()
    if allow_latest and text.lower() == "latest":
        return "latest"
    label_id = _LABEL_ALIASES.get(_normalization_key(text))
    if label_id is None:
        allowed = ", ".join(str(item["display_name"]) for item in LABEL_CATALOG)
        raise ValueError(f"Unknown SDACS label {value!r}. Allowed labels: {allowed}.")
    return label_id


def label_display_name(label_id: str) -> str:
    item = _LABELS_BY_ID.get(label_id)
    return str(item["display_name"]) if item else label_id


def get_label_catalog(available_ids: Iterable[str] = ()) -> list[dict[str, Any]]:
    available = set(available_ids)
    return [
        {
            "id": str(item["id"]),
            "display_name": str(item["display_name"]),
            "description": str(item["description"]),
            "has_data": str(item["id"]) in available,
        }
        for item in LABEL_CATALOG
    ]


def _require_columns(table: pd.DataFrame, required: set[str], source: Path) -> None:
    missing = sorted(required.difference(table.columns))
    if missing:
        raise ValueError(f"{source} is missing required columns: {', '.join(missing)}")


def _numeric(series: pd.Series) -> pd.Series:
    return pd.to_numeric(series, errors="coerce")


def _prepare_labels(table: pd.DataFrame) -> pd.DataFrame:
    prepared = table.copy()
    try:
        prepared["_label_id"] = prepared["label"].map(lambda value: canonical_label_id(str(value), allow_latest=False))
    except ValueError as error:
        raise ValueError(f"The source CSV contains an unsupported label. {error}") from error
    return prepared


def available_labels_from_table(table: pd.DataFrame) -> list[str]:
    prepared = _prepare_labels(table)
    present = set(prepared["_label_id"].dropna().astype(str))
    return [str(item["id"]) for item in LABEL_CATALOG if str(item["id"]) in present]


def available_labels(summary_csv: Path) -> list[str]:
    summary = pd.read_csv(summary_csv)
    _require_columns(summary, {"label"}, summary_csv)
    return available_labels_from_table(summary)


def summarize_raw_capture(raw_csv: Path) -> pd.DataFrame:
    """Convert a finalized Node-RED feature CSV into one row per run/node/label."""
    raw_csv = Path(raw_csv)
    raw = pd.read_csv(raw_csv)
    _require_columns(raw, REQUIRED_RAW_COLUMNS, raw_csv)

    if "run_id" not in raw.columns:
        if "request_id" in raw.columns:
            raw = raw.rename(columns={"request_id": "run_id"})
        else:
            raise ValueError(f"{raw_csv} must contain either run_id or request_id.")

    raw = raw.dropna(subset=["run_id", "node_id", "label"]).copy()
    if raw.empty:
        raise ValueError(f"{raw_csv} contains no usable capture rows.")

    ratio_columns = [
        "band_bass_ratio",
        "band_low_mid_ratio",
        "band_mid_ratio",
        "band_presence_ratio",
        "band_high_ratio",
    ]
    for column in ratio_columns:
        raw[column] = _numeric(raw[column])

    group_columns = ["run_id", "node_id", "label"]
    grouped = raw.groupby(group_columns, dropna=False)
    summary = grouped.size().rename("rows").reset_index()

    for column in ratio_columns:
        values = grouped[column].mean().rename(f"mean_{column}").reset_index()
        summary = summary.merge(values, on=group_columns, how="left", validate="one_to_one")

    if "sample_rate_ok" in raw.columns:
        ok = raw["sample_rate_ok"].astype(str).str.lower().isin({"true", "1", "yes", "ok"}).astype(int)
        raw = raw.assign(_sample_rate_ok=ok)
        good = raw.groupby(group_columns)["_sample_rate_ok"].sum().rename("good_rows").reset_index()
        summary = summary.merge(good, on=group_columns, how="left", validate="one_to_one")
    else:
        summary["good_rows"] = summary["rows"]

    timestamp_column = next(
        (column for column in ("timestamp_iso", "timestamp", "time_iso") if column in raw.columns),
        None,
    )
    if timestamp_column:
        times = raw.groupby(group_columns)[timestamp_column].agg(["min", "max"]).reset_index()
        times = times.rename(columns={"min": "first_timestamp", "max": "last_timestamp"})
        summary = summary.merge(times, on=group_columns, how="left", validate="one_to_one")

    for optional in ("target_freq_hz", "z_value", "suggested_z_metric"):
        if optional in raw.columns:
            values = raw.groupby(group_columns)[optional].first().rename(optional).reset_index()
            summary = summary.merge(values, on=group_columns, how="left", validate="one_to_one")

    if "capture_complete_status" in raw.columns:
        status = raw.groupby(group_columns)["capture_complete_status"].first().reset_index()
        summary = summary.merge(status, on=group_columns, how="left", validate="one_to_one")
    else:
        # Only point SDACS_ACOUSTIC_RAW_CSV at a finalized/atomically renamed file.
        summary["capture_complete_status"] = "complete"

    return summary


def _choose_run(summary: pd.DataFrame, label: str | None) -> tuple[str, str]:
    working = _prepare_labels(summary)
    if "capture_complete_status" in working.columns:
        complete = working[working["capture_complete_status"].astype(str).str.lower() == "complete"]
        if not complete.empty:
            working = complete

    requested_id = canonical_label_id(label or "latest")
    if requested_id != "latest":
        working = working[working["_label_id"] == requested_id]
        if working.empty:
            raise ValueError(
                f"No completed rows were found for {label_display_name(requested_id)!r} ({requested_id})."
            )

    if working.empty:
        raise ValueError("No completed capture rows are available.")

    # Prefer the run with the largest node count, then the latest timestamp/run ID.
    run_stats = working.groupby(["run_id", "_label_id"], dropna=False).agg(
        node_count=("node_id", "nunique"),
        first_timestamp=("first_timestamp", "max") if "first_timestamp" in working.columns else ("run_id", "max"),
    )
    run_stats = run_stats.reset_index()
    max_nodes = int(run_stats["node_count"].max())
    run_stats = run_stats[run_stats["node_count"] == max_nodes]

    if "first_timestamp" in working.columns:
        timestamps = pd.to_datetime(run_stats["first_timestamp"], errors="coerce", utc=True)
        run_stats = run_stats.assign(_sort_time=timestamps)
        run_stats = run_stats.sort_values(["_sort_time", "run_id"], na_position="first")
    else:
        run_stats = run_stats.sort_values("run_id")

    selected = run_stats.iloc[-1]
    return str(selected["run_id"]), str(selected["_label_id"])


def _build_acoustic_map_from_tables(
    summary: pd.DataFrame,
    positions: pd.DataFrame,
    *,
    source: dict[str, str],
    label: str | None,
    room: RoomDimensions,
) -> dict[str, Any]:
    prepared = _prepare_labels(summary)
    run_id, selected_label = _choose_run(prepared, label)
    selected = prepared[
        (prepared["run_id"].astype(str) == run_id) & (prepared["_label_id"] == selected_label)
    ].copy()
    if selected.empty:
        raise ValueError("The selected run contains no data.")

    numeric_columns = [
        "rows",
        "good_rows",
        "z_value",
        "mean_band_bass_ratio",
        "mean_band_low_mid_ratio",
        "mean_band_mid_ratio",
        "mean_band_presence_ratio",
        "mean_band_high_ratio",
    ]
    for column in numeric_columns:
        if column in selected.columns:
            selected[column] = _numeric(selected[column])

    agg_spec: dict[str, str] = {
        "rows": "mean",
        "mean_band_bass_ratio": "mean",
        "mean_band_low_mid_ratio": "mean",
        "mean_band_mid_ratio": "mean",
        "mean_band_presence_ratio": "mean",
        "mean_band_high_ratio": "mean",
    }
    for optional in [
        "good_rows",
        "z_value",
        "dominant_band_mode",
        "suggested_z_metric",
        "target_freq_hz",
        "capture_complete_status",
        "first_timestamp",
        "last_timestamp",
    ]:
        if optional in selected.columns:
            agg_spec[optional] = "mean" if optional in {"good_rows", "z_value", "target_freq_hz"} else "first"

    selected = selected.groupby("node_id", as_index=False).agg(agg_spec)
    positions = positions.copy()
    for column in ["x_m", "y_m", "height_m"]:
        positions[column] = _numeric(positions[column])

    merged = positions.merge(selected, on="node_id", how="inner", validate="one_to_one")
    if merged.empty:
        raise ValueError("No node IDs match between the positions and capture files.")

    merged["low_raw"] = merged["mean_band_bass_ratio"] + merged["mean_band_low_mid_ratio"]
    merged["mid_raw"] = merged["mean_band_mid_ratio"] + merged["mean_band_presence_ratio"]
    merged["high_raw"] = merged["mean_band_high_ratio"]
    total = merged[["low_raw", "mid_raw", "high_raw"]].sum(axis=1).replace(0, np.nan)
    merged["low_ratio"] = (merged["low_raw"] / total).fillna(0.0)
    merged["mid_ratio"] = (merged["mid_raw"] / total).fillna(0.0)
    merged["high_ratio"] = (merged["high_raw"] / total).fillna(0.0)

    if "z_value" in merged.columns:
        merged["intensity"] = merged["z_value"].fillna(0.0).clip(lower=0.0)
    else:
        merged["intensity"] = merged[["low_ratio", "mid_ratio", "high_ratio"]].max(axis=1)

    max_intensity = float(merged["intensity"].max())
    if max_intensity > 1.0:
        merged["intensity"] = merged["intensity"] / max_intensity

    nodes: list[dict[str, Any]] = []
    for row in merged.sort_values("node_id").to_dict(orient="records"):
        low = float(row["low_ratio"])
        mid = float(row["mid_ratio"])
        high = float(row["high_ratio"])
        dominant = max(("low", low), ("mid", mid), ("high", high), key=lambda item: item[1])[0]
        node = {
            "node_id": str(row["node_id"]),
            "x_m": float(row["x_m"]),
            "y_m": float(row["y_m"]),
            "height_m": float(row["height_m"]),
            "low_ratio": low,
            "mid_ratio": mid,
            "high_ratio": high,
            "intensity": float(row["intensity"]),
            "dominant_band": dominant,
            "rows": int(round(float(row.get("rows", 0) or 0))),
            "good_rows": int(round(float(row.get("good_rows", row.get("rows", 0)) or 0))),
        }
        for key in [
            "z_value",
            "suggested_z_metric",
            "target_freq_hz",
            "capture_complete_status",
            "first_timestamp",
            "last_timestamp",
        ]:
            value = row.get(key)
            if pd.notna(value):
                if isinstance(value, np.generic):
                    value = value.item()
                node[key] = value
        nodes.append(node)

    available_ids = available_labels_from_table(summary)
    return {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "source": source,
        "run_id": run_id,
        "selected_label": selected_label,
        "selected_label_display": label_display_name(selected_label),
        "available_labels": available_ids,
        "label_catalog": get_label_catalog(available_ids),
        "room": {
            "width_m": room.width,
            "depth_m": room.depth,
            "height_m": room.height,
        },
        "nodes": nodes,
        "notes": [
            "This is a four-point node-local visualization, not a dense acoustic field measurement.",
            "SPL is not used because it is not calibrated; frequency-band ratios drive the visualization.",
            "RGB mapping is high=red, mid=green, low=blue.",
            "CSV/API labels use stable IDs while Flutter displays the project label wording.",
        ],
    }


def build_acoustic_map(
    summary_csv: Path,
    positions_csv: Path,
    label: str | None = "latest",
    room: RoomDimensions = RoomDimensions(),
) -> dict[str, Any]:
    summary_csv = Path(summary_csv)
    positions_csv = Path(positions_csv)
    summary = pd.read_csv(summary_csv)
    positions = pd.read_csv(positions_csv)
    _require_columns(summary, REQUIRED_SUMMARY_COLUMNS, summary_csv)
    _require_columns(positions, REQUIRED_POSITION_COLUMNS, positions_csv)
    return _build_acoustic_map_from_tables(
        summary,
        positions,
        source={"summary_csv": str(summary_csv), "positions_csv": str(positions_csv)},
        label=label,
        room=room,
    )


def build_acoustic_map_from_raw(
    raw_csv: Path,
    positions_csv: Path,
    label: str | None = "latest",
    room: RoomDimensions = RoomDimensions(),
) -> dict[str, Any]:
    raw_csv = Path(raw_csv)
    positions_csv = Path(positions_csv)
    summary = summarize_raw_capture(raw_csv)
    positions = pd.read_csv(positions_csv)
    _require_columns(positions, REQUIRED_POSITION_COLUMNS, positions_csv)
    return _build_acoustic_map_from_tables(
        summary,
        positions,
        source={"raw_csv": str(raw_csv), "positions_csv": str(positions_csv)},
        label=label,
        room=room,
    )


def render_acoustic_map(data: dict[str, Any], output_png: Path) -> Path:
    output_png = Path(output_png)
    output_png.parent.mkdir(parents=True, exist_ok=True)

    room = data["room"]
    fig = plt.figure(figsize=(12, 8), facecolor="white")
    ax = fig.add_subplot(111, projection="3d")

    width = float(room["width_m"])
    depth = float(room["depth_m"])
    height = float(room["height_m"])

    for x in np.arange(0, width + 0.001, 1.0):
        ax.plot([x, x], [0, depth], [0, 0], linewidth=0.5, alpha=0.35)
    for y in np.arange(0, depth + 0.001, 1.0):
        ax.plot([0, width], [y, y], [0, 0], linewidth=0.5, alpha=0.35)

    corners = [
        (0, 0, 0), (width, 0, 0), (width, depth, 0), (0, depth, 0),
        (0, 0, height), (width, 0, height), (width, depth, height), (0, depth, height),
    ]
    edges = [
        (0, 1), (1, 2), (2, 3), (3, 0),
        (4, 5), (5, 6), (6, 7), (7, 4),
        (0, 4), (1, 5), (2, 6), (3, 7),
    ]
    for a, b in edges:
        xa, ya, za = corners[a]
        xb, yb, zb = corners[b]
        ax.plot([xa, xb], [ya, yb], [za, zb], linewidth=0.7, alpha=0.25)

    half_size = 0.55
    grid = np.linspace(-half_size, half_size, 55)
    xg, yg = np.meshgrid(grid, grid)
    gaussian = np.exp(-((xg / 0.30) ** 2 + (yg / 0.30) ** 2) / 2.0)

    for node in data["nodes"]:
        x = float(node["x_m"])
        y = float(node["y_m"])
        z = float(node["height_m"])
        intensity = max(0.05, float(node["intensity"]))
        low = float(node["low_ratio"])
        mid = float(node["mid_ratio"])
        high = float(node["high_ratio"])

        cloud_height = 0.25 + 0.9 * intensity
        zg = z + gaussian * cloud_height
        rgb = np.array([high, mid, low], dtype=float)
        rgb = np.clip(0.15 + 0.85 * rgb, 0.0, 1.0)
        facecolors = np.empty((*zg.shape, 4), dtype=float)
        facecolors[..., :3] = rgb
        facecolors[..., 3] = 0.20 + 0.75 * gaussian

        ax.plot_surface(
            xg + x,
            yg + y,
            zg,
            facecolors=facecolors,
            linewidth=0,
            antialiased=True,
            shade=True,
        )
        ax.scatter([x], [y], [z], s=34)
        ax.text(
            x,
            y,
            z + cloud_height + 0.12,
            f"{node['node_id']}\nH {high:.0%}  M {mid:.0%}  L {low:.0%}",
            ha="center",
            va="bottom",
            fontsize=8,
        )

    ax.set_xlim(0, width)
    ax.set_ylim(0, depth)
    ax.set_zlim(0, height + 1.0)
    ax.set_xlabel("Room X (m)")
    ax.set_ylabel("Room Y (m)")
    ax.set_zlabel("Height (m)")
    title_label = data.get("selected_label_display", data["selected_label"])
    ax.set_title(f"SDACS Acoustic Frequency Ratios — {title_label}")
    ax.view_init(elev=25, azim=-55)
    ax.set_box_aspect((width, depth, height + 1.0))
    fig.tight_layout()
    fig.savefig(output_png, dpi=160, bbox_inches="tight")
    plt.close(fig)
    return output_png


def write_json(data: dict[str, Any], output_json: Path) -> Path:
    output_json = Path(output_json)
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(data, indent=2, default=str), encoding="utf-8")
    return output_json


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate SDACS acoustic-map JSON and PNG output.")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--summary", type=Path)
    source.add_argument("--raw", type=Path, help="Finalized Node-RED raw feature CSV")
    parser.add_argument("--positions", required=True, type=Path)
    parser.add_argument("--label", default="latest")
    parser.add_argument("--json", dest="output_json", required=True, type=Path)
    parser.add_argument("--png", dest="output_png", required=True, type=Path)
    args = parser.parse_args()

    if args.raw:
        data = build_acoustic_map_from_raw(args.raw, args.positions, args.label)
    else:
        data = build_acoustic_map(args.summary, args.positions, args.label)
    write_json(data, args.output_json)
    render_acoustic_map(data, args.output_png)
    print(f"Selected {data['run_id']} / {data['selected_label_display']} ({data['selected_label']})")
    print(f"JSON: {args.output_json}")
    print(f"PNG:  {args.output_png}")


if __name__ == "__main__":
    main()
