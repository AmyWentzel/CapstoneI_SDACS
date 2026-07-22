import asyncio
from datetime import datetime, timezone
from typing import Any

from fastapi import APIRouter, HTTPException, WebSocket, WebSocketDisconnect

from .config import Settings
from .ble_scanner import BleScanError, scan_sdacs_nodes
from .models import BleScanResponse, CaptureStartRequest, CommandRequest, PublishResult
from .mqtt_client import SdacsMqttClient
from .state_store import StateStore
from .websocket_manager import WebSocketManager


def build_router(
    settings: Settings,
    state_store: StateStore,
    mqtt_client: SdacsMqttClient,
    websocket_manager: WebSocketManager,
) -> APIRouter:
    router = APIRouter()
    ble_scan_lock = asyncio.Lock()

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
        return [capture.model_dump(mode="json") for capture in state_store.list_captures()]

    @router.post("/api/capture/start")
    async def start_capture(request: CaptureStartRequest) -> PublishResult:
        request_id = request.request_id or f"capture_{datetime.now().strftime('%Y%m%d%H%M%S')}"
        payload = {
            "cmd": "start_capture",
            "request_id": request_id,
            "delay_ms": request.delay_ms,
            "record_seconds": request.record_seconds,
        }
        if request.label is not None:
            payload["label"] = request.label
        published = mqtt_client.publish_json(settings.command_topic_all, payload)
        return PublishResult(topic=settings.command_topic_all, payload=payload, published=published)

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

            await asyncio.sleep(1.0)
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
