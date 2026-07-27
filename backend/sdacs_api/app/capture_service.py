from __future__ import annotations

import asyncio
import re
from datetime import datetime, timedelta, timezone
from pathlib import Path
from threading import RLock
from typing import Any

from .capture_processing import acoustic_analysis, atomic_json, build_model_input, fuse
from .capture_store import CaptureStore
from .config import Settings
from .edge_impulse import ConfiguredEdgeImpulseRunner, EdgeImpulseRunner
from .models import TelemetryUpdate
from .room_layout import RoomLayout, RoomLayoutStore


CAPTURE_ID_PATTERN = re.compile(r"^capture_[A-Za-z0-9_-]{1,80}$")
TERMINAL_STATUSES = {"complete", "partial", "failed", "acoustic_only"}


def validate_capture_id(capture_id: str) -> str:
    if not CAPTURE_ID_PATTERN.fullmatch(capture_id):
        raise ValueError("Invalid capture ID")
    return capture_id


class CaptureService:
    def __init__(self, settings: Settings, runner: EdgeImpulseRunner | None = None) -> None:
        self.settings = settings
        self.store = CaptureStore(settings.sqlite_path)
        self.runner = runner or ConfiguredEdgeImpulseRunner(settings)
        self.layouts = RoomLayoutStore(settings.sqlite_path)
        self._loop: asyncio.AbstractEventLoop | None = None
        self._tasks: dict[str, asyncio.Task[None]] = {}
        self._processing: set[str] = set()
        self._lock = RLock()

    def start(self, loop: asyncio.AbstractEventLoop) -> None:
        self._loop = loop
        self.store.initialize()
        self.layouts.initialize()
        for session in self.store.list_sessions():
            if session["status"] not in TERMINAL_STATUSES:
                session["status"] = "failed"
                session["error_stage"] = "backend_restart"
                session["error_message"] = "Processing was interrupted by a backend restart."
                session["updated_at"] = self._now()
                self._save(session)

    def stop(self) -> None:
        for task in self._tasks.values():
            task.cancel()
        self._tasks.clear()

    def create(self, capture_id: str, record_seconds: int, delay_ms: int, validation_label: str | None) -> dict[str, Any]:
        validate_capture_id(capture_id)
        if self.store.get_session(capture_id):
            raise ValueError("Capture ID already exists")
        now = datetime.now(timezone.utc)
        session = {
            "capture_id": capture_id, "validation_label": validation_label,
            "requested_at": now.isoformat(),
            "scheduled_start_at": (now + timedelta(milliseconds=delay_ms)).isoformat(),
            "record_seconds": record_seconds, "expected_nodes": list(self.settings.expected_nodes),
            "completed_nodes": [], "missing_nodes": [], "status": "requested",
            "processing_stage": "waiting_for_nodes", "artifacts": {},
            "error_stage": None, "error_message": None, "model_status": None,
            "model_version": None, "updated_at": now.isoformat(),
        }
        self._save(session)
        directory = self.directory(capture_id)
        directory.mkdir(parents=True, exist_ok=False)
        layout_snapshot = self.layouts.snapshot(self.layouts.get(), capture_id)
        atomic_json(directory / "room_layout.json", layout_snapshot)
        session["artifacts"]["room_layout"] = "room_layout.json"
        self._save(session)
        self._write_manifest(session)
        timeout = delay_ms / 1000 + record_seconds + self.settings.capture_completion_grace_seconds
        if self._loop:
            self._tasks[capture_id] = self._loop.create_task(self._wait_then_process(capture_id, timeout))
        return session

    def mark_publish_failed(self, capture_id: str) -> None:
        session = self.require(capture_id)
        session.update(status="failed", error_stage="mqtt_publish", error_message="MQTT capture command publish failed", updated_at=self._now())
        self._save(session)

    def observe(self, telemetry: TelemetryUpdate) -> None:
        capture_id = telemetry.request_id
        if not capture_id or not CAPTURE_ID_PATTERN.fullmatch(capture_id):
            return
        session = self.store.get_session(capture_id)
        if not session:
            return
        payload = telemetry.model_dump(mode="json")
        self.store.add_telemetry(capture_id, payload)
        reported_state = telemetry.capture_state or telemetry.raw_json.get("state")
        if str(reported_state).lower() in {"armed", "capturing"}:
            session["status"] = "capturing"
            session["processing_stage"] = "capturing"
            session["updated_at"] = self._now()
            self._save(session)
        if telemetry.record_type != "capture_complete" or telemetry.node_id not in session["expected_nodes"]:
            return
        completed = set(session["completed_nodes"])
        completed.add(telemetry.node_id)
        session["completed_nodes"] = sorted(completed)
        session["status"] = "capture_complete" if completed == set(session["expected_nodes"]) else "waiting_for_nodes"
        session["updated_at"] = self._now()
        self._save(session)
        if completed == set(session["expected_nodes"]) and self._loop:
            self._loop.call_soon_threadsafe(self._schedule_processing, capture_id)

    def require(self, capture_id: str) -> dict[str, Any]:
        validate_capture_id(capture_id)
        session = self.store.get_session(capture_id)
        if not session:
            raise KeyError(capture_id)
        return session

    def list(self) -> list[dict[str, Any]]:
        return self.store.list_sessions()

    def directory(self, capture_id: str) -> Path:
        validate_capture_id(capture_id)
        root = self.settings.capture_data_dir.resolve()
        directory = (root / capture_id).resolve()
        if root != directory.parent:
            raise ValueError("Invalid capture directory")
        return directory

    def artifact(self, capture_id: str, filename: str) -> Path:
        allowed = {"acoustic_summary.json", "acoustic_map.png", "edge_impulse_result.json", "combined_result.json"}
        if filename not in allowed:
            raise ValueError("Invalid artifact")
        return self.directory(capture_id) / filename

    def health(self) -> dict[str, Any]:
        return self.runner.health()

    def get_layout(self) -> RoomLayout:
        return self.layouts.get()

    def save_layout(self, layout: RoomLayout) -> RoomLayout:
        return self.layouts.save(layout)

    async def _wait_then_process(self, capture_id: str, timeout: float) -> None:
        await asyncio.sleep(timeout)
        self._schedule_processing(capture_id)

    def _schedule_processing(self, capture_id: str) -> None:
        with self._lock:
            if capture_id in self._processing:
                return
            session = self.store.get_session(capture_id)
            if not session or session["status"] in TERMINAL_STATUSES:
                return
            self._processing.add(capture_id)
        task = self._tasks.pop(capture_id, None)
        if task and task is not asyncio.current_task():
            task.cancel()
        if self._loop:
            self._loop.create_task(self._process(capture_id))

    async def _process(self, capture_id: str) -> None:
        try:
            await asyncio.to_thread(self._process_sync, capture_id)
        except Exception as exc:
            session = self.require(capture_id)
            session.update(
                status="failed", error_stage=session.get("processing_stage") or "processing",
                error_message=str(exc), updated_at=self._now(),
            )
            self._save(session)
        finally:
            with self._lock:
                self._processing.discard(capture_id)

    def _process_sync(self, capture_id: str) -> None:
        session = self.require(capture_id)
        session["missing_nodes"] = sorted(set(session["expected_nodes"]) - set(session["completed_nodes"]))
        session.update(status="processing", processing_stage="processing_acoustic_analysis", updated_at=self._now())
        self._save(session)
        directory = self.directory(capture_id)
        telemetry = self.store.telemetry(capture_id)
        acoustic = acoustic_analysis(capture_id, telemetry, directory, directory / "room_layout.json")
        session["artifacts"].update(acoustic_input="acoustic_input.csv", acoustic_summary="acoustic_summary.json")
        if (directory / "acoustic_map.png").is_file():
            session["artifacts"]["plot"] = "acoustic_map.png"
        session.update(processing_stage="processing_edge_impulse", updated_at=self._now())
        self._save(session)
        input_meta = build_model_input(capture_id, telemetry, directory / "edge_impulse_input.csv")
        atomic_json(directory / "edge_impulse_schema.json", input_meta)
        edge = self.runner.infer(capture_id, directory / "edge_impulse_input.csv")
        atomic_json(directory / "edge_impulse_result.json", edge)
        combined = fuse(session, acoustic, edge)
        atomic_json(directory / "combined_result.json", combined)
        (directory / "processor_stdout.log").touch(exist_ok=True)
        (directory / "processor_stderr.log").touch(exist_ok=True)
        session["artifacts"].update(edge_impulse_input="edge_impulse_input.csv", edge_impulse_result="edge_impulse_result.json", combined_result="combined_result.json")
        session["model_status"] = edge["status"]
        session["model_version"] = edge.get("model_version")
        session["processing_stage"] = "complete"
        session["status"] = (
            "failed" if combined["status"] == "failed"
            else "partial" if session["missing_nodes"]
            else combined["status"]
        )
        session["updated_at"] = self._now()
        self._save(session)

    def _save(self, session: dict[str, Any]) -> None:
        self.store.upsert_session(session)
        if self.directory(session["capture_id"]).exists():
            self._write_manifest(session)

    def _write_manifest(self, session: dict[str, Any]) -> None:
        atomic_json(self.directory(session["capture_id"]) / "manifest.json", session)

    @staticmethod
    def _now() -> str:
        return datetime.now(timezone.utc).isoformat()
