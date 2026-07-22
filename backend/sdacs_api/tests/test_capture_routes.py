from fastapi import FastAPI
from fastapi.testclient import TestClient

from app.capture_service import CaptureService
from app.config import Settings
from app.routes import build_router


class FakeMqtt:
    connected = True

    def __init__(self):
        self.messages = []

    def publish_json(self, topic, payload):
        self.messages.append((topic, payload))
        return True


def client(tmp_path):
    settings = Settings(
        capture_data_dir=tmp_path / "captures",
        sqlite_path=tmp_path / "telemetry.db",
        ei_enabled=False,
    )
    service = CaptureService(settings)
    service.store.initialize()
    mqtt = FakeMqtt()
    app = FastAPI()
    app.include_router(build_router(settings, object(), mqtt, object(), service))
    return TestClient(app), mqtt


def test_normal_capture_is_unlabelled_and_preserves_id(tmp_path):
    api, mqtt = client(tmp_path)
    response = api.post("/api/capture/start", json={
        "request_id": "capture_route_test", "record_seconds": 60, "delay_ms": 5000,
    })
    assert response.status_code == 200
    assert response.json()["capture_id"] == "capture_route_test"
    topic, payload = mqtt.messages[0]
    assert topic == "sdacs/group/all/cmd"
    assert payload["request_id"] == "capture_route_test"
    assert payload["record_seconds"] == 60
    assert "label" not in payload
    assert "validation_label" not in payload


def test_unknown_capture_returns_404_and_ai_health_is_exposed(tmp_path):
    api, _ = client(tmp_path)
    assert api.get("/api/captures/capture_missing").status_code == 404
    health = api.get("/api/ai/health")
    assert health.status_code == 200
    assert health.json()["is_simulated"] is False


def test_invalid_capture_id_cannot_traverse(tmp_path):
    api, _ = client(tmp_path)
    response = api.post("/api/capture/start", json={"request_id": "capture_../../secret", "record_seconds": 60})
    assert response.status_code == 400
