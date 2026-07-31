from pathlib import Path

import pytest
from fastapi import HTTPException

from app.calibration import build_calibration_preview
from app.capture_service import CaptureService
from app.config import Settings
from app.models import CalibrationApplyRequest, CalibrationPreviewRequest, TelemetryUpdate
from app.routes import build_router
from app.state_store import StateStore


EXPECTED_NODES = ["node01", "node02", "node03", "node04"]


def calibration_rows(*, detected: bool = True, clipped: int = 0):
    rows = []
    for node_index, node_id in enumerate(EXPECTED_NODES):
        for sample_index in range(5):
            dbfs = -50.0 - node_index * 0.25 + sample_index * 0.02
            rows.append(
                {
                    "node_id": node_id,
                    "record_type": "features",
                    "dbfs": dbfs,
                    "db_spl": dbfs + 120.0,
                    "cal_offset_db": 120.0,
                    "tone_1khz_peak_hz": 998.0 + sample_index,
                    "tone_1khz_detected": detected,
                    "clipped_sample_count": clipped,
                    "spectral_clipped_sample_count": 0,
                }
            )
    return rows


def test_preview_calculates_backend_authoritative_offset():
    preview = build_calibration_preview(
        "capture_calibration",
        70.0,
        calibration_rows(),
        EXPECTED_NODES,
    )

    assert preview["status"] == "ready"
    assert len(preview["nodes"]) == 4
    node01 = preview["nodes"][0]
    assert node01["measured_dbfs"] == pytest.approx(-49.96)
    assert node01["suggested_offset_db"] == pytest.approx(119.96)
    assert node01["tone_detection_rate"] == pytest.approx(1.0)
    assert node01["representative_frequency_hz"] == pytest.approx(1000.0)
    assert node01["eligible"] is True
    assert node01["within_tolerance"] is True


@pytest.mark.parametrize(
    "rows, warning_fragment",
    [
        (calibration_rows(detected=False), "detected in only"),
        (calibration_rows(clipped=2), "Clipped samples"),
    ],
)
def test_preview_blocks_unsafe_capture_data(rows, warning_fragment):
    preview = build_calibration_preview(
        "capture_invalid",
        70.0,
        rows,
        EXPECTED_NODES,
    )

    assert preview["status"] == "invalid"
    assert all(not node["eligible"] for node in preview["nodes"])
    assert all(
        any(warning_fragment in warning for warning in node["warnings"])
        for node in preview["nodes"]
    )


def make_service(tmp_path: Path) -> tuple[Settings, CaptureService]:
    settings = Settings(
        capture_data_dir=tmp_path / "captures",
        sqlite_path=tmp_path / "telemetry.db",
        capture_completion_grace_seconds=0,
        ei_enabled=False,
    )
    service = CaptureService(settings)
    service.store.initialize()
    service.layouts.initialize()
    session = service.create(
        "capture_calibration_route",
        record_seconds=60,
        delay_ms=0,
        validation_label="calibration_1khz",
    )
    for row in calibration_rows():
        service.store.add_telemetry(session["capture_id"], row)
    session["status"] = "acoustic_only"
    service._save(session)
    return settings, service


class AcknowledgingMqtt:
    connected = True

    def __init__(self, state: StateStore, *, mismatch: bool = False):
        self.state = state
        self.mismatch = mismatch
        self.messages = []

    def publish_json(self, topic, payload):
        self.messages.append((topic, payload))
        reported = payload["cal_offset_db"] + (1.0 if self.mismatch else 0.0)
        self.state.update(
            TelemetryUpdate(
                node_id=topic.split("/")[2],
                record_type="command_response",
                request_id=payload["request_id"],
                cmd="set_cal_offset",
                result="ok",
                reason="saved",
                cal_offset_db=reported,
            )
        )
        return True


def route_endpoint(router, path: str):
    return next(route.endpoint for route in router.routes if route.path == path)


@pytest.mark.asyncio
async def test_apply_route_publishes_per_node_and_requires_matching_ack(tmp_path):
    settings, service = make_service(tmp_path)
    state = StateStore()
    mqtt = AcknowledgingMqtt(state)
    router = build_router(settings, state, mqtt, object(), service)

    preview = await route_endpoint(router, "/api/calibration/preview")(
        CalibrationPreviewRequest(
            capture_id="capture_calibration_route",
            reference_spl_db=70.0,
        )
    )
    assert preview["status"] == "ready"

    result = await route_endpoint(router, "/api/calibration/apply")(
        CalibrationApplyRequest(
            capture_id="capture_calibration_route",
            reference_spl_db=70.0,
            acknowledgement_timeout_seconds=0.5,
        )
    )

    assert result["status"] == "complete"
    assert len(mqtt.messages) == 4
    assert [topic for topic, _ in mqtt.messages] == [
        f"sdacs/node/{node_id}/cmd" for node_id in EXPECTED_NODES
    ]
    assert all(payload["cmd"] == "set_cal_offset" for _, payload in mqtt.messages)
    assert all(row["applied"] for row in result["nodes"])
    assert service.artifact(
        "capture_calibration_route", "calibration_result.json"
    ).is_file()


@pytest.mark.asyncio
async def test_apply_route_rejects_acknowledged_offset_mismatch(tmp_path):
    settings, service = make_service(tmp_path)
    state = StateStore()
    mqtt = AcknowledgingMqtt(state, mismatch=True)
    router = build_router(settings, state, mqtt, object(), service)

    result = await route_endpoint(router, "/api/calibration/apply")(
        CalibrationApplyRequest(
            capture_id="capture_calibration_route",
            reference_spl_db=70.0,
            acknowledgement_timeout_seconds=0.5,
        )
    )

    assert result["status"] == "failed"
    assert all(row["acknowledged"] for row in result["nodes"])
    assert all(not row["applied"] for row in result["nodes"])
    assert all(
        row["reason"] == "reported_offset_mismatch" for row in result["nodes"]
    )


@pytest.mark.asyncio
async def test_apply_route_blocks_non_calibration_capture(tmp_path):
    settings, service = make_service(tmp_path)
    speech = service.create(
        "capture_speech_route",
        record_seconds=60,
        delay_ms=0,
        validation_label="speech",
    )
    speech["status"] = "acoustic_only"
    service._save(speech)
    state = StateStore()
    router = build_router(settings, state, AcknowledgingMqtt(state), object(), service)

    with pytest.raises(HTTPException) as error:
        await route_endpoint(router, "/api/calibration/apply")(
            CalibrationApplyRequest(
                capture_id="capture_speech_route",
                reference_spl_db=70.0,
            )
        )
    assert error.value.status_code == 409
