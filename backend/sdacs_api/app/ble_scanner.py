import asyncio
from collections import defaultdict
from statistics import median
from typing import Any

try:
    from bleak import BleakScanner
    from bleak.exc import BleakError
except ImportError:  # Allows configuration/tests to report a useful setup error.
    BleakScanner = None  # type: ignore[assignment,misc]

    class BleakError(Exception):
        pass


class BleScanError(RuntimeError):
    """Raised when BlueZ cannot perform a BLE scan."""


def normalize_observations(
    observations: dict[str, list[int]], details: dict[str, tuple[str, str]]
) -> list[dict[str, Any]]:
    return [
        {
            "node_id": node_id,
            "ble_rssi_dbm": int(median(observations[node_id])),
            "address": details[node_id][0],
            "local_name": details[node_id][1],
            "seen": True,
        }
        for node_id in sorted(observations)
        if observations[node_id]
    ]


def node_id_from_advertisement(local_name: str | None, manufacturer_data: dict[int, bytes]) -> str | None:
    if local_name:
        normalized = local_name.strip()
        if normalized.startswith("SDACS-node") and normalized[10:].isdigit():
            node_id = normalized[6:]
            if node_id in {f"node{number:02d}" for number in range(1, 5)}:
                return node_id

    for company_id, value in manufacturer_data.items():
        # BlueZ/Bleak separates the first two manufacturer bytes as company ID.
        payloads = (value, company_id.to_bytes(2, "little") + value)
        for payload in payloads:
            marker = payload.find(b"SDACS:")
            if marker < 0:
                continue
            node_id = payload[marker + 6 :].decode("utf-8", errors="ignore").strip("\x00")
            if node_id in {f"node{number:02d}" for number in range(1, 5)}:
                return node_id
    return None


async def scan_sdacs_nodes(duration_seconds: float = 8.0) -> list[dict[str, Any]]:
    if BleakScanner is None:
        raise BleScanError("Bluetooth scanner dependency is not installed; install backend requirements")
    observations: dict[str, list[int]] = defaultdict(list)
    details: dict[str, tuple[str, str]] = {}

    def on_detection(device: Any, advertisement_data: Any) -> None:
        local_name = advertisement_data.local_name or getattr(device, "name", None)
        node_id = node_id_from_advertisement(local_name, advertisement_data.manufacturer_data)
        if node_id is None:
            return
        observations[node_id].append(int(advertisement_data.rssi))
        details[node_id] = (str(device.address), local_name or f"SDACS-{node_id}")

    try:
        scanner = BleakScanner(detection_callback=on_detection)
        await scanner.start()
        try:
            await asyncio.sleep(duration_seconds)
        finally:
            await scanner.stop()
    except (BleakError, OSError, PermissionError) as exc:
        raise BleScanError(f"Bluetooth scan unavailable: {exc}") from exc
    except asyncio.TimeoutError as exc:
        raise BleScanError("Bluetooth scan timed out") from exc

    return normalize_observations(observations, details)
