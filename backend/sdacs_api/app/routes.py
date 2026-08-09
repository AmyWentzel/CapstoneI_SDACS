"""SDACS backend module: Primary REST and WebSocket API surface for health, telemetry, captures, calibration, BLE discovery, commands, plots, and AI status.

The module is part of the Raspberry Pi middleware/API layer used by the final SDACS system.
"""

import asyncio
import json
from datetime import datetime, timezone
from typing import Any

from fastapi import APIRouter, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse

from .config import Settings
from .capture_service import CaptureService
from .ble_scanner import BleScanError, scan_sdacs_nodes
from .models import (
    BleScanResponse,
    CalibrationApplyRequest,
    CalibrationApplyResponse,
    CalibrationPreviewRequest,
    CalibrationPreviewResponse,
    CaptureStartRequest,
    CaptureStartResponse,
    CommandRequest,
    PublishResult,
)
from .mqtt_client import SdacsMqttClient
from .state_store import StateStore
from .room_layout import RoomLayout
from .websocket_manager import WebSocketManager

_sleep = asyncio.sleep

def build_router(
    settings: Settings,
    state_store: StateStore,
    mqtt_client: SdacsMqttClient,
    websocket_manager: WebSocketManager,
    capture_service: CaptureService,
) -> APIRouter:
    router = APIRouter()
    ble_scan_lock = asyncio.Lock()

    @router.get("/health")
    @router.get("/api/health")
    async def health() -> dict[str, Any]:
        return {
            "status": "ok",
            "mqtt_connected": mqtt_client.connected,
            "mqtt_host": settings.mqtt_host,
            "mqtt_port": settings.mqtt_port,
        }

    @router.get("/api/nodes")
    async def list_nodes() -> list[dict[str, Any]]:
        return [node.model_dump(mode="json") for node in state_store.list_nodes()]

    @router.get("/api/telemetry/latest")
    async def latest_telemetry() -> list[dict[str, Any]]:
        return [node.model_dump(mode="json") for node in state_store.list_nodes()]

    @router.get("/api/nodes/{node_id}")
    async def get_node(node_id: str) -> dict[str, Any]:
        node = state_store.get_node(node_id)
        if node is None:
            raise HTTPException(status_code=404, detail="Node not found")
        return node.model_dump(mode="json")

    @router.get("/api/nodes/{node_id}/features/latest")
    async def get_latest_features(node_id: str) -> dict[str, Any]:
        node = state_store.get_node(node_id)
        if node is None:
            raise HTTPException(status_code=404, detail="Node not found")
        telemetry = node.latest_features or node.latest
        if telemetry is None:
            raise HTTPException(status_code=404, detail="No telemetry found")
        return telemetry.model_dump(mode="json")

    @router.get("/api/captures")
    async def list_captures() -> list[dict[str, Any]]:
        return capture_service.list()

    @router.get("/api/layout", response_model=RoomLayout)
    async def get_layout() -> RoomLayout:
        return capture_service.get_layout()

    @router.put("/api/layout", response_model=RoomLayout)
    async def put_layout(layout: RoomLayout) -> RoomLayout:
        try:
            return capture_service.save_layout(layout)
        except ValueError as exc:
            raise HTTPException(status_code=409, detail=str(exc)) from exc

    @router.post("/api/test/start", response_model=CaptureStartResponse)
    @router.post("/api/capture/start", response_model=CaptureStartResponse)
    async def start_capture(request: CaptureStartRequest) -> CaptureStartResponse:
        now = datetime.now(timezone.utc)
        request_id = request.request_id or f"capture_{now.strftime('%Y%m%dT%H%M%S%fZ')}"
        validation_label = request.validation_label
        if validation_label is None and request.label in {"noisy", "speech"}:
            validation_label = request.label
        try:
            session = capture_service.create(request_id, request.record_seconds, request.delay_ms, validation_label)
        except ValueError as exc:
            raise HTTPException(status_code=409 if "exists" in str(exc) else 400, detail=str(exc)) from exc
        payload = {
            "cmd": "start_capture",
            "request_id": request_id,
            "delay_ms": request.delay_ms,
            "record_seconds": request.record_seconds,
        }
        if request.label is not None:  # Explicit labelled dataset/validation workflow only.
            payload["label"] = request.label
        published = mqtt_client.publish_json(settings.command_topic_all, payload)
        if not published:
            capture_service.mark_publish_failed(request_id)
            raise HTTPException(status_code=503, detail="MQTT capture command publish failed")
        return CaptureStartResponse(
            capture_id=request_id, status="requested", requested_at=session["requested_at"],
            scheduled_start_at=session["scheduled_start_at"], record_seconds=request.record_seconds,
        )

    def require_capture(capture_id: str) -> dict[str, Any]:
        try:
            return capture_service.require(capture_id)
        except KeyError as exc:
            raise HTTPException(status_code=404, detail="Capture not found") from exc
        except ValueError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc

    def json_artifact(capture_id: str, filename: str) -> JSONResponse:
        require_capture(capture_id)
        try:
            path = capture_service.artifact(capture_id, filename)
        except ValueError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        if not path.is_file():
            return JSONResponse(status_code=202, content={"capture_id": capture_id, "status": "pending"})
        return JSONResponse(content=json.loads(path.read_text(encoding="utf-8")))

    @router.get("/api/captures/{capture_id}")
    async def get_capture(capture_id: str) -> dict[str, Any]:
        return require_capture(capture_id)

    @router.get("/api/captures/{capture_id}/result")
    async def get_capture_result(capture_id: str) -> JSONResponse:
        return json_artifact(capture_id, "combined_result.json")

    @router.get("/api/captures/{capture_id}/acoustic")
    @router.get("/api/captures/{capture_id}/matlab")
    async def get_capture_acoustic(capture_id: str) -> JSONResponse:
        return json_artifact(capture_id, "acoustic_summary.json")

    @router.get("/api/captures/{capture_id}/edge-impulse")
    async def get_capture_edge_impulse(capture_id: str) -> JSONResponse:
        return json_artifact(capture_id, "edge_impulse_result.json")

    @router.get("/api/captures/{capture_id}/calibration")
    async def get_capture_calibration(capture_id: str) -> JSONResponse:
        return json_artifact(capture_id, "calibration_result.json")

    @router.post("/api/calibration/preview", response_model=CalibrationPreviewResponse)
    async def preview_calibration(request: CalibrationPreviewRequest) -> dict[str, Any]:
        try:
            return capture_service.calibration_preview(
                request.capture_id, request.reference_spl_db
            )
        except KeyError as exc:
            raise HTTPException(status_code=404, detail="Capture not found") from exc
        except ValueError as exc:
            raise HTTPException(status_code=409, detail=str(exc)) from exc

    @router.get("/api/calibration/latest")
    async def latest_calibration() -> dict[str, Any]:
        result = capture_service.latest_calibration_result()
        if result is None:
            raise HTTPException(status_code=404, detail="No applied calibration is available")
        return result

    @router.post("/api/calibration/apply", response_model=CalibrationApplyResponse)
    async def apply_calibration(request: CalibrationApplyRequest) -> dict[str, Any]:
        try:
            preview = capture_service.calibration_preview(
                request.capture_id, request.reference_spl_db
            )
        except KeyError as exc:
            raise HTTPException(status_code=404, detail="Capture not found") from exc
        except ValueError as exc:
            raise HTTPException(status_code=409, detail=str(exc)) from exc

        expected = list(settings.expected_nodes)
        by_node = {row["node_id"]: row for row in preview["nodes"]}
        if not request.allow_partial and preview["status"] != "ready":
            raise HTTPException(
                status_code=409,
                detail="All four nodes require valid 1 kHz calibration data before offsets can be applied.",
            )

        requested_offsets = request.node_offsets_db or {
            node_id: row["suggested_offset_db"]
            for node_id, row in by_node.items()
            if row["eligible"] and row["suggested_offset_db"] is not None
        }
        unknown = sorted(set(requested_offsets) - set(expected))
        if unknown:
            raise HTTPException(
                status_code=400, detail=f"Unknown calibration node IDs: {', '.join(unknown)}"
            )
        if not request.allow_partial and set(requested_offsets) != set(expected):
            raise HTTPException(
                status_code=400,
                detail="Offsets for node01 through node04 are required.",
            )
        ineligible = sorted(
            node_id
            for node_id in requested_offsets
            if not by_node.get(node_id, {}).get("eligible", False)
        )
        if ineligible:
            raise HTTPException(
                status_code=409,
                detail=(
                    "Offsets cannot be applied to nodes that failed calibration checks: "
                    + ", ".join(ineligible)
                ),
            )
        changed = []
        for node_id, offset in requested_offsets.items():
            suggested = by_node[node_id].get("suggested_offset_db")
            if suggested is None or abs(float(offset) - float(suggested)) > 0.10:
                changed.append(node_id)
        if changed:
            raise HTTPException(
                status_code=400,
                detail=(
                    "Requested offsets must match the backend calibration preview: "
                    + ", ".join(sorted(changed))
                ),
            )

        pending: dict[str, dict[str, Any]] = {}
        started = datetime.now(timezone.utc)
        for node_id in expected:
            if node_id not in requested_offsets:
                continue
            offset = float(requested_offsets[node_id])
            if not 60.0 <= offset <= 180.0:
                raise HTTPException(
                    status_code=400,
                    detail=f"Offset for {node_id} must be from 60 to 180 dB.",
                )
            request_id = f"cal_{started.strftime('%H%M%S%f')}_{node_id}"
            topic = f"sdacs/node/{node_id}/cmd"
            payload = {
                "cmd": "set_cal_offset",
                "request_id": request_id,
                "cal_offset_db": round(offset, 3),
            }
            published = mqtt_client.publish_json(topic, payload)
            pending[node_id] = {
                "node_id": node_id,
                "requested_offset_db": offset,
                "request_id": request_id,
                "published": published,
                "acknowledged": False,
                "applied": False,
                "reported_offset_db": None,
                "reason": None if published else "mqtt_publish_failed",
            }

        deadline = asyncio.get_running_loop().time() + request.acknowledgement_timeout_seconds
        while asyncio.get_running_loop().time() < deadline:
            waiting = False
            for row in pending.values():
                if not row["published"] or row["acknowledged"]:
                    continue
                response = state_store.get_command_response(row["request_id"])
                if response is None:
                    waiting = True
                    continue
                row["acknowledged"] = True
                result = response.result or response.raw_json.get("result")
                reason = response.reason or response.raw_json.get("reason")
                reported = response.cal_offset_db
                if reported is None:
                    value = response.raw_json.get("cal_offset_db")
                    if isinstance(value, (int, float)) and not isinstance(value, bool):
                        reported = float(value)
                row["reported_offset_db"] = reported
                matches_requested = (
                    reported is not None
                    and abs(float(reported) - float(row["requested_offset_db"])) <= 0.05
                )
                row["applied"] = result == "ok" and matches_requested
                if result != "ok":
                    row["reason"] = reason or "rejected"
                elif not matches_requested:
                    row["reason"] = "reported_offset_mismatch"
                else:
                    row["reason"] = reason
            if not waiting:
                break
            await _sleep(0.1)

        for row in pending.values():
            if row["published"] and not row["acknowledged"]:
                row["reason"] = "acknowledgement_timeout"

        applied_count = sum(1 for row in pending.values() if row["applied"])
        if applied_count == len(pending) and len(pending) == len(expected):
            status = "complete"
        elif applied_count:
            status = "partial"
        else:
            status = "failed"
        warnings = []
        if status != "complete":
            warnings.append(
                f"Calibration was confirmed on {applied_count} of {len(pending)} requested nodes."
            )
        result = {
            "capture_id": request.capture_id,
            "reference_spl_db": request.reference_spl_db,
            "status": status,
            "applied_at": datetime.now(timezone.utc).isoformat(),
            "nodes": list(pending.values()),
            "warnings": warnings,
            "preview": preview,
        }
        capture_service.record_calibration_result(request.capture_id, result)
        return result

    @router.get("/api/captures/{capture_id}/plot", response_class=FileResponse)
    async def get_capture_plot(capture_id: str) -> FileResponse:
        require_capture(capture_id)
        try:
            path = capture_service.artifact(capture_id, "acoustic_map.png")
        except ValueError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        if not path.is_file():
            raise HTTPException(status_code=404, detail="Capture plot is not available")
        return FileResponse(path, media_type="image/png", headers={"Cache-Control": "no-store"})

    @router.get("/api/ai/health")
    async def ai_health() -> dict[str, Any]:
        return capture_service.health()

    @router.post("/api/ble/scan", response_model=BleScanResponse)
    async def scan_ble() -> BleScanResponse:
        if ble_scan_lock.locked():
            raise HTTPException(status_code=409, detail="A BLE scan is already running")

        async with ble_scan_lock:
            started = datetime.now(timezone.utc)
            request_id = f"ble_scan_{started.strftime('%Y%m%dT%H%M%S%fZ')}"
            payload = {
                "cmd": "ble_advertise",
                "request_id": request_id,
                "duration_ms": 15000,
            }
            if not mqtt_client.publish_json(settings.command_topic_all, payload):
                raise HTTPException(status_code=503, detail="MQTT BLE command publish failed")

            await _sleep(1.0)
            try:
                nodes = await asyncio.wait_for(scan_sdacs_nodes(8.0), timeout=10.0)
            except asyncio.TimeoutError as exc:
                raise HTTPException(status_code=504, detail="BLE scan timed out") from exc
            except BleScanError as exc:
                raise HTTPException(status_code=503, detail=str(exc)) from exc

            completed = datetime.now(timezone.utc)
            return BleScanResponse(
                request_id=request_id,
                status="complete",
                started_at=started.isoformat(),
                completed_at=completed.isoformat(),
                scan_duration_seconds=8.0,
                detected_count=len(nodes),
                nodes=nodes,
            )

    @router.post("/api/nodes/{node_id}/command")
    async def node_command(node_id: str, request: CommandRequest) -> PublishResult:
        topic = f"sdacs/node/{node_id}/cmd"
        payload: dict[str, Any] = {"cmd": request.cmd}
        if request.request_id:
            payload["request_id"] = request.request_id
        published = mqtt_client.publish_json(topic, payload)
        return PublishResult(topic=topic, payload=payload, published=published)

    @router.websocket("/ws/sdacs/live")
    async def websocket_live(websocket: WebSocket) -> None:
        await websocket_manager.connect(websocket)
        try:
            while True:
                await websocket.receive_text()
        except WebSocketDisconnect:
            await websocket_manager.disconnect(websocket)

    return router
