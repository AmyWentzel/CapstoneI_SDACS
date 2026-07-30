import json
import math
import subprocess
from pathlib import Path

import pytest

from app.ai_features import (
    FeatureWindowError,
    SDACS_V3_FEATURE_NAMES,
    SDACS_V3_SOURCE_FIELDS,
    build_feature_window,
    export_feature_window,
)
from app.config import Settings
from app.edge_impulse import ConfiguredEdgeImpulseRunner


def configured(tmp_path: Path) -> Settings:
    runner = tmp_path / "runner"
    runner.write_text("fixture", encoding="utf-8")
    runner.chmod(0o755)
    return Settings(
        capture_data_dir=tmp_path / "captures",
        sqlite_path=tmp_path / "db.sqlite",
        ei_enabled=True,
        ei_runner_path=runner,
    )


def test_exact_feature_contract_is_immutable_and_complete():
    assert isinstance(SDACS_V3_FEATURE_NAMES, tuple)
    assert len(SDACS_V3_FEATURE_NAMES) == 57
    assert SDACS_V3_FEATURE_NAMES[0] == "dbfs_mean"
    assert SDACS_V3_FEATURE_NAMES[-1] == "fft_total_energy_range"


def source_rows():
    return [
        {
            "capture_id": "capture_fixture",
            "sample_index": 7,
            "node_id": f"node0{index}",
            **{field: float(index) for field in SDACS_V3_SOURCE_FIELDS},
        }
        for index in range(1, 5)
    ]


def test_manual_four_node_fixture_uses_population_statistics_and_order():
    window = build_feature_window(
        source_rows(), capture_id="capture_fixture", sample_index=7
    )
    assert window["feature_names"] == list(SDACS_V3_FEATURE_NAMES)
    assert len(window["feature_values"]) == 57
    for offset in range(0, 57, 3):
        assert window["feature_values"][offset] == pytest.approx(2.5)
        assert window["feature_values"][offset + 1] == pytest.approx(math.sqrt(1.25))
        assert window["feature_values"][offset + 2] == pytest.approx(3.0)


def test_missing_node_is_rejected():
    with pytest.raises(FeatureWindowError, match="Missing node"):
        build_feature_window(
            source_rows()[:-1], capture_id="capture_fixture", sample_index=7
        )


def test_duplicate_node_is_rejected():
    rows = source_rows()
    rows.append(dict(rows[0]))
    with pytest.raises(FeatureWindowError, match="Duplicate node"):
        build_feature_window(rows, capture_id="capture_fixture", sample_index=7)


def test_non_finite_source_value_is_rejected():
    rows = source_rows()
    rows[0]["dbfs"] = float("nan")
    with pytest.raises(FeatureWindowError, match="non-finite"):
        build_feature_window(rows, capture_id="capture_fixture", sample_index=7)


def test_debug_export_contains_source_rows_and_ordered_values(tmp_path):
    csv_path = tmp_path / "acoustic.csv"
    fieldnames = ["capture_id", "sample_index", "node_id", *SDACS_V3_SOURCE_FIELDS]
    import csv
    with csv_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(source_rows())
    output = tmp_path / "window.json"
    export_feature_window(csv_path, "capture_fixture", 7, output)
    value = json.loads(output.read_text(encoding="utf-8"))
    assert [row["node_id"] for row in value["source_rows"]] == [
        "node01", "node02", "node03", "node04"
    ]
    assert value["feature_names"] == list(SDACS_V3_FEATURE_NAMES)
    assert len(value["feature_values"]) == 57
    assert value["aggregation_rules"]["standard_deviation"].endswith("(ddof=0)")


def test_health_accepts_verified_feature_provenance(tmp_path, monkeypatch):
    runner = ConfiguredEdgeImpulseRunner(configured(tmp_path))
    monkeypatch.setattr(runner, "_executable_available", lambda: True)
    monkeypatch.setattr(runner, "_run", lambda arguments: {
        "status": "ready",
        "project_name": "SDACS_V3",
        "project_id": 1071949,
        "impulse_id": 1,
        "deploy_version": 1,
        "feature_count": 57,
        "labels": ["noisy", "quiet_room_white_noise", "speech"],
    })
    health = runner.health()
    assert health["status"] == "ready"
    assert health["feature_provenance_verified"] is True
    assert health["input_feature_count"] == 57


def test_runner_result_validation_rejects_bad_probabilities(tmp_path):
    runner = ConfiguredEdgeImpulseRunner(configured(tmp_path))
    with pytest.raises(ValueError, match="invalid probability"):
        runner._validate_result("capture_bad", {
            "model": {"project_id": 1071949, "deploy_version": 1},
            "probabilities": {
                "noisy": -0.1,
                "quiet_room_white_noise": 0.2,
                "speech": 0.9,
            },
            "top_label": "speech",
        })


def test_golden_fixture_has_exact_contract():
    fixture = Path(__file__).parents[1] / "edge_impulse" / "tests" / "golden_fixture.json"
    value = json.loads(fixture.read_text(encoding="utf-8"))
    assert value["status"] == "ready"
    assert value["model_variant"] == "Unoptimized float32"
    assert value["feature_names"] == list(SDACS_V3_FEATURE_NAMES)
    assert len(value["features"]) == 57
    assert value["expected_top_label"] == "quiet_room_white_noise"
    assert value["expected_probabilities"] == {
        "noisy": 0.03,
        "quiet_room_white_noise": 0.90,
        "speech": 0.08,
    }
    assert value["tolerance"] == 0.02
    assert value["features"][43] == 162.3798


def test_scene_hpf_fields_are_preferred_over_unfiltered_fields():
    rows = source_rows()
    for index, row in enumerate(rows, start=1):
        row["scene_metrics_valid"] = True
        for field in SDACS_V3_SOURCE_FIELDS:
            row[f"scene_{field}"] = float(index * 10)
            row[field] = float(index)

    window = build_feature_window(
        rows, capture_id="capture_fixture", sample_index=7
    )

    assert window["source_path"] == "scene_hpf_150hz_gain16"
    assert window["features"]["dbfs_mean"] == pytest.approx(25.0)
    assert window["features"]["dbfs_range"] == pytest.approx(30.0)


def test_invalid_scene_metrics_fall_back_to_unfiltered_fields():
    rows = source_rows()
    for index, row in enumerate(rows, start=1):
        row["scene_metrics_valid"] = False
        for field in SDACS_V3_SOURCE_FIELDS:
            row[f"scene_{field}"] = float(index * 100)
            row[field] = float(index)

    window = build_feature_window(
        rows, capture_id="capture_fixture", sample_index=7
    )

    assert window["features"]["dbfs_mean"] == pytest.approx(2.5)
    assert window["features"]["dbfs_range"] == pytest.approx(3.0)
