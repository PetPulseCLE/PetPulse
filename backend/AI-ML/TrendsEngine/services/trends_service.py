"""
trends_service.py — core analytics pipeline for 30-day pet health trends.

Pipeline overview:
  1. Fetch ≤30 pre-aggregated rows per metric from materialized views (no raw data).
  2. Align each metric to a dense 30-day grid; interpolate gaps.
  3. Run IsolationForest on aggregated vitals (HR + BR); null anomalous days before analysis.
  4. Apply Z-score classification:
       baseline  = days  1–14  (iloc[:14])
       trend win = days 24–30  (iloc[23:])
       Z = (trend_mean − baseline_mean) / baseline_std
  5. Return structured TrendsResponse ready for the Expo frontend.

Source data schema (per sample):
  Columns : pet_id, metric_type, data (jsonb), recorded_at
  Vitals  : {"heartRate": 86.6, "breathRate": 12.7, "hr_confidence": 98}
  Env     : {"temperature": 100.9, "humidity": 48.1}
  Activity: {"classifier": {"activityClass": 4, ...}, "stepCount": {"steps": 0, ...}}
"""

from datetime import date, timedelta

import numpy as np
import pandas as pd
from sklearn.ensemble import IsolationForest

from db.client import get_client
from models.schemas import TrendMetric, TrendsResponse

# ── Constants ─────────────────────────────────────────────────────────────────

STATUS_COLORS: dict[str, str] = {
    "improved":  "#34C759",  # Apple green
    "stable":    "#8E8E93",  # Apple gray
    "declining": "#FF9500",  # Apple orange
    "warning":   "#FF3B30",  # Apple red
}

METRIC_LABELS: dict[str, str] = {
    "heart_rate":     "Heart Rate",
    "breathing_rate": "Breathing Rate",
    "temperature":    "Temperature",
    "weight":         "Weight",
    "activity":       "Activity Level",
}


# ── Classification helpers ────────────────────────────────────────────────────

def _classify_vitals(z: float, pct_change: float) -> str:
    """
    Map Z-score and percentage change to a vitals status.

    Rules (CLAUDE.md):
      warning  → |Z| > 2.0  OR  |Δ%| > 10
      improved → Z > 1.0
      declining → Z < -1.0
      stable   → otherwise
    """
    if abs(z) >= 2.0 or abs(pct_change) >= 10.0:
        return "warning"
    if z > 1.0:
        return "improved"
    if z < -1.0:
        return "declining"
    return "stable"


def _classify_activity(z: float) -> str:
    """
    Map Z-score to an activity status.

    Rules (CLAUDE.md):
      declining → Z < -1.5
      improved  → Z > 1.0
      stable    → otherwise
    """
    if z <= -1.5:
        return "declining"
    if z > 1.0:
        return "improved"
    return "stable"


def _display_string(metric_key: str, status: str, pct_change: float) -> str:
    """Build a human-readable, Apple Health-style insight string."""
    label = METRIC_LABELS.get(metric_key, metric_key.replace("_", " ").title())
    abs_pct = abs(round(pct_change, 1))
    direction = "up" if pct_change >= 0 else "down"

    return {
        "warning":   f"{label} has shifted significantly ({direction} {abs_pct}%)",
        "improved":  f"{label} has been trending upward",
        "declining": f"{label} has been trending downward",
        "stable":    f"{label} is within a normal range",
    }[status]


# ── Data fetchers (hit materialized views only) ───────────────────────────────

def _fetch_vitals(pet_id: str, start: date, end: date) -> pd.DataFrame:
    """
    Fetch pre-aggregated daily vitals from daily_vitals_mv.
    Returns at most 30 rows; columns: observation_day, avg_hr, avg_br.
    """
    resp = (
        get_client()
        .table("daily_vitals_mv")
        .select("observation_day, avg_hr, avg_br")
        .eq("pet_id", pet_id)
        .gte("observation_day", start.isoformat())
        .lte("observation_day", end.isoformat())
        .order("observation_day")
        .execute()
    )
    if not resp.data:
        return pd.DataFrame(columns=["observation_day", "avg_hr", "avg_br"])

    df = pd.DataFrame(resp.data)
    df["observation_day"] = pd.to_datetime(df["observation_day"])
    return df.astype({"avg_hr": float, "avg_br": float})


