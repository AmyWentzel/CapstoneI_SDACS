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
    rows = []
    from app.ai_features import SDACS_V3_SOURCE_FIELDS
    for index in range(1, 5):
        rows.append({
            "record_type": "features",
            "request_id": "capture_one",
            "seq": 1,
            "node_id": f"node0{index}",
            "validation_label": "speech",
            "rssi_dbm": -40,
            **{field: float(index) for field in SDACS_V3_SOURCE_FIELDS},
        })
    metadata = build_model_input("capture_one", rows, output)
    header = output.read_text(encoding="utf-8").splitlines()[0]
    assert metadata["rows"] == 1
    assert len(header.split(",")) == 57
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


def test_per_window_inference_artifact_preserves_acoustic_only_status(tmp_path):
    from app.ai_features import SDACS_V3_SOURCE_FIELDS
    configured = settings(tmp_path).model_copy(update={"ei_enabled": True})
    runner = TestStubEdgeImpulseRunner({
        "status": "complete",
        "top_label": "quiet_room_white_noise",
        "confidence": 0.9,
        "accepted": True,
        "probabilities": {
            "noisy": 0.03,
            "quiet_room_white_noise": 0.90,
            "speech": 0.07,
        },
        "timing_ms": {"classification": 2},
    })
    service = CaptureService(configured, runner=runner)
    service.store.initialize()
    service.create("capture_windows", 60, 0, None)
    for index in range(1, 5):
        service.store.add_telemetry("capture_windows", {
            "record_type": "features",
            "request_id": "capture_windows",
            "node_id": f"node0{index}",
            "seq": 1,
            "db_spl": 30.0 + index,
            "f_peak_hz": 100.0 + index,
            **{field: float(index) for field in SDACS_V3_SOURCE_FIELDS},
        })
    service._process_sync("capture_windows")
    directory = service.directory("capture_windows")
    windows = json.loads(
        (directory / "ai_window_predictions.json").read_text(encoding="utf-8")
    )
    summary = json.loads((directory / "ai_summary.json").read_text(encoding="utf-8"))
    combined = json.loads((directory / "combined_result.json").read_text(encoding="utf-8"))
    assert windows["window_count"] == 1
    assert len(windows["windows"][0]["feature_values"]) == 57
    assert summary["status"] == "fusion_not_configured"
    assert summary["successful_window_count"] == 1
    assert combined["overall_status"] == "acoustic_only"
    assert combined["acoustic_status"] == "complete"


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
        "room_width_m": 5.0, "room_depth_m": 6.0,
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


