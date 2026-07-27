import asyncio
import json
from datetime import datetime, timezone
from typing import Any

from fastapi import APIRouter, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse

from .config import Settings
from .capture_service import CaptureService
from .ble_scanner import BleScanError, scan_sdacs_nodes
from .models import BleScanResponse, CaptureStartRequest, CaptureStartResponse, CommandRequest, PublishResult
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
