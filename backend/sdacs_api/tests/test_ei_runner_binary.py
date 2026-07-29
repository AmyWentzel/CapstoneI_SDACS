import json
import math
import os
import subprocess
from pathlib import Path

import pytest

from app.ai_features import SDACS_V3_FEATURE_NAMES

RUNNER = Path(
    os.getenv(
        "SDACS_EI_TEST_RUNNER",
        Path(__file__).parents[1] / "edge_impulse" / "bin" / "sdacs_ei_runner",
    )
)

pytestmark = pytest.mark.skipif(
    not RUNNER.is_file(),
    reason="Build the Linux SDACS_V3 runner or set SDACS_EI_TEST_RUNNER.",
)


def invoke(value: str | dict) -> subprocess.CompletedProcess[str]:
    text = value if isinstance(value, str) else json.dumps(value)
    return subprocess.run(
        [str(RUNNER)],
        input=text,
        text=True,
        capture_output=True,
        check=False,
        timeout=30,
        shell=False,
    )


def valid_payload() -> dict:
    return {
        "feature_names": list(SDACS_V3_FEATURE_NAMES),
        "features": [1.0] * 57,
    }


def test_health_metadata():
    result = subprocess.run(
        [str(RUNNER), "--health"],
        text=True,
        capture_output=True,
        check=False,
        timeout=30,
        shell=False,
    )
    assert result.returncode == 0
    assert result.stderr == ""
    assert json.loads(result.stdout) == {
        "status": "ready",
        "project_name": "SDACS_V3",
        "project_id": 1071949,
        "impulse_id": 1,
        "deploy_version": 1,
        "feature_count": 57,
        "labels": ["noisy", "quiet_room_white_noise", "speech"],
    }


@pytest.mark.parametrize("count", [56, 58])
def test_wrong_feature_count_is_rejected(count):
    payload = valid_payload()
    payload["features"] = [1.0] * count
    assert invoke(payload).returncode != 0


def test_wrong_name_order_is_rejected():
    payload = valid_payload()
    payload["feature_names"][0], payload["feature_names"][1] = (
        payload["feature_names"][1],
        payload["feature_names"][0],
    )
    assert invoke(payload).returncode != 0


@pytest.mark.parametrize("bad", [None, "1.0", math.nan, math.inf])
def test_invalid_values_are_rejected(bad):
    payload = valid_payload()
    payload["features"][4] = bad
    assert invoke(json.dumps(payload, allow_nan=True)).returncode != 0


def test_malformed_json_is_rejected():
    assert invoke("{not json").returncode != 0


def test_success_is_json_only_and_probabilities_are_complete():
    result = invoke(valid_payload())
    assert result.returncode == 0
    value = json.loads(result.stdout)
    assert set(value["probabilities"]) == {
        "noisy",
        "quiet_room_white_noise",
        "speech",
    }
    top = max(value["probabilities"], key=value["probabilities"].get)
    assert value["top_label"] == top
    assert value["accepted"] == (value["confidence"] >= 0.6)


def test_studio_golden_window_matches_within_supplied_rounding_tolerance():
    fixture_path = (
        Path(__file__).parents[1] / "edge_impulse" / "tests" / "golden_fixture.json"
    )
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    result = invoke({
        "feature_names": fixture["feature_names"],
        "features": fixture["features"],
    })
    assert result.returncode == 0
    actual = json.loads(result.stdout)
    assert actual["top_label"] == fixture["expected_top_label"]
    for label, expected in fixture["expected_probabilities"].items():
        assert actual["probabilities"][label] == pytest.approx(
            expected, abs=fixture["tolerance"]
        )
