"""SDACS backend module: Pydantic request/response and telemetry models shared by MQTT normalization, capture services, and FastAPI routes.

The module is part of the Raspberry Pi middleware/API layer used by the final SDACS system.
"""

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
    mode: str | None = None
    state: str | None = None
    cmd: str | None = None
    result: str | None = None
    reason: str | None = None
    fw_version: str | None = None
    seq: int | None = None
    n: int | None = None
    rms: float | None = None
    dbfs: float | None = None
    db_spl: float | None = None
    peak_db_spl: float | None = None
    cal_offset_db: float | None = None
    tone_1khz_peak_hz: float | None = None
    tone_1khz_ratio: float | None = None
    tone_1khz_local_ratio: float | None = None
    tone_1khz_contrast_db: float | None = None
    tone_1khz_detected: bool | None = None
    f_peak_hz: float | None = None
    f_peak_acoustic_hz: float | None = None
    low_rumble_ratio: float | None = None
    band_bass_ratio: float | None = None
    band_low_mid_ratio: float | None = None
    band_mid_ratio: float | None = None
    band_presence_ratio: float | None = None
    band_high_ratio: float | None = None
    band_bass_peak_hz: float | None = None
    band_low_mid_peak_hz: float | None = None
    band_mid_peak_hz: float | None = None
    band_presence_peak_hz: float | None = None
    band_high_peak_hz: float | None = None
    dominant_band_ratio: float | None = None
    dominant_band_peak_hz: float | None = None
    fft_low_ratio: float | None = None
    fft_mid_ratio: float | None = None
    fft_high_ratio: float | None = None
    fft_total_energy: float | None = None

    # HPF scene path for quiet/speech/noisy classification.
    scene_metrics_valid: bool | None = None
    scene_hpf_enabled: bool | None = None
    scene_hpf_cutoff_hz: float | None = None
    scene_hpf_order: int | None = None
    scene_software_gain: float | None = None
    scene_rms: float | None = None
    scene_dbfs: float | None = None
    scene_db_spl: float | None = None
    scene_peak_db_spl: float | None = None
    scene_f_peak_hz: float | None = None
    scene_f_peak_acoustic_hz: float | None = None
    scene_low_rumble_ratio: float | None = None
    scene_band_bass_ratio: float | None = None
    scene_band_low_mid_ratio: float | None = None
    scene_band_mid_ratio: float | None = None
    scene_band_presence_ratio: float | None = None
    scene_band_high_ratio: float | None = None
    scene_band_bass_peak_hz: float | None = None
    scene_band_low_mid_peak_hz: float | None = None
    scene_band_mid_peak_hz: float | None = None
    scene_band_presence_peak_hz: float | None = None
    scene_band_high_peak_hz: float | None = None
    scene_dominant_band_name: str | None = None
    scene_dominant_band_ratio: float | None = None
    scene_dominant_band_peak_hz: float | None = None
    scene_fft_low_ratio: float | None = None
    scene_fft_mid_ratio: float | None = None
    scene_fft_high_ratio: float | None = None
    scene_fft_total_energy: float | None = None
    scene_p2p_raw: int | None = None
    scene_zeros: int | None = None
    scene_n: int | None = None

    # Gain/HPF verification diagnostics.
    hpf_enabled: bool | None = None
    hpf_cutoff_hz: float | None = None
    hpf_order: int | None = None
    mic_software_gain: float | None = None
    clipped_sample_count: int | None = None
    spectral_clipped_sample_count: int | None = None
    scene_clipped_sample_count: int | None = None
    pre_gain_dbfs: float | None = None
    post_hpf_dbfs: float | None = None
    current_dbfs: float | None = None
    scene_current_dbfs: float | None = None
    pre_gain_peak_abs: int | None = None
    post_gain_peak_abs: int | None = None
    scene_post_gain_peak_abs: int | None = None

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

RuntimeClass = Literal[
    "quiet_room",
    "noisy",
    "speech",
    "calibration_1khz",
    "calibration_sweep",
    "spl_calibration_verification",
]


class CaptureStartRequest(BaseModel):
    delay_ms: int = 5000
    record_seconds: int = Field(default=60, ge=1, le=600)
    request_id: str | None = None
    label: CaptureLabel | None = None
    validation_label: RuntimeClass | None = None


class CaptureStartResponse(BaseModel):
    capture_id: str
    status: Literal["requested"]
    requested_at: str
    scheduled_start_at: str
    record_seconds: int


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


class CalibrationPreviewRequest(BaseModel):
    capture_id: str
    reference_spl_db: float = Field(ge=30.0, le=140.0)


class CalibrationApplyRequest(CalibrationPreviewRequest):
    node_offsets_db: dict[str, float] | None = None
    allow_partial: bool = False
    acknowledgement_timeout_seconds: float = Field(default=5.0, ge=0.5, le=15.0)


class CalibrationNodePreview(BaseModel):
    node_id: str
    sample_count: int
    measured_dbfs: float | None = None
    measured_spl_db: float | None = None
    current_offset_db: float | None = None
    suggested_offset_db: float | None = None
    adjustment_db: float | None = None
    measurement_error_db: float | None = None
    representative_frequency_hz: float | None = None
    tone_detected_count: int = 0
    tone_sample_count: int = 0
    tone_detection_rate: float | None = None
    eligible: bool
    within_tolerance: bool | None = None
    warnings: list[str] = Field(default_factory=list)


class CalibrationPreviewResponse(BaseModel):
    capture_id: str
    reference_spl_db: float
    status: Literal["ready", "partial", "invalid"]
    tolerance_db: float
    generated_at: str
    nodes: list[CalibrationNodePreview]
    warnings: list[str] = Field(default_factory=list)


class CalibrationNodeApplyResult(BaseModel):
    node_id: str
    requested_offset_db: float
    request_id: str
    published: bool
    acknowledged: bool
    applied: bool
    reported_offset_db: float | None = None
    reason: str | None = None


class CalibrationApplyResponse(BaseModel):
    capture_id: str
    reference_spl_db: float
    status: Literal["complete", "partial", "failed"]
    applied_at: str
    nodes: list[CalibrationNodeApplyResult]
    warnings: list[str] = Field(default_factory=list)


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()
