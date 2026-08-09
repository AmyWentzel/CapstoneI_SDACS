"""SDACS verification: regression tests for capture routes.

These tests document expected final-system behavior and protect the production merge from regressions.
"""

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


def test_calibration_capture_metadata_is_preserved_for_ai_skip(tmp_path):
    api, _ = client(tmp_path)
    response = api.post("/api/capture/start", json={
        "request_id": "capture_calibration",
        "record_seconds": 60,
        "validation_label": "calibration_1khz",
    })
    assert response.status_code == 200
    session = api.get("/api/captures/capture_calibration").json()
    assert session["validation_label"] == "calibration_1khz"


def test_layout_routes_round_trip_all_nodes_and_source(tmp_path):
    api, _ = client(tmp_path)
    initial = api.get("/api/layout")
    assert initial.status_code == 200
    payload = initial.json()
    assert {row["node_id"] for row in payload["node_positions"]} == {
        "node01", "node02", "node03", "node04",
    }
    assert payload["source_position"]["normalized_x"] == 0.5

    payload["source_position"]["normalized_x"] = 0.61
    saved = api.put("/api/layout", json=payload)
    assert saved.status_code == 200
    assert saved.json()["revision"] == 1
    reloaded = api.get("/api/layout")
    assert reloaded.status_code == 200
    assert reloaded.json()["source_position"]["normalized_x"] == 0.61
    assert reloaded.json()["revision"] == 1


def test_invalid_layout_returns_422_not_404(tmp_path):
    api, _ = client(tmp_path)
    payload = api.get("/api/layout").json()
    payload["node_positions"][0]["position"]["normalized_x"] = 1.5
    response = api.put("/api/layout", json=payload)
    assert response.status_code == 422


def test_production_app_registers_layout_and_existing_route_families():
    from app.main import app

    paths = set(app.openapi()["paths"])
    assert {"/api/layout", "/api/ble/scan", "/api/capture/start",
            "/api/ai/health", "/api/telemetry/latest"}.issubset(paths)
    assert any(path.startswith("/api/captures/") for path in paths)
    # FastAPI flattens included router entries into app.routes; inspect the public
    # route path instead of relying on the version-specific original_router detail.
    route_paths = {
        route.path for route in app.routes if hasattr(route, "path")
    }
    assert "/ws/sdacs/live" in route_paths