def _fetch_temperature(pet_id: str, start: date, end: date) -> pd.DataFrame:
    """
    Fetch pre-aggregated daily temperature from daily_temp_mv (env metric).
    Returns at most 30 rows; columns: observation_day, avg_temp.
    """
    resp = (
        get_client()
        .table("daily_temp_mv")
        .select("observation_day, avg_temp")
        .eq("pet_id", pet_id)
        .gte("observation_day", start.isoformat())
        .lte("observation_day", end.isoformat())
        .order("observation_day")
        .execute()
    )
    if not resp.data:
        return pd.DataFrame(columns=["observation_day", "avg_temp"])

    df = pd.DataFrame(resp.data)
    df["observation_day"] = pd.to_datetime(df["observation_day"])
    return df.astype({"avg_temp": float})


def _fetch_weight(pet_id: str, start: date, end: date) -> pd.DataFrame:
    """
    Fetch pre-aggregated daily weight from daily_weight_mv.
    Returns at most 30 rows; columns: observation_day, avg_weight.
    """
    resp = (
        get_client()
        .table("daily_weight_mv")
        .select("observation_day, avg_weight")
        .eq("pet_id", pet_id)
        .gte("observation_day", start.isoformat())
        .lte("observation_day", end.isoformat())
        .order("observation_day")
        .execute()
    )
    if not resp.data:
        return pd.DataFrame(columns=["observation_day", "avg_weight"])

    df = pd.DataFrame(resp.data)
    df["observation_day"] = pd.to_datetime(df["observation_day"])
    return df.astype({"avg_weight": float})


def _fetch_activity(pet_id: str, start: date, end: date) -> pd.DataFrame:
    """
    Fetch pre-aggregated daily activity percentages from daily_activity_mv.
    Returns at most 30 rows; columns: observation_day, activity_pct.
    """
    resp = (
        get_client()
        .table("daily_activity_mv")
        .select("observation_day, activity_pct")
        .eq("pet_id", pet_id)
        .gte("observation_day", start.isoformat())
        .lte("observation_day", end.isoformat())
        .order("observation_day")
        .execute()
    )
    if not resp.data:
        return pd.DataFrame(columns=["observation_day", "activity_pct"])

    df = pd.DataFrame(resp.data)
    df["observation_day"] = pd.to_datetime(df["observation_day"])
    return df.astype({"activity_pct": float})


# ── Core analytics ────────────────────────────────────────────────────────────

def _align_to_window(df: pd.DataFrame, value_col: str, start: date, end: date) -> pd.Series:
    """
    Reindex a sparse daily Series onto a dense 30-day grid [start, end].

    Missing days (gaps in the materialized view) are filled via:
      1. Linear interpolation for interior gaps.
      2. Forward / backward fill for leading and trailing edges.

    Returns a 0-indexed Series of length 30.
    """
    full_index = pd.date_range(start, end, freq="D")
    aligned = (
        df.set_index("observation_day")[value_col]
        .reindex(full_index)
        .interpolate(method="linear")
        .ffill()
        .bfill()
    )
    return aligned.reset_index(drop=True)


def _detect_anomalies(vitals_df: pd.DataFrame) -> np.ndarray:
    """
    Flag anomalous days in aggregated vitals using IsolationForest.

    Trains on (avg_hr, avg_br) with contamination=0.05.
    Returns an array of shape (n,): 1 = normal, -1 = anomaly.
    Falls back to all-normal when fewer than 10 rows are available.
    """
    n = len(vitals_df)
    if n < 10:
        return np.ones(n, dtype=int)

    features = vitals_df[["avg_hr", "avg_br"]].to_numpy()
    return IsolationForest(
        contamination=0.05, random_state=42, n_jobs=-1
    ).fit_predict(features)


