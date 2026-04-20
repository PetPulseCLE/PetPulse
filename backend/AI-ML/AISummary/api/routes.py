import logging

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from repository.pet_repository import fetch_health_snapshot
from services.ai_service import generate_summary


logger = logging.getLogger(__name__)

router = APIRouter()


class SummarizeRequest(BaseModel):
    pet_id: str


class SummarizeResponse(BaseModel):
    pet_id: str
    summary: str
    success: bool


@router.post("/summarize", response_model=SummarizeResponse)
async def summarize(request: SummarizeRequest):
    try:
        snapshot = await fetch_health_snapshot(request.pet_id)
        summary = await generate_summary(snapshot)
        return SummarizeResponse(
            pet_id=request.pet_id,
            summary=summary,
            success=True,
        )
    except HTTPException:
        raise
    except Exception as e:
        logger.exception(
            "Failed to summarize pet data for pet_id=%s", request.pet_id
        )
        raise HTTPException(
            status_code=500,
            detail="internal server error while summarizing pet data",
        ) from e
