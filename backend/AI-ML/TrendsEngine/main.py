import os

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from db.client import get_client
from routers.trends import router as trends_router

_allowed_origins = [
    stripped
    for o in os.getenv("CORS_ALLOWED_ORIGINS", "").split(",")
    if (stripped := o.strip()) and stripped.startswith(("http://", "https://"))
]

app = FastAPI(
    title="PetPulse Trends Engine",
    description="30-day pet health trend analysis: vitals, temperature, weight, and activity.",
    version="1.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=_allowed_origins,
    allow_methods=["GET"],
    allow_headers=["*"],
)

app.include_router(trends_router)


@app.on_event("startup")
def startup() -> None:
    """Initialize external clients at boot so config issues fail fast."""
    get_client()


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}
