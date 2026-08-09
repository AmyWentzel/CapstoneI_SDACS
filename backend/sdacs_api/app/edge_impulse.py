"""SDACS backend module: Backend abstraction for validating and invoking the deployed Edge Impulse runner while preserving deterministic health/error reporting.

The module is part of the Raspberry Pi middleware/API layer used by the final SDACS system.
"""

from __future__ import annotations

import json
import math
import os
import subprocess
from pathlib import Path
from typing import Any, Mapping, Protocol

from .ai_features import (
    SDACS_V3_FEATURE_NAMES,
    SDACS_V3_FEATURE_PROVENANCE_ERROR,
    SDACS_V3_FEATURE_PROVENANCE_VERIFIED,
    SDACS_V3_FEATURE_SCHEMA,
)
from .config import Settings

APPROVED_CLASSES = ("noisy", "quiet_room_white_noise", "speech")
MODEL_PROJECT_ID = 1071949
MODEL_DEPLOY_VERSION = 1
MODEL_THRESHOLD = 0.60


class EdgeImpulseRunner(Protocol):
    def health(self) -> dict[str, Any]: ...
    def infer(self, capture_id: str, features: Mapping[str, Any]) -> dict[str, Any]: ...


def unavailable_result(capture_id: str, status: str, warning: str) -> dict[str, Any]:
    return {
        "capture_id": capture_id,
        "status": status,
        "ai_status": status,
        "ai_reason": warning,
        "top_label": None,
        "predicted_label": None,
        "display_label": None,
        "confidence": None,
        "accepted": None,
        "threshold": MODEL_THRESHOLD,
        "probabilities": {},
        "scores": {},
        "model": {
            "project_name": "SDACS_V3",
            "project_id": MODEL_PROJECT_ID,
            "impulse_id": 1,
            "deploy_version": MODEL_DEPLOY_VERSION,
            "feature_schema": SDACS_V3_FEATURE_SCHEMA,
        },
        "timing_ms": {},
        "warnings": [warning],
        "error": warning if status == "failed" else None,
    }


