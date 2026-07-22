from __future__ import annotations

import os
from pathlib import Path
from typing import Any, Protocol

from .config import Settings


APPROVED_CLASSES = {"quiet_room", "noisy", "speech"}


class EdgeImpulseRunner(Protocol):
    def health(self) -> dict[str, Any]: ...
    def infer(self, capture_id: str, input_path: Path) -> dict[str, Any]: ...


class ConfiguredEdgeImpulseRunner:
    """Replaceable production adapter; deliberately schema-neutral until the final EIM exists."""

    def __init__(self, settings: Settings) -> None:
        self.settings = settings

    def health(self) -> dict[str, Any]:
        path = self.settings.eim_path
        configured = path is not None
        readable = bool(path and path.is_file() and os.access(path, os.R_OK))
        status = "disabled"
        if self.settings.ei_enabled and not configured:
            status = "model_not_configured"
        elif self.settings.ei_enabled and not readable:
            status = "model_unavailable"
        elif self.settings.ei_enabled and readable:
            status = "schema_not_finalized"
        return {
            "enabled": self.settings.ei_enabled,
            "provider": "edge_impulse_eim",
            "status": status,
            "model_configured": configured,
            "model_file_readable": readable,
            "model_path": path.name if path else None,
            "runtime_available": False,
            "schema_version": self.settings.ei_schema_version,
            "model_version": None,
            "is_simulated": False,
        }

    def infer(self, capture_id: str, input_path: Path) -> dict[str, Any]:
        health = self.health()
        status = health["status"]
        warning = {
            "disabled": "Edge Impulse inference is disabled.",
            "model_not_configured": "The final Edge Impulse model has not been configured.",
            "model_unavailable": "The configured Edge Impulse model is not readable.",
            "schema_not_finalized": "The final Edge Impulse feature schema has not been finalized.",
        }[status]
        return {
            "capture_id": capture_id,
            "status": status,
            "provider": "edge_impulse_eim",
            "is_simulated": False,
            "predicted_label": None,
            "confidence": None,
            "scores": {},
            "model_version": None,
            "model_path": health["model_path"],
            "aggregation_method": None,
            "warnings": [warning],
        }


class TestStubEdgeImpulseRunner:
    """Deterministic adapter available only through explicit test dependency injection."""

    def __init__(self, result: dict[str, Any]) -> None:
        self.result = result

    def health(self) -> dict[str, Any]:
        return {"enabled": True, "provider": "test_stub", "status": "ready", "is_simulated": True}

    def infer(self, capture_id: str, input_path: Path) -> dict[str, Any]:
        label = self.result.get("predicted_label")
        if label not in APPROVED_CLASSES:
            raise ValueError(f"Unexpected model output class: {label}")
        return {"capture_id": capture_id, "provider": "test_stub", "is_simulated": True, **self.result}
