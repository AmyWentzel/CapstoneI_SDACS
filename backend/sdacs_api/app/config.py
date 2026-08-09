"""SDACS backend module: Central backend configuration loaded from environment variables with deployment defaults for MQTT, API, paths, nodes, and Edge Impulse.

The module is part of the Raspberry Pi middleware/API layer used by the final SDACS system.
"""

from functools import lru_cache
import os
from pathlib import Path

from dotenv import load_dotenv
from pydantic import BaseModel, Field


load_dotenv()


class Settings(BaseModel):
    mqtt_host: str = Field(default_factory=lambda: os.getenv("SDACS_MQTT_HOST", "127.0.0.1"))
    mqtt_port: int = Field(default_factory=lambda: int(os.getenv("SDACS_MQTT_PORT", "1883")))
    api_host: str = Field(default_factory=lambda: os.getenv("SDACS_API_HOST", "0.0.0.0"))
    api_port: int = Field(default_factory=lambda: int(os.getenv("SDACS_API_PORT", "8000")))
    mqtt_client_id: str = "sdacs-fastapi-backend"
    command_topic_all: str = "sdacs/group/all/cmd"
    allowed_origins: list[str] = ["*"]
    capture_data_dir: Path = Field(
        default_factory=lambda: Path(os.getenv("SDACS_CAPTURE_DATA_DIR", "/home/kyledavid36/sdacs/captures"))
    )
    sqlite_path: Path = Field(
        default_factory=lambda: Path(os.getenv("SDACS_SQLITE_PATH", "/home/vortex/sdacs_logs/sdacs_telemetry.db"))
    )
    expected_nodes: list[str] = ["node01", "node02", "node03", "node04"]
    capture_completion_grace_seconds: int = Field(
        default_factory=lambda: int(os.getenv("SDACS_CAPTURE_COMPLETION_GRACE_SECONDS", "30"))
    )
    ei_enabled: bool = Field(
        default_factory=lambda: os.getenv("SDACS_EI_ENABLED", "false").lower() in {"1", "true", "yes"}
    )
    ei_runner_path: Path | None = Field(
        default_factory=lambda: Path(value) if (value := os.getenv("SDACS_EI_RUNNER_PATH")) else None
    )
    ei_timeout_seconds: int = Field(default_factory=lambda: int(os.getenv("SDACS_EI_TIMEOUT_SECONDS", "30")))
    ei_expected_project_id: int = Field(
        default_factory=lambda: int(os.getenv("SDACS_EI_EXPECTED_PROJECT_ID", "1071949"))
    )
    ei_expected_deploy_version: int = Field(
        default_factory=lambda: int(os.getenv("SDACS_EI_EXPECTED_DEPLOY_VERSION", "1"))
    )


@lru_cache
def get_settings() -> Settings:
    return Settings()

