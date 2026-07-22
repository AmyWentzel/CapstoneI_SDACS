from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from threading import RLock
from typing import Any


class CaptureStore:
    """Capture-session tables added safely to the existing telemetry SQLite DB."""

    def __init__(self, database_path: Path) -> None:
        self.database_path = Path(database_path)
        self._lock = RLock()

    def initialize(self) -> None:
        self.database_path.parent.mkdir(parents=True, exist_ok=True)
        with self._connect() as db:
            db.executescript(
                """
                CREATE TABLE IF NOT EXISTS capture_sessions (
                    capture_id TEXT PRIMARY KEY,
                    status TEXT NOT NULL,
                    requested_at TEXT NOT NULL,
                    scheduled_start_at TEXT NOT NULL,
                    record_seconds INTEGER NOT NULL,
                    validation_label TEXT,
                    expected_nodes_json TEXT NOT NULL,
                    completed_nodes_json TEXT NOT NULL,
                    missing_nodes_json TEXT NOT NULL,
                    processing_stage TEXT,
                    artifacts_json TEXT NOT NULL,
                    error_stage TEXT,
                    error_message TEXT,
                    model_status TEXT,
                    model_version TEXT,
                    updated_at TEXT NOT NULL
                );
                CREATE TABLE IF NOT EXISTS capture_telemetry (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    capture_id TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    record_type TEXT NOT NULL,
                    timestamp_iso TEXT,
                    payload_json TEXT NOT NULL
                );
                CREATE INDEX IF NOT EXISTS idx_capture_telemetry_capture
                    ON capture_telemetry(capture_id, node_id, record_type);
                """
            )

    def upsert_session(self, session: dict[str, Any]) -> None:
        values = (
            session["capture_id"], session["status"], session["requested_at"],
            session["scheduled_start_at"], session["record_seconds"], session.get("validation_label"),
            json.dumps(session["expected_nodes"]), json.dumps(session["completed_nodes"]),
            json.dumps(session["missing_nodes"]), session.get("processing_stage"),
            json.dumps(session.get("artifacts", {})), session.get("error_stage"),
            session.get("error_message"), session.get("model_status"), session.get("model_version"),
            session["updated_at"],
        )
        with self._lock, self._connect() as db:
            db.execute(
                """INSERT INTO capture_sessions(
                capture_id,status,requested_at,scheduled_start_at,record_seconds,validation_label,
                expected_nodes_json,completed_nodes_json,missing_nodes_json,processing_stage,
                artifacts_json,error_stage,error_message,model_status,model_version,updated_at
                ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
                ON CONFLICT(capture_id) DO UPDATE SET
                status=excluded.status, completed_nodes_json=excluded.completed_nodes_json,
                missing_nodes_json=excluded.missing_nodes_json, processing_stage=excluded.processing_stage,
                artifacts_json=excluded.artifacts_json, error_stage=excluded.error_stage,
                error_message=excluded.error_message, model_status=excluded.model_status,
                model_version=excluded.model_version, updated_at=excluded.updated_at""",
                values,
            )

    def get_session(self, capture_id: str) -> dict[str, Any] | None:
        with self._lock, self._connect() as db:
            row = db.execute("SELECT * FROM capture_sessions WHERE capture_id=?", (capture_id,)).fetchone()
        return self._session_from_row(row) if row else None

    def list_sessions(self) -> list[dict[str, Any]]:
        with self._lock, self._connect() as db:
            rows = db.execute("SELECT * FROM capture_sessions ORDER BY requested_at DESC").fetchall()
        return [self._session_from_row(row) for row in rows]

    def add_telemetry(self, capture_id: str, payload: dict[str, Any]) -> None:
        with self._lock, self._connect() as db:
            db.execute(
                "INSERT INTO capture_telemetry(capture_id,node_id,record_type,timestamp_iso,payload_json) VALUES(?,?,?,?,?)",
                (capture_id, payload["node_id"], payload["record_type"], payload.get("timestamp_iso"), json.dumps(payload)),
            )

    def telemetry(self, capture_id: str) -> list[dict[str, Any]]:
        with self._lock, self._connect() as db:
            rows = db.execute(
                "SELECT payload_json FROM capture_telemetry WHERE capture_id=? ORDER BY id", (capture_id,)
            ).fetchall()
        return [json.loads(row[0]) for row in rows]

    def _connect(self) -> sqlite3.Connection:
        db = sqlite3.connect(self.database_path, timeout=10)
        db.row_factory = sqlite3.Row
        return db

    @staticmethod
    def _session_from_row(row: sqlite3.Row) -> dict[str, Any]:
        result = dict(row)
        for source, target in (
            ("expected_nodes_json", "expected_nodes"), ("completed_nodes_json", "completed_nodes"),
            ("missing_nodes_json", "missing_nodes"), ("artifacts_json", "artifacts"),
        ):
            result[target] = json.loads(result.pop(source))
        return result
