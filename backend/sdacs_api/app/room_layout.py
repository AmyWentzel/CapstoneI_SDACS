"""SDACS backend module: Validated room geometry models and SQLite-backed storage for node/source positions used by acoustic visualization.

The module is part of the Raspberry Pi middleware/API layer used by the final SDACS system.
"""

from __future__ import annotations

import json
import math
import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from threading import RLock
from typing import Any

from pydantic import BaseModel, ConfigDict, Field, model_validator


NODE_IDS = ("node01", "node02", "node03", "node04")


class SpatialPosition(BaseModel):
    model_config = ConfigDict(extra="forbid")
    normalized_x: float = Field(ge=0.0, le=1.0)
    normalized_y: float = Field(ge=0.0, le=1.0)
    z_m: float = Field(default=1.2, ge=0.0, le=20.0)

    @model_validator(mode="after")
    def finite(self) -> "SpatialPosition":
        if not all(math.isfinite(value) for value in (self.normalized_x, self.normalized_y, self.z_m)):
            raise ValueError("position values must be finite")
        return self


class NodeLayoutPosition(BaseModel):
    model_config = ConfigDict(extra="forbid")
    node_id: str
    position: SpatialPosition


class RoomLayout(BaseModel):
    """Origin is room upper-left; +x points right and +y points down."""

    model_config = ConfigDict(extra="forbid")
    layout_id: str = "primary"
    room_width_m: float | None = Field(default=None, gt=0.0, le=1000.0)
    room_depth_m: float | None = Field(default=None, gt=0.0, le=1000.0)
    source_position: SpatialPosition = SpatialPosition(normalized_x=0.5, normalized_y=0.5)
    node_positions: list[NodeLayoutPosition]
    updated_at: str | None = None
    revision: int = Field(default=0, ge=0)

    @model_validator(mode="after")
    def validate_layout(self) -> "RoomLayout":
        for value in (self.room_width_m, self.room_depth_m):
            if value is not None and not math.isfinite(value):
                raise ValueError("room dimensions must be finite")
        ids = [node.node_id for node in self.node_positions]
        if len(ids) != len(set(ids)):
            raise ValueError("node IDs must be unique")
        if set(ids) != set(NODE_IDS):
            raise ValueError("layout must contain exactly node01 through node04")
        points = [(node.position.normalized_x, node.position.normalized_y) for node in self.node_positions]
        if len(points) != len(set(points)):
            raise ValueError("node positions must not be duplicates")
        return self


def default_layout() -> RoomLayout:
    return RoomLayout(
        node_positions=[
            NodeLayoutPosition(node_id="node01", position=SpatialPosition(normalized_x=0.16, normalized_y=0.18)),
            NodeLayoutPosition(node_id="node02", position=SpatialPosition(normalized_x=0.84, normalized_y=0.18)),
            NodeLayoutPosition(node_id="node03", position=SpatialPosition(normalized_x=0.16, normalized_y=0.82)),
            NodeLayoutPosition(node_id="node04", position=SpatialPosition(normalized_x=0.84, normalized_y=0.82)),
        ]
    )


class RoomLayoutStore:
    def __init__(self, database_path: Path) -> None:
        self.database_path = Path(database_path)
        self._lock = RLock()

    def initialize(self) -> None:
        self.database_path.parent.mkdir(parents=True, exist_ok=True)
        with self._connect() as db:
            db.execute(
                """CREATE TABLE IF NOT EXISTS room_layouts (
                    layout_id TEXT PRIMARY KEY,
                    revision INTEGER NOT NULL,
                    updated_at TEXT NOT NULL,
                    layout_json TEXT NOT NULL
                )"""
            )

    def get(self) -> RoomLayout:
        self.initialize()
        with self._connect() as db:
            row = db.execute(
                "SELECT layout_json FROM room_layouts WHERE layout_id = ?", ("primary",)
            ).fetchone()
        return RoomLayout.model_validate_json(row[0]) if row else default_layout()

    def save(self, requested: RoomLayout) -> RoomLayout:
        self.initialize()
        with self._lock, self._connect() as db:
            row = db.execute(
                "SELECT revision FROM room_layouts WHERE layout_id = ?", ("primary",)
            ).fetchone()
            current_revision = int(row[0]) if row else 0
            if requested.revision != current_revision:
                raise ValueError(f"layout revision conflict; current revision is {current_revision}")
            saved = requested.model_copy(
                update={
                    "layout_id": "primary",
                    "revision": current_revision + 1,
                    "updated_at": datetime.now(timezone.utc).isoformat(),
                }
            )
            db.execute(
                """INSERT INTO room_layouts(layout_id, revision, updated_at, layout_json)
                   VALUES(?, ?, ?, ?)
                   ON CONFLICT(layout_id) DO UPDATE SET
                     revision=excluded.revision,
                     updated_at=excluded.updated_at,
                     layout_json=excluded.layout_json""",
                ("primary", saved.revision, saved.updated_at, saved.model_dump_json()),
            )
        return saved

    @staticmethod
    def snapshot(layout: RoomLayout, capture_id: str) -> dict[str, Any]:
        width, depth = layout.room_width_m, layout.room_depth_m
        source = _snapshot_position(layout.source_position, width, depth)
        return {
            "capture_id": capture_id,
            "layout_revision": layout.revision,
            "room_width_m": width,
            "room_depth_m": depth,
            "coordinate_system": "room_upper_left_x_right_y_down",
            "units": "meters" if width is not None and depth is not None else "normalized",
            "source": source,
            "nodes": {
                node.node_id: _snapshot_position(node.position, width, depth)
                for node in layout.node_positions
            },
        }

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.database_path)
        connection.execute("PRAGMA busy_timeout = 5000")
        return connection


def _snapshot_position(position: SpatialPosition, width: float | None, depth: float | None) -> dict[str, float | None]:
    return {
        "normalized_x": position.normalized_x,
        "normalized_y": position.normalized_y,
        "x_m": position.normalized_x * width if width is not None else None,
        "y_m": position.normalized_y * depth if depth is not None else None,
        "z_m": position.z_m,
    }