def test_spatial_profile_uses_weighted_bands_median_frequency_and_snapshot_coordinates(tmp_path):
    layout = {
        "capture_id": "capture_profile", "layout_revision": 7,
        "coordinate_system": "room_upper_left_x_right_y_down", "units": "meters",
        "room_width_m": 6.0, "room_depth_m": 4.0,
        "source": {
            "normalized_x": 0.62, "normalized_y": 0.41,
            "x_m": 3.72, "y_m": 1.64, "z_m": 1.2,
        },
        "nodes": {
            "node01": {
                "normalized_x": 0.2, "normalized_y": 0.3,
                "x_m": 1.2, "y_m": 1.2, "z_m": 1.2,
            },
            "node02": {
                "normalized_x": 0.8, "normalized_y": 0.7,
                "x_m": 4.8, "y_m": 2.8, "z_m": 1.2,
            },
            "node03": {
                "normalized_x": 0.2, "normalized_y": 0.7,
                "x_m": 1.2, "y_m": 2.8, "z_m": 1.2,
            },
            "node04": {
                "normalized_x": 0.8, "normalized_y": 0.3,
                "x_m": 4.8, "y_m": 1.2, "z_m": 1.2,
            },
        },
    }
    layout_path = tmp_path / "room_layout.json"
    atomic_json(layout_path, layout)
    rows = [
        {
            "record_type": "features", "node_id": "node01",
            "db_spl": 33.0, "f_peak_hz": 80.0,
            "fft_total_energy": 2.0,
            "fft_low_ratio": 0.5, "fft_mid_ratio": 0.3, "fft_high_ratio": 0.2,
        },
        {
            "record_type": "features", "node_id": "node01",
            "db_spl": 35.0, "f_peak_hz": 120.0,
            "fft_total_energy": 1.0,
            "fft_low_ratio": 0.2, "fft_mid_ratio": 0.3, "fft_high_ratio": 0.5,
        },
        {
            "record_type": "features", "node_id": "node01",
            "db_spl": 34.0, "f_peak_hz": 100.0,
            "fft_total_energy": 1.0,
            "fft_low_ratio": 0.2, "fft_mid_ratio": 0.2, "fft_high_ratio": 0.6,
        },
        {
            "record_type": "features", "node_id": "node02",
            "db_spl": 32.0, "f_peak_hz": 100.0,
            "fft_total_energy": None,
            "fft_low_ratio": None, "fft_mid_ratio": None, "fft_high_ratio": None,
        },
        {
            "record_type": "features", "node_id": "node02",
            "db_spl": 32.5, "f_peak_hz": 200.0,
        },
        *[
            {
                "record_type": "features", "node_id": "node02",
                "db_spl": 32.0, "f_peak_hz": invalid_peak,
            }
            for invalid_peak in (None, 0.0, -1.0, float("nan"), float("inf"))
        ],
        *[
            {
                "record_type": "features", "node_id": "node03",
                "db_spl": 31.0, "f_peak_hz": invalid_peak,
            }
            for invalid_peak in (None, 0.0, -2.0, float("nan"), float("inf"))
        ],
    ]

    summary = acoustic_analysis("capture_profile", rows, tmp_path, layout_path)
    node01 = summary["node_metrics"]["node01"]
    assert node01["representative_peak_frequency_hz"] == pytest.approx(100.0)
    assert node01["peak_frequency_hz"] == pytest.approx(100.0)
    assert node01["mean_fft_low_ratio"] == pytest.approx(1.4 / 4.0)
    assert node01["mean_fft_mid_ratio"] == pytest.approx(1.1 / 4.0)
    assert node01["mean_fft_high_ratio"] == pytest.approx(1.5 / 4.0)
    assert sum(node01[key] for key in (
        "mean_fft_low_ratio", "mean_fft_mid_ratio", "mean_fft_high_ratio"
    )) == pytest.approx(1.0)
    assert summary["node_metrics"]["node02"]["mean_fft_low_ratio"] is None
    assert summary["node_metrics"]["node02"]["mean_fft_mid_ratio"] is None
    assert summary["node_metrics"]["node02"]["mean_fft_high_ratio"] is None
    assert summary["node_metrics"]["node02"]["representative_peak_frequency_hz"] == pytest.approx(150.0)
    assert summary["node_metrics"]["node03"]["representative_peak_frequency_hz"] is None
    assert "Band ratios unavailable for this node." in summary["node_metrics"]["node02"]["warnings"]
    assert node01["x_position_m"] == pytest.approx(1.2)
    assert node01["y_position_m"] == pytest.approx(1.2)
    assert summary["plot_coordinates"]["source"]["x_position_m"] == pytest.approx(3.72)
    assert summary["plot_coordinates"]["source"]["y_position_m"] == pytest.approx(1.64)
    assert set(summary["plot_coordinates"]["nodes"]) == {"node01", "node02", "node03"}
    assert set(summary["node_metrics"]) == {"node01", "node02", "node03"}
    assert "node04" not in summary["node_metrics"]
    assert summary["plot_title"] == "Spatial Acoustic Profile\ncapture_profile"
    assert "capture capture_" not in summary["plot_title"]
    assert (tmp_path / "acoustic_map.png").is_file()
    assert (tmp_path / "acoustic_map.png").stat().st_size > 0
