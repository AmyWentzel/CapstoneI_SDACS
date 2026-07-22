import asyncio

import pytest
from fastapi import HTTPException

from app import routes as routes_module
from app.config import Settings
from app.routes import build_router


class FakeMqttClient:
    connected = True

    def __init__(self, accepted: bool = True):
        self.accepted = accepted
        self.messages = []

    def publish_json(self, topic, payload):
        self.messages.append((topic, payload))
        return self.accepted


def ble_endpoint(mqtt_client):
    router = build_router(Settings(), object(), mqtt_client, object(), object())
    return next(route.endpoint for route in router.routes if route.path == "/api/ble/scan")


@pytest.mark.asyncio
async def test_ble_route_publishes_exact_group_command(monkeypatch):
    mqtt = FakeMqttClient()
    async def no_delay(_duration):
        return None

    monkeypatch.setattr(routes_module, "_sleep", no_delay)

    async def fake_scan(_duration):
        return []

    monkeypatch.setattr(routes_module, "scan_sdacs_nodes", fake_scan)
    result = await ble_endpoint(mqtt)()
    topic, payload = mqtt.messages[0]
    assert topic == "sdacs/group/all/cmd"
    assert payload["cmd"] == "ble_advertise"
    assert payload["duration_ms"] == 15000
    assert payload["request_id"].startswith("ble_scan_")
    assert result.detected_count == 0


@pytest.mark.asyncio
async def test_overlapping_scan_returns_409(monkeypatch):
    entered = asyncio.Event()
    release = asyncio.Event()
    mqtt = FakeMqttClient()

    async def no_delay(_duration):
        return None

    async def blocked_scan(_duration):
        entered.set()
        await release.wait()
        return []

    monkeypatch.setattr(routes_module, "_sleep", no_delay)
    monkeypatch.setattr(routes_module, "scan_sdacs_nodes", blocked_scan)
    endpoint = ble_endpoint(mqtt)
    first = asyncio.create_task(endpoint())
    await entered.wait()
    with pytest.raises(HTTPException) as error:
        await endpoint()
    assert error.value.status_code == 409
    release.set()
    await first
