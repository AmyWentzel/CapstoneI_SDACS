import json
from pathlib import Path

import pytest
from pydantic import ValidationError

from app.room_layout import RoomLayout, RoomLayoutStore, default_layout


def test_default_layout_has_four_distinct_nodes(tmp_path: Path) -> None:
    store = RoomLayoutStore(tmp_path / "telemetry.db")
    layout = store.get()
    assert {item.node_id for item in layout.node_positions} == {
        "node01", "node02", "node03", "node04"
    }
    points = {
        (item.position.normalized_x, item.position.normalized_y)
        for item in layout.node_positions
    }
    assert len(points) == 4


def test_layout_save_persists_and_revision_prevents_stale_overwrite(tmp_path: Path) -> None:
    database = tmp_path / "telemetry.db"
    first_store = RoomLayoutStore(database)
    saved = first_store.save(
        default_layout().model_copy(update={"room_width_m": 5.0, "room_depth_m": 4.0})
    )
    assert saved.revision == 1
    assert RoomLayoutStore(database).get().room_width_m == 5.0
    with pytest.raises(ValueError, match="revision conflict"):
        first_store.save(default_layout())


@pytest.mark.parametrize("x,y", [(-0.1, 0.5), (1.1, 0.5), (float("nan"), 0.5)])
def test_invalid_normalized_coordinates_are_rejected(x: float, y: float) -> None:
    payload = default_layout().model_dump()
    payload["node_positions"][0]["position"]["normalized_x"] = x
    payload["node_positions"][0]["position"]["normalized_y"] = y
    with pytest.raises(ValidationError):
        RoomLayout.model_validate(payload)


def test_missing_duplicate_and_unknown_nodes_are_rejected() -> None:
    payload = default_layout().model_dump()
    payload["node_positions"].pop()
    with pytest.raises(ValidationError):
        RoomLayout.model_validate(payload)
    payload = default_layout().model_dump()
    payload["node_positions"][1]["node_id"] = "node01"
    with pytest.raises(ValidationError):
        RoomLayout.model_validate(payload)
    payload = default_layout().model_dump()
    payload["node_positions"][1]["node_id"] = "node99"
    with pytest.raises(ValidationError):
        RoomLayout.model_validate(payload)


def test_snapshots_are_capture_specific_and_do_not_mutate(tmp_path: Path) -> None:
    store = RoomLayoutStore(tmp_path / "telemetry.db")
    first = store.save(
        default_layout().model_copy(update={"room_width_m": 5.0, "room_depth_m": 4.0})
    )
    snapshot = store.snapshot(first, "capture_first")
    serialized = json.dumps(snapshot, sort_keys=True)
    moved_payload = first.model_dump()
    moved_payload["node_positions"][0]["position"]["normalized_x"] = 0.25
    store.save(RoomLayout.model_validate(moved_payload))
    assert json.dumps(snapshot, sort_keys=True) == serialized
    assert snapshot["capture_id"] == "capture_first"
    assert snapshot["nodes"]["node01"]["x_m"] == pytest.approx(0.8)


def test_snapshot_coordinates_do_not_depend_on_rssi(tmp_path: Path) -> None:
    store = RoomLayoutStore(tmp_path / "telemetry.db")
    snapshot = store.snapshot(default_layout(), "capture_rssi")
    def keys(value):
        if isinstance(value, dict):
            return list(value) + [nested for item in value.values() for nested in keys(item)]
        if isinstance(value, list):
            return [nested for item in value for nested in keys(item)]
        return []
    assert not any("rssi" in key.lower() for key in keys(snapshot))


def test_non_centered_source_persists_and_is_snapshotted(tmp_path: Path) -> None:
    database = tmp_path / "telemetry.db"
    store = RoomLayoutStore(database)
    payload = default_layout().model_dump()
    payload["room_width_m"] = 5.0
    payload["room_depth_m"] = 4.0
    payload["source_position"]["normalized_x"] = 0.62
    payload["source_position"]["normalized_y"] = 0.41
    saved = store.save(RoomLayout.model_validate(payload))

    reloaded = RoomLayoutStore(database).get()
    assert reloaded.source_position.normalized_x == pytest.approx(0.62)
    snapshot = store.snapshot(reloaded, "capture_source")
    assert snapshot["source"]["normalized_y"] == pytest.approx(0.41)
    assert snapshot["source"]["x_m"] == pytest.approx(3.1)
    assert snapshot["source"]["y_m"] == pytest.approx(1.64)


def test_invalid_source_coordinates_are_rejected() -> None:
    payload = default_layout().model_dump()
    payload["source_position"]["normalized_x"] = 1.01
    with pytest.raises(ValidationError):
        RoomLayout.model_validate(payload)
