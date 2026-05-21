import logging
from uuid import UUID

from fastapi import APIRouter, HTTPException

from models.schemas import TrendsResponse
from services.trends_service import compute_trends

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/trends", tags=["trends"])


@router.get("/{pet_id}", response_model=TrendsResponse)
def get_trends(pet_id: UUID) -> TrendsResponse:
    """
    Return 30-day health trends for a pet.

    Fetches ≤30 pre-aggregated rows per metric from materialized views,
    applies Z-score analysis (baseline days 1-14 vs trend days 24-30),
    and runs IsolationForest to mask anomalous vitals days.
    """
    try:
        return compute_trends(str(pet_id))
    except Exception as exc:
        logger.exception("Failed to compute trends for pet %s", pet_id)
        raise HTTPException(status_code=500, detail="Internal server error") from exc
