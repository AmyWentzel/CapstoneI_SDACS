"""SDACS backend module: FastAPI endpoints that expose acoustic-map labels, JSON results, and rendered images generated from SDACS capture data.

The module is part of the Raspberry Pi middleware/API layer used by the final SDACS system.
"""

from __future__ import annotations

import os
from pathlib import Path
from threading import Lock

from fastapi import APIRouter, HTTPException, Query
from fastapi.responses import FileResponse

try:
    # Works after the files are copied into backend/sdacs_api/app/.
    from .sdacs_acoustic_map import (
        build_acoustic_map,
        build_acoustic_map_from_raw,
        get_label_catalog,
        render_acoustic_map,
    )
except ImportError:
    # Keeps the reference module runnable outside the package.
    from sdacs_acoustic_map import (
        build_acoustic_map,
        build_acoustic_map_from_raw,
        get_label_catalog,
        render_acoustic_map,
    )

router = APIRouter(prefix="/api/acoustic-map", tags=["acoustic-map"])
_render_lock = Lock()


def _paths() -> tuple[Path | None, Path, Path, Path]:
    raw_value = os.getenv("SDACS_ACOUSTIC_RAW_CSV", "").strip()
    raw = Path(raw_value) if raw_value else None
    summary = Path(os.getenv(
        "SDACS_ACOUSTIC_SUMMARY_CSV",
        "/home/vortex/sdacs_ai_data/amy_matlab_roommap_summary_best_runs.csv",
    ))
    positions = Path(os.getenv(
        "SDACS_NODE_POSITIONS_CSV",
        "/home/vortex/sdacs_ai_data/amy_node_positions_template.csv",
    ))
    output_dir = Path(os.getenv(
        "SDACS_ACOUSTIC_OUTPUT_DIR",
        "/home/vortex/sdacs_api/generated",
    ))
    return raw, summary, positions, output_dir


def _load(label: str) -> dict:
    raw, summary, positions, _ = _paths()
    if not positions.exists():
        raise HTTPException(status_code=503, detail=f"Node positions CSV not found: {positions}")

    try:
        if raw is not None:
            if not raw.exists():
                raise HTTPException(status_code=503, detail=f"Finalized Node-RED CSV not found: {raw}")
            return build_acoustic_map_from_raw(raw, positions, label)
        if not summary.exists():
            raise HTTPException(status_code=503, detail=f"Acoustic summary CSV not found: {summary}")
        return build_acoustic_map(summary, positions, label)
    except HTTPException:
        raise
    except ValueError as error:
        raise HTTPException(status_code=422, detail=str(error)) from error


@router.get("/labels")
def get_acoustic_map_labels() -> dict:
    """Return the fixed provisional label catalog used by Flutter and Node-RED."""
    return {"labels": get_label_catalog()}


@router.get("")
def get_acoustic_map(
    label: str = Query(
        default="latest",
        description="latest or one of: noisy, speech, low, mid, high, quiet_room_white_noise",
    ),
) -> dict:
    return _load(label)


@router.get("/image", response_class=FileResponse)
def get_acoustic_map_image(label: str = Query(default="latest")) -> FileResponse:
    data = _load(label)
    _, _, _, output_dir = _paths()
    output_dir.mkdir(parents=True, exist_ok=True)
    safe_label = "".join(
        character if character.isalnum() or character in "-_" else "_"
        for character in data["selected_label"]
    )
    image_path = output_dir / f"acoustic_map_{safe_label}.png"
    with _render_lock:
        render_acoustic_map(data, image_path)
    return FileResponse(
        image_path,
        media_type="image/png",
        filename=image_path.name,
        headers={"Cache-Control": "no-store"},
    )
