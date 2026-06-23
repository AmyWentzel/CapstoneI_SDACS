from contextlib import asynccontextmanager
import asyncio
import logging

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .config import get_settings
from .mqtt_client import SdacsMqttClient
from .routes import build_router
from .state_store import StateStore
from .websocket_manager import WebSocketManager


logging.basicConfig(level=logging.INFO)

settings = get_settings()
state_store = StateStore()
websocket_manager = WebSocketManager()
mqtt_client = SdacsMqttClient(settings, state_store, websocket_manager)


@asynccontextmanager
async def lifespan(app: FastAPI):
    mqtt_client.start(asyncio.get_running_loop())
    try:
        yield
    finally:
        mqtt_client.stop()


app = FastAPI(title="SDACS API Backend", version="0.1.0", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)
app.include_router(build_router(settings, state_store, mqtt_client, websocket_manager))
