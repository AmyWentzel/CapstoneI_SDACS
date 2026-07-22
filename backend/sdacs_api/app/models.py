from datetime import datetime, timezone
from typing import Any, Literal

from pydantic import BaseModel, Field


class TelemetryUpdate(BaseModel):
    node_id: str
    record_type: str
    timestamp_iso: str | None = None
    timestamp_us: int | None = None
    uptime_ms: int | None = None
    raw_timestamp: str | int | float | None = None
    status: str | None = None
    capture_state: str | None = None
    fw_version: str | None = None
    seq: int | None = None
    n: int | None = None
    rms: float | None = None
    dbfs: float | None = None
    db_spl: float | None = None
    f_peak_hz: float | None = None
    fft_low_ratio: float | None = None
    fft_mid_ratio: float | None = None
    fft_high_ratio: float | None = None
    fft_total_energy: float | None = None
    p2p_raw: int | None = None
    zeros: int | None = None
    temp_c: float | None = None
    rh_percent: float | None = None
    batt_soc_percent: float | None = None
    batt_voltage_v: float | None = None
    batt_valid: bool | None = None
    rssi_dbm: int | None = None
    free_heap: int | None = None
    wifi_connected: bool | None = None
    mqtt_connected: bool | None = None
    request_id: str | None = None
    raw_path: str | None = None
    wav_path: str | None = None
    metrics_path: str | None = None
    raw_json: dict[str, Any] = Field(default_factory=dict)


class NodeState(BaseModel):
    node_id: str
    latest: TelemetryUpdate | None = None
    latest_features: TelemetryUpdate | None = None
    latest_status: TelemetryUpdate | None = None
    last_seen_iso: str | None = None


CaptureLabel = Literal[
    "noisy",
    "speech",
    "low",
    "mid",
    "high",
    "quiet_room_white_noise",
]


class CaptureStartRequest(BaseModel):
    delay_ms: int = 5000
    record_seconds: int = Field(default=60, ge=1, le=600)
    request_id: str | None = None
    label: CaptureLabel | None = None


class CommandRequest(BaseModel):
    cmd: Literal["report_status", "reboot"]
    request_id: str | None = None


class PublishResult(BaseModel):
    topic: str
    payload: dict[str, Any]
    published: bool


class BleNodeScanResult(BaseModel):
    node_id: str
    ble_rssi_dbm: int
    address: str
    local_name: str
    seen: bool = True


class BleScanResponse(BaseModel):
    request_id: str
    status: Literal["complete"]
    started_at: str
    completed_at: str
    scan_duration_seconds: float
    detected_count: int
    nodes: list[BleNodeScanResult]


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()
