import asyncio
import json
from pathlib import Path

import pytest

from app.capture_processing import MODEL_FEATURES, acoustic_analysis, atomic_json, build_model_input, fuse
from app.capture_service import CaptureService, validate_capture_id
from app.config import Settings
from app.edge_impulse import ConfiguredEdgeImpulseRunner, TestStubEdgeImpulseRunner
from app.models import TelemetryUpdate


def settings(tmp_path: Path) -> Settings:
    return Settings(
        capture_data_dir=tmp_path / "captures",
        sqlite_path=tmp_path / "telemetry.db",
        capture_completion_grace_seconds=0,
        ei_enabled=False,
    )


def test_model_absence_has_no_fabricated_prediction(tmp_path):
    result = ConfiguredEdgeImpulseRunner(settings(tmp_path)).infer("capture_test", tmp_path / "input.csv")
    assert result["predicted_label"] is None
    assert result["confidence"] is None
    assert result["scores"] == {}


def test_feature_builder_excludes_labels_ids_and_rssi(tmp_path):
    output = tmp_path / "edge_impulse_input.csv"
    metadata = build_model_input("capture_one", [{
        "record_type": "features", "node_id": "node01", "validation_label": "speech",
        "capture_id": "capture_one", "ble_rssi_dbm": -50, "rssi_dbm": -40,
        **{name: 1.0 for name in MODEL_FEATURES},
    }], output)
    header = output.read_text(encoding="utf-8").splitlines()[0]
    assert metadata["capture_id"] == "capture_one"
    assert "label" not in header
    assert "capture_id" not in header
    assert "rssi" not in header


def test_two_captures_are_persistent_and_isolated(tmp_path):
    service = CaptureService(settings(tmp_path))
    service.store.initialize()
    first = service.create("capture_one", 60, 5000, None)
    second = service.create("capture_two", 60, 5000, None)
    service.observe(TelemetryUpdate(node_id="node01", record_type="features", request_id="capture_one", rms=1))
    assert first["capture_id"] != second["capture_id"]
    assert len(service.store.telemetry("capture_one")) == 1
    assert service.store.telemetry("capture_two") == []
    reloaded = CaptureService(settings(tmp_path))
    reloaded.store.initialize()
    assert reloaded.require("capture_one")["record_seconds"] == 60


def test_four_node_completion_is_idempotent(tmp_path):
    service = CaptureService(settings(tmp_path))
    service.store.initialize()
    service.create("capture_nodes", 60, 5000, None)
    for node in ("node01", "node02", "node03", "node04", "node04"):
        service.observe(TelemetryUpdate(node_id=node, record_type="capture_complete", request_id="capture_nodes"))
    session = service.require("capture_nodes")
    assert session["completed_nodes"] == ["node01", "node02", "node03", "node04"]
    assert session["status"] == "capture_complete"


def test_test_stub_is_explicit_and_simulated(tmp_path):
    runner = TestStubEdgeImpulseRunner({
        "status": "complete", "predicted_label": "speech", "confidence": 0.8,
        "scores": {"quiet_room": 0.1, "noisy": 0.1, "speech": 0.8},
    })
    result = runner.infer("capture_stub", tmp_path / "input.csv")
    assert result["provider"] == "test_stub"
    assert result["is_simulated"] is True


def test_unknown_model_class_is_rejected(tmp_path):
    runner = TestStubEdgeImpulseRunner({"status": "complete", "predicted_label": "tone", "confidence": 1.0})
    with pytest.raises(ValueError, match="Unexpected model output class"):
        runner.infer("capture_stub", tmp_path / "input.csv")


def test_model_unavailable_combination_is_acoustic_only():
    combined = fuse(
        {"capture_id": "capture_one", "completed_nodes": ["node01"], "missing_nodes": ["node02"]},
        {"status": "complete", "warnings": []},
        {"status": "model_not_configured", "warnings": [], "confidence": None},
    )
    assert combined["status"] == "acoustic_only"
    assert combined["fusion"]["agreement"] == "unavailable"


def test_capture_id_rejects_traversal():
    with pytest.raises(ValueError):
        validate_capture_id("capture_../../secret")


def test_acoustic_summary_contains_capture_wide_per_node_metrics(tmp_path):
    layout = {
        "capture_id": "capture_metrics", "layout_revision": 2,
        "coordinate_system": "room_upper_left_x_right_y_down", "units": "meters",
        "source": {"x_m": 2.5, "y_m": 2.0, "z_m": 1.2},
        "nodes": {
            f"node0{i}": {"normalized_x": i / 10, "normalized_y": i / 8,
                         "x_m": float(i), "y_m": float(i + 1), "z_m": 1.2}
            for i in range(1, 5)
        },
    }
    layout_path = tmp_path / "room_layout.json"
    atomic_json(layout_path, layout)
    rows = []
    for i in range(1, 5):
        for sample in range(3):
            rows.append({
                "record_type": "features", "node_id": f"node0{i}",
                "rms": 0.00001 * (i + sample), "dbfs": -90 + i + sample,
                "db_spl": 30 + i + sample, "f_peak_hz": 100 + i + sample,
            })
    rows[0]["rms"] = None
    summary = acoustic_analysis("capture_metrics", rows, tmp_path, layout_path)
    assert set(summary["node_metrics"]) == {"node01", "node02", "node03", "node04"}
    assert summary["node_metrics"]["node01"]["sample_count"] == 3
    assert summary["node_metrics"]["node01"]["mean_rms"] is not None
    assert summary["node_metrics"]["node02"]["mean_dbfs"] != summary["node_metrics"]["node03"]["mean_dbfs"]
    assert summary["node_metrics"]["node04"]["x_position_m"] == 4.0
    assert summary["node_metrics"]["node01"]["warnings"]
