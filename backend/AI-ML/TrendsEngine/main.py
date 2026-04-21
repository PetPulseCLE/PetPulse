import os
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from db.client import get_client
from routers.trends import router as trends_router

_allowed_origins = [
    stripped
    for o in os.getenv("CORS_ALLOWED_ORIGINS", "").split(",")
    if (stripped := o.strip()) and stripped.startswith(("http://", "https://"))
]


@asynccontextmanager
async def lifespan(_app: FastAPI) -> AsyncIterator[None]:
    """Initialize external clients at boot so config issues fail fast."""
    get_client()
    yield


app = FastAPI(
    title="PetPulse Trends Engine",
    description="30-day pet health trend analysis: vitals, temperature, weight, and activity.",
    version="1.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=_allowed_origins,
    allow_methods=["GET"],
    allow_headers=["*"],
)

app.include_router(trends_router)


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}
