import asyncio
import json
import logging
from typing import Any

import paho.mqtt.client as mqtt

from .config import Settings
from .models import TelemetryUpdate
from .state_store import StateStore
from .websocket_manager import WebSocketManager


LOG = logging.getLogger(__name__)

SUBSCRIPTIONS = [
    "sdacs/node/+/features",
    "sdacs/node/+/status/heartbeat",
    "sdacs/node/+/status",
    "sdacs/node/+/capture_complete",
    "sdacs/node/+/temp_humidity",
    "sdacs/node/+/fuel_gauge",
    "sdacs/node/+/ota/status",
    "sdacs/node/+/presence",
    "sdacs/node/+/acoustic_ai",
]

FIELD_ALIASES = {
    "node": "node_id",
    "nodeId": "node_id",
    "timestamp": "timestamp_iso",
    "timestampIso": "timestamp_iso",
    "timestampUs": "timestamp_us",
    "captureState": "capture_state",
    "fw": "fw_version",
    "firmware": "fw_version",
    "firmware_version": "fw_version",
    "firmwareVersion": "fw_version",
    "peak_hz": "f_peak_hz",
    "peakFrequencyHz": "f_peak_hz",
    "low_ratio": "fft_low_ratio",
    "mid_ratio": "fft_mid_ratio",
    "high_ratio": "fft_high_ratio",
    "total_energy": "fft_total_energy",
    "temperature_c": "temp_c",
    "temperatureC": "temp_c",
    "humidity_percent": "rh_percent",
    "humidityPercent": "rh_percent",
    "soc_percent": "batt_soc_percent",
    "battery_soc_percent": "batt_soc_percent",
    "batterySoc": "batt_soc_percent",
    "voltage_v": "batt_voltage_v",
    "battery_voltage_v": "batt_voltage_v",
    "batteryVoltage": "batt_voltage_v",
    "battery_valid": "batt_valid",
    "batteryValid": "batt_valid",
    "rssi": "rssi_dbm",
    "heap_free": "free_heap",
    "freeHeap": "free_heap",
    "wifiConnected": "wifi_connected",
    "mqttConnected": "mqtt_connected",
    "requestId": "request_id",
    "rawPath": "raw_path",
    "wavPath": "wav_path",
    "metricsPath": "metrics_path",
}


class SdacsMqttClient:
    def __init__(
        self,
        settings: Settings,
        state_store: StateStore,
        websocket_manager: WebSocketManager,
    ) -> None:
        self.settings = settings
        self.state_store = state_store
        self.websocket_manager = websocket_manager
        self.connected = False
        self._loop: asyncio.AbstractEventLoop | None = None
        self._client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=settings.mqtt_client_id,
        )
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message

    def start(self, loop: asyncio.AbstractEventLoop) -> None:
        self._loop = loop
        LOG.info("Connecting to MQTT broker %s:%s", self.settings.mqtt_host, self.settings.mqtt_port)
        self._client.connect_async(self.settings.mqtt_host, self.settings.mqtt_port, keepalive=60)
        self._client.loop_start()

    def stop(self) -> None:
        self._client.loop_stop()
        self._client.disconnect()

    def publish_json(self, topic: str, payload: dict[str, Any]) -> bool:
        result = self._client.publish(topic, json.dumps(payload), qos=1)
        return result.rc == mqtt.MQTT_ERR_SUCCESS

    def _on_connect(self, client: mqtt.Client, userdata: Any, flags: Any, reason_code: Any, properties: Any) -> None:
        self.connected = reason_code == 0
        LOG.info("Connected to MQTT broker with reason code %s", reason_code)
        if not self.connected:
            return
        for topic in SUBSCRIPTIONS:
            client.subscribe(topic, qos=0)
            LOG.info("Subscribed to %s", topic)

    def _on_disconnect(
        self,
        client: mqtt.Client,
        userdata: Any,
        disconnect_flags: Any,
        reason_code: Any,
        properties: Any,
    ) -> None:
        self.connected = False
        LOG.warning("Disconnected from MQTT broker with reason code %s", reason_code)

    def _on_message(self, client: mqtt.Client, userdata: Any, message: mqtt.MQTTMessage) -> None:
        try:
            telemetry = normalize_mqtt_message(message.topic, message.payload)
        except Exception:
            LOG.exception("Failed to normalize MQTT message on %s", message.topic)
            return

        telemetry = self.state_store.update(telemetry)
        if self._loop is not None:
            payload = telemetry.model_dump(mode="json")
            asyncio.run_coroutine_threadsafe(self.websocket_manager.broadcast(payload), self._loop)


def normalize_mqtt_message(topic: str, payload: bytes) -> TelemetryUpdate:
    raw_payload = _decode_payload(payload)
    topic_parts = topic.split("/")
    node_id = _node_id_from_topic(topic_parts, raw_payload)
    record_type = _record_type_from_topic(topic_parts, raw_payload)

    normalized: dict[str, Any] = {
        "node_id": node_id,
        "record_type": record_type,
        "raw_json": raw_payload,
    }
    for key, value in raw_payload.items():
        normalized_key = FIELD_ALIASES.get(key, key)
        if normalized_key in TelemetryUpdate.model_fields:
            normalized[normalized_key] = value

    normalized["node_id"] = str(normalized.get("node_id") or node_id)
    normalized["record_type"] = str(normalized.get("record_type") or record_type)
    return TelemetryUpdate.model_validate(normalized)


def _decode_payload(payload: bytes) -> dict[str, Any]:
    text = payload.decode("utf-8", errors="replace").strip()
    if not text:
        return {}
    try:
        decoded = json.loads(text)
    except json.JSONDecodeError:
        return {"payload": text}
    return decoded if isinstance(decoded, dict) else {"payload": decoded}


def _node_id_from_topic(topic_parts: list[str], payload: dict[str, Any]) -> str:
    if len(topic_parts) >= 3 and topic_parts[0:2] == ["sdacs", "node"]:
        return str(topic_parts[2])
    return str(payload.get("node_id", "unknown"))


def _record_type_from_topic(topic_parts: list[str], payload: dict[str, Any]) -> str:
    if "record_type" in payload:
        return str(payload["record_type"])
    if topic_parts[-2:] == ["status", "heartbeat"]:
        return "heartbeat"
    if topic_parts[-2:] == ["ota", "status"]:
        return "ota_status"
    return topic_parts[-1] if topic_parts else "unknown"