def _z_score_windows(series: pd.Series) -> tuple[float, float]:
    """
    Compute Z-score and percentage change between the baseline and trend windows.

    Baseline  : iloc[:14]   (days  1–14)
    Trend win : iloc[23:]   (days 24–30)

    Z = (trend_mean − baseline_mean) / baseline_std
    Returns (z_score, pct_change).  Returns (0.0, 0.0) on flat/empty baseline.
    """
    baseline   = series.iloc[:14]
    trend_win  = series.iloc[23:]

    baseline_mean = float(baseline.mean())
    baseline_std  = float(baseline.std())
    trend_mean    = float(trend_win.mean())

    if baseline_std == 0 or np.isnan(baseline_std):
        return 0.0, 0.0

    z   = (trend_mean - baseline_mean) / baseline_std
    pct = (trend_mean - baseline_mean) / baseline_mean * 100 if baseline_mean != 0 else 0.0
    return float(z), float(pct)


def _build_trend(
    series: pd.Series,
    metric_key: str,
    *,
    is_activity: bool = False,
) -> TrendMetric | None:
    """
    Build a TrendMetric from a fully-aligned 30-day Series.

    Returns None if the series is entirely NaN (no data available for metric).
    """
    if series.isna().all():
        return None

    z, pct = _z_score_windows(series)
    status = _classify_activity(z) if is_activity else _classify_vitals(z, pct)

    return TrendMetric(
        metricKey=metric_key,
        status=status,
        displayString=_display_string(metric_key, status, pct),
        uiColor=STATUS_COLORS[status],
        percentageChange=round(pct, 2),
        sparklineData=series.iloc[-7:].round(1).tolist(),
    )


# ── Public API ────────────────────────────────────────────────────────────────

def compute_trends(pet_id: str) -> TrendsResponse:
    """
    Compute 30-day health trends for a single pet.

    Steps:
      1. Build a 30-day window ending today.
      2. Fetch ≤30 aggregated rows per metric from materialized views.
      3. Align each series to a dense 30-day grid (interpolate missing days).
      4. Run IsolationForest on aggregated vitals (HR + BR); null anomalous
         days so they are treated as missing and filled by interpolation rather
         than skewing the Z-score calculation.
      5. Compute Z-score over baseline (days 1–14) vs trend (days 24–30).
      6. Classify status and assemble response.
    """
    end_date   = date.today()
    start_date = end_date - timedelta(days=29)

    vitals_df   = _fetch_vitals(pet_id, start_date, end_date)
    temp_df     = _fetch_temperature(pet_id, start_date, end_date)
    weight_df   = _fetch_weight(pet_id, start_date, end_date)
    activity_df = _fetch_activity(pet_id, start_date, end_date)

    trends: list[TrendMetric] = []

    # ── Vitals: HR, BR ────────────────────────────────────────────────────────
    if not vitals_df.empty:
        anomaly_mask = _detect_anomalies(vitals_df) == -1
        vitals_clean = vitals_df.copy()
        vitals_clean.loc[anomaly_mask, ["avg_hr", "avg_br"]] = np.nan

        for col, key in (
            ("avg_hr", "heart_rate"),
            ("avg_br", "breathing_rate"),
        ):
            series = _align_to_window(vitals_clean, col, start_date, end_date)
            trend  = _build_trend(series, key)
            if trend:
                trends.append(trend)

    # ── Temperature (from env) ────────────────────────────────────────────────
    if not temp_df.empty:
        series = _align_to_window(temp_df, "avg_temp", start_date, end_date)
        trend  = _build_trend(series, "temperature")
        if trend:
            trends.append(trend)

    # ── Weight ────────────────────────────────────────────────────────────────
    if not weight_df.empty:
        series = _align_to_window(weight_df, "avg_weight", start_date, end_date)
        trend  = _build_trend(series, "weight")
        if trend:
            trends.append(trend)

    # ── Activity ──────────────────────────────────────────────────────────────
    if not activity_df.empty:
        series = _align_to_window(activity_df, "activity_pct", start_date, end_date)
        trend  = _build_trend(series, "activity", is_activity=True)
        if trend:
            trends.append(trend)

    return TrendsResponse(
        pet_id=pet_id,
        computed_at=end_date.isoformat(),
        trends=trends,
    )
