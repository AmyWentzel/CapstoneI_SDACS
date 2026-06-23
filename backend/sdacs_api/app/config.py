from functools import lru_cache
import os

from dotenv import load_dotenv
from pydantic import BaseModel, Field


load_dotenv()


class Settings(BaseModel):
    mqtt_host: str = Field(default_factory=lambda: os.getenv("SDACS_MQTT_HOST", "192.168.5.40"))
    mqtt_port: int = Field(default_factory=lambda: int(os.getenv("SDACS_MQTT_PORT", "1883")))
    api_host: str = Field(default_factory=lambda: os.getenv("SDACS_API_HOST", "0.0.0.0"))
    api_port: int = Field(default_factory=lambda: int(os.getenv("SDACS_API_PORT", "8000")))
    mqtt_client_id: str = "sdacs-fastapi-backend"
    command_topic_all: str = "sdacs/group/all/cmd"
    allowed_origins: list[str] = ["*"]


@lru_cache
def get_settings() -> Settings:
    return Settings()

