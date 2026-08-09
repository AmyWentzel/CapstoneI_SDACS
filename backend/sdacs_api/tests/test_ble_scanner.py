"""SDACS verification: regression tests for ble scanner.

These tests document expected final-system behavior and protect the production merge from regressions.
"""

import pytest

from app import ble_scanner
from app.ble_scanner import BleScanError, node_id_from_advertisement, normalize_observations


def test_parses_supported_name_and_filters_unrelated_device():
    assert node_id_from_advertisement("SDACS-node01", {}) == "node01"
    assert node_id_from_advertisement("Headphones", {}) is None


def test_parses_manufacturer_payload_after_bleak_company_id_split():
    company_id = int.from_bytes(b"SD", "little")
    assert node_id_from_advertisement(None, {company_id: b"ACS:node04"}) == "node04"


def test_deduplicates_with_median_rssi_and_sorts_nodes():
    results = normalize_observations(
        {"node02": [-80, -60, -70], "node01": [-55, -57, -56]},
        {
            "node02": ("AA:02", "SDACS-node02"),
            "node01": ("AA:01", "SDACS-node01"),
        },
    )
    assert [row["node_id"] for row in results] == ["node01", "node02"]
    assert results[0]["ble_rssi_dbm"] == -56
    assert results[1]["ble_rssi_dbm"] == -70
    assert all("rssi_dbm" not in row for row in results)


@pytest.mark.asyncio
async def test_missing_scanner_dependency_is_not_an_empty_success(monkeypatch):
    monkeypatch.setattr(ble_scanner, "BleakScanner", None)
    with pytest.raises(BleScanError, match="dependency is not installed"):
        await ble_scanner.scan_sdacs_nodes(0.01)
