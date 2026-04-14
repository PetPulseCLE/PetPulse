from pydantic import BaseModel


class TodayMetrics(BaseModel):
    avg_hr: float | None = None
    avg_br: float | None = None
    stddev_hr: float | None = None
    stddev_br: float | None = None
    total_steps: int | None = None
    activity_pct: float | None = None
    avg_weight: float | None = None
    target_weight: float | None = None


class BaselineMetrics(BaseModel):
    avg_hr: float | None = None
    avg_br: float | None = None
    avg_steps: float | None = None


class HealthSnapshot(BaseModel):
    pet_id: str
    today: TodayMetrics
    baseline: BaselineMetrics
