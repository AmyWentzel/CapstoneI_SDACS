from contextlib import asynccontextmanager
import asyncio
import logging

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .config import get_settings
from .capture_service import CaptureService
from .mqtt_client import SdacsMqttClient
from .routes import build_router
from .state_store import StateStore
from .websocket_manager import WebSocketManager


logging.basicConfig(level=logging.INFO)

settings = get_settings()
state_store = StateStore()
websocket_manager = WebSocketManager()
capture_service = CaptureService(settings)
mqtt_client = SdacsMqttClient(settings, state_store, websocket_manager, capture_service.observe)


@asynccontextmanager
async def lifespan(app: FastAPI):
    loop = asyncio.get_running_loop()
    capture_service.start(loop)
    mqtt_client.start(loop)
    try:
        yield
    finally:
        capture_service.stop()
        mqtt_client.stop()


app = FastAPI(title="SDACS API Backend", version="0.1.0", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)
app.include_router(build_router(settings, state_store, mqtt_client, websocket_manager, capture_service))
from .acoustic_map_fastapi import router as acoustic_map_router
app.include_router(acoustic_map_router)