class ConfiguredEdgeImpulseRunner:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self.last_error: str | None = None
        self._health_verified = False

    def _executable_available(self) -> bool:
        path = self.settings.ei_runner_path
        return bool(path and path.is_file() and os.access(path, os.X_OK))

    def _run(self, arguments: list[str], *, input_text: str | None = None) -> dict[str, Any]:
        completed = subprocess.run(
            arguments,
            input=input_text,
            text=True,
            capture_output=True,
            timeout=self.settings.ei_timeout_seconds,
            check=False,
            shell=False,
        )
        if completed.returncode:
            detail = completed.stderr.strip() or f"runner exited {completed.returncode}"
            raise RuntimeError(detail[:1000])
        try:
            value = json.loads(completed.stdout)
        except json.JSONDecodeError as exc:
            raise RuntimeError("runner returned invalid JSON") from exc
        if not isinstance(value, dict):
            raise RuntimeError("runner response must be a JSON object")
        return value

    def health(self) -> dict[str, Any]:
        path = self.settings.ei_runner_path
        response: dict[str, Any] = {
            "enabled": self.settings.ei_enabled,
            "status": "disabled",
            "runner_path_configured": path is not None,
            "executable_exists": self._executable_available(),
            "feature_provenance_verified": SDACS_V3_FEATURE_PROVENANCE_VERIFIED,
            "project_name": "SDACS_V3",
            "project_id": MODEL_PROJECT_ID,
            "deploy_version": MODEL_DEPLOY_VERSION,
            "labels": list(APPROVED_CLASSES),
            "input_feature_count": len(SDACS_V3_FEATURE_NAMES),
            "last_error": self.last_error,
            "is_simulated": False,
        }
        if not self.settings.ei_enabled:
            return response
        if not SDACS_V3_FEATURE_PROVENANCE_VERIFIED:
            response.update(status="feature_provenance_unverified", last_error=SDACS_V3_FEATURE_PROVENANCE_ERROR)
            return response
        if not self._executable_available():
            response.update(status="runner_unavailable", last_error="Configured runner is not executable.")
            return response
        try:
            runner = self._run([str(path), "--health"])
            if (
                runner.get("status") != "ready"
                or runner.get("project_id") != self.settings.ei_expected_project_id
                or runner.get("deploy_version") != self.settings.ei_expected_deploy_version
                or runner.get("labels") != list(APPROVED_CLASSES)
                or runner.get("feature_count") != len(SDACS_V3_FEATURE_NAMES)
            ):
                raise RuntimeError("runner health metadata mismatch")
            response.update(runner)
            self.last_error = None
            self._health_verified = True
        except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
            self.last_error = str(exc)
            self._health_verified = False
            response.update(status="failed", last_error=self.last_error)
        return response

    def infer(self, capture_id: str, features: Mapping[str, Any]) -> dict[str, Any]:
        health = {"status": "ready", "last_error": None} if self._health_verified else self.health()
        if health["status"] != "ready":
            return unavailable_result(capture_id, health["status"], health["last_error"] or "AI unavailable.")
        ordered: list[float] = []
        for name in SDACS_V3_FEATURE_NAMES:
            value = features.get(name)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(float(value)):
                raise ValueError(f"Missing or non-finite AI feature: {name}")
            ordered.append(float(value))
        payload = json.dumps({"feature_names": list(SDACS_V3_FEATURE_NAMES), "features": ordered}, allow_nan=False)
        try:
            result = self._run([str(self.settings.ei_runner_path)], input_text=payload)
            return self._validate_result(capture_id, result)
        except (OSError, subprocess.TimeoutExpired, RuntimeError, ValueError) as exc:
            self.last_error = str(exc)
            return unavailable_result(capture_id, "failed", self.last_error)

    def _validate_result(self, capture_id: str, result: dict[str, Any]) -> dict[str, Any]:
        model = result.get("model")
        probabilities = result.get("probabilities")
        if not isinstance(model, dict) or not isinstance(probabilities, dict):
            raise ValueError("runner result is missing model or probabilities")
        if model.get("project_id") != self.settings.ei_expected_project_id:
            raise ValueError("unexpected model project ID")
        if model.get("deploy_version") != self.settings.ei_expected_deploy_version:
            raise ValueError("unexpected model deployment version")
        if set(probabilities) != set(APPROVED_CLASSES):
            raise ValueError("runner returned unexpected labels")
        parsed: dict[str, float] = {}
        for label in APPROVED_CLASSES:
            value = probabilities[label]
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("runner returned a non-numeric probability")
            probability = float(value)
            if not math.isfinite(probability) or not 0 <= probability <= 1:
                raise ValueError("runner returned an invalid probability")
            parsed[label] = probability
        top_label = max(parsed, key=parsed.get)
        confidence = parsed[top_label]
        if result.get("top_label") != top_label:
            raise ValueError("runner top label does not match maximum probability")
        accepted = confidence >= MODEL_THRESHOLD
        return {
            "capture_id": capture_id,
            "status": "complete",
            "top_label": top_label,
            "predicted_label": top_label,
            "display_label": {
                "noisy": "Noisy",
                "quiet_room_white_noise": "Quiet Room / White Noise",
                "speech": "Speech",
            }[top_label] if accepted else "Uncertain",
            "confidence": confidence,
            "accepted": accepted,
            "threshold": MODEL_THRESHOLD,
            "probabilities": parsed,
            "scores": parsed,
            "model": {**model, "feature_schema": SDACS_V3_FEATURE_SCHEMA},
            "timing_ms": result.get("timing_ms") if isinstance(result.get("timing_ms"), dict) else {},
            "warnings": [],
            "error": None,
        }


class TestStubEdgeImpulseRunner:
    __test__ = False

    def __init__(self, result: dict[str, Any]) -> None:
        self.result = result

    def health(self) -> dict[str, Any]:
        return {"enabled": True, "status": "ready", "is_simulated": True}

    def infer(self, capture_id: str, features: Mapping[str, Any]) -> dict[str, Any]:
        label = self.result.get("top_label", self.result.get("predicted_label"))
        if label not in APPROVED_CLASSES:
            raise ValueError(f"Unexpected model output class: {label}")
        return {
            "capture_id": capture_id,
            "provider": "test_stub",
            "is_simulated": True,
            **self.result,
        }
