from datetime import date
from typing import Literal

from pydantic import BaseModel

TrendStatus = Literal["improved", "declining", "stable", "warning"]


class TrendMetric(BaseModel):
    metricKey: str
    status: TrendStatus
    displayString: str
    uiColor: str
    percentageChange: float
    sparklineData: list[float]


class TrendsResponse(BaseModel):
    pet_id: str
    computed_at: date
    trends: list[TrendMetric]
