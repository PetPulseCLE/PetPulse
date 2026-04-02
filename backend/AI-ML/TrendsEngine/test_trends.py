"""
test_trends.py — offline unit tests for the TrendsEngine analytics pipeline.

These tests mock all Supabase calls so no database connection is needed.
Run from the TrendsEngine directory with the venv active:

    python -m pytest test_trends.py -v

Covers:
  - _classify_vitals / _classify_activity boundary conditions
  - _z_score_windows math
  - _align_to_window gap-filling / interpolation
  - _detect_anomalies (normal path + short-series fallback)
  - _build_trend (full metric + all-NaN guard)
  - compute_trends end-to-end with mocked Supabase responses
"""

from datetime import date, timedelta
from unittest.mock import MagicMock, patch

import numpy as np
import pandas as pd
import pytest

# ── Import the module under test ───────────────────────────────────────────────
from services.trends_service import (
    _align_to_window,
    _build_trend,
    _classify_activity,
    _classify_vitals,
    _detect_anomalies,
    _display_string,
    _z_score_windows,
    compute_trends,
)


# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

def _make_daily_df(col: str, values: list[float], end: date | None = None) -> pd.DataFrame:
    """Build a DataFrame that looks like a materialized-view response."""
    if end is None:
        end = date.today()
    start = end - timedelta(days=len(values) - 1)
    days = pd.date_range(start, periods=len(values), freq="D")
    return pd.DataFrame({"observation_day": days, col: values})


def _flat(n: int = 30, value: float = 70.0) -> list[float]:
    return [value] * n


def _rising(n: int = 30, start: float = 60.0, step: float = 1.0) -> list[float]:
    return [start + i * step for i in range(n)]


def _declining(n: int = 30, start: float = 90.0, step: float = 1.5) -> list[float]:
    return [start - i * step for i in range(n)]


# ─────────────────────────────────────────────────────────────────────────────
# _classify_vitals
# ─────────────────────────────────────────────────────────────────────────────

class TestClassifyVitals:
    def test_warning_on_high_z(self):
        assert _classify_vitals(2.5, 5.0) == "warning"

    def test_warning_on_high_pct_change(self):
        assert _classify_vitals(0.5, 11.0) == "warning"

    def test_improved_on_positive_z(self):
        assert _classify_vitals(1.5, 5.0) == "improved"

    def test_declining_on_negative_z(self):
        assert _classify_vitals(-1.5, -5.0) == "declining"

    def test_stable_in_normal_range(self):
        assert _classify_vitals(0.3, 2.0) == "stable"

    def test_boundary_z_exactly_two(self):
        # |Z| == 2.0 is still warning
        assert _classify_vitals(2.0, 0.0) == "warning"

    def test_boundary_pct_exactly_ten(self):
        # |Δ%| == 10 is still warning
        assert _classify_vitals(0.0, 10.0) == "warning"


# ─────────────────────────────────────────────────────────────────────────────
# _classify_activity
# ─────────────────────────────────────────────────────────────────────────────

class TestClassifyActivity:
    def test_declining_below_threshold(self):
        assert _classify_activity(-2.0) == "declining"

    def test_improved_above_threshold(self):
        assert _classify_activity(1.5) == "improved"

    def test_stable_in_middle(self):
        assert _classify_activity(0.0) == "stable"

    def test_boundary_exactly_minus_1_5(self):
        assert _classify_activity(-1.5) == "declining"


# ─────────────────────────────────────────────────────────────────────────────
# _z_score_windows
# ─────────────────────────────────────────────────────────────────────────────

class TestZScoreWindows:
    def test_flat_series_returns_zero(self):
        series = pd.Series(_flat(30, 70.0))
        z, pct = _z_score_windows(series)
        assert z == 0.0
        assert pct == 0.0

    def test_rising_series_positive_z(self):
        series = pd.Series(_rising(30, start=60.0, step=1.0))
        z, pct = _z_score_windows(series)
        assert z > 0, "Rising series should yield positive Z"
        assert pct > 0, "Rising series should yield positive pct change"

    def test_declining_series_negative_z(self):
        series = pd.Series(_declining(30, start=90.0, step=1.5))
        z, pct = _z_score_windows(series)
        assert z < 0, "Declining series should yield negative Z"

    def test_all_nan_baseline_returns_zero(self):
        series = pd.Series([float("nan")] * 30)
        z, pct = _z_score_windows(series)
        assert z == 0.0
        assert pct == 0.0


# ─────────────────────────────────────────────────────────────────────────────
# _align_to_window
# ─────────────────────────────────────────────────────────────────────────────

class TestAlignToWindow:
    def test_dense_30_day_output(self):
        end = date.today()
        start = end - timedelta(days=29)
        df = _make_daily_df("avg_hr", _flat(30, 75.0), end=end)
        result = _align_to_window(df, "avg_hr", start, end)
        assert len(result) == 30

    def test_gap_is_interpolated(self):
        """A missing day in the middle should be linearly interpolated."""
        end = date.today()
        start = end - timedelta(days=29)
        values = _flat(30, 70.0)
        df = _make_daily_df("avg_hr", values, end=end)
        # Remove the middle row to create a gap
        df = df.drop(index=14).reset_index(drop=True)
        result = _align_to_window(df, "avg_hr", start, end)
        assert len(result) == 30
        assert not result.isna().any(), "No NaNs should remain after interpolation"

    def test_sparse_data_fills_edges(self):
        """Only a few rows present — ffill/bfill should cover the whole window."""
        end = date.today()
        start = end - timedelta(days=29)
        # Only 5 rows at the end of the window
        values = [80.0] * 5
        df = _make_daily_df("avg_hr", values, end=end)
        result = _align_to_window(df, "avg_hr", start, end)
        assert len(result) == 30
        assert not result.isna().any()


# ─────────────────────────────────────────────────────────────────────────────
# _detect_anomalies
# ─────────────────────────────────────────────────────────────────────────────

class TestDetectAnomalies:
    def test_short_df_returns_all_normal(self):
        df = pd.DataFrame({"avg_hr": [70.0] * 5, "avg_br": [15.0] * 5})
        result = _detect_anomalies(df)
        assert (result == 1).all()

    def test_normal_data_mostly_inliers(self):
        rng = np.random.default_rng(0)
        hr = rng.normal(75, 3, 30)
        br = rng.normal(15, 1, 30)
        df = pd.DataFrame({"avg_hr": hr, "avg_br": br})
        result = _detect_anomalies(df)
        # With contamination=0.05 on 30 rows, at most ~2 should be flagged
        assert (result == -1).sum() <= 3

    def test_obvious_outlier_flagged(self):
        hr = [75.0] * 29 + [200.0]   # last point is wildly abnormal
        br = [15.0] * 29 + [60.0]
        df = pd.DataFrame({"avg_hr": hr, "avg_br": br})
        result = _detect_anomalies(df)
        assert result[-1] == -1, "Obvious outlier should be flagged as anomaly"


# ─────────────────────────────────────────────────────────────────────────────
# _build_trend
# ─────────────────────────────────────────────────────────────────────────────

class TestBuildTrend:
    def test_all_nan_returns_none(self):
        series = pd.Series([float("nan")] * 30)
        assert _build_trend(series, "heart_rate") is None

    def test_stable_metric_structure(self):
        series = pd.Series(_flat(30, 70.0))
        trend = _build_trend(series, "heart_rate")
        assert trend is not None
        assert trend.metricKey == "heart_rate"
        assert trend.status in ("improved", "declining", "stable", "warning")
        assert len(trend.sparklineData) == 7
        assert trend.uiColor.startswith("#")

    def test_improving_series_status(self):
        series = pd.Series(_rising(30, start=60.0, step=1.5))
        trend = _build_trend(series, "heart_rate")
        assert trend is not None
        assert trend.status in ("improved", "warning")

    def test_activity_flag_uses_activity_classifier(self):
        series = pd.Series(_declining(30, start=80.0, step=2.0))
        trend = _build_trend(series, "activity", is_activity=True)
        assert trend is not None
        assert trend.status in ("declining", "stable", "improved")

    def test_sparkline_is_last_7_days(self):
        values = list(range(30))
        series = pd.Series([float(v) for v in values])
        trend = _build_trend(series, "heart_rate")
        assert trend is not None
        assert trend.sparklineData == [round(float(v), 1) for v in values[-7:]]


# ─────────────────────────────────────────────────────────────────────────────
# _display_string
# ─────────────────────────────────────────────────────────────────────────────

class TestDisplayString:
    def test_all_statuses_produce_strings(self):
        for status in ("improved", "declining", "stable", "warning"):
            result = _display_string("heart_rate", status, 5.0)
            assert isinstance(result, str) and len(result) > 0

    def test_warning_includes_direction(self):
        result = _display_string("heart_rate", "warning", 12.0)
        assert "up" in result

    def test_warning_negative_direction(self):
        result = _display_string("heart_rate", "warning", -12.0)
        assert "down" in result

    def test_unknown_metric_key_falls_back(self):
        result = _display_string("custom_metric", "stable", 0.0)
        assert "Custom Metric" in result


# ─────────────────────────────────────────────────────────────────────────────
# compute_trends — end-to-end with mocked Supabase
# ─────────────────────────────────────────────────────────────────────────────

def _mock_supabase_response(table_data: dict[str, list[dict]]) -> MagicMock:
    """Build a fake Supabase client that dispatches responses by table name."""
    client = MagicMock()

    def table_side_effect(table_name: str) -> MagicMock:
        if table_name not in table_data:
            raise AssertionError(f"Unexpected table queried: {table_name}")
        chain = MagicMock()
        resp = MagicMock()
        resp.data = table_data[table_name]
        chain.select.return_value = chain
        chain.eq.return_value = chain
        chain.gte.return_value = chain
        chain.lte.return_value = chain
        chain.order.return_value = chain
        chain.execute.return_value = resp
        return chain

    client.table.side_effect = table_side_effect
    return client


def _build_vitals_rows(pet_id: str, n: int = 30) -> list[dict]:
    end = date.today()
    rows = []
    for i in range(n):
        day = end - timedelta(days=n - 1 - i)
        rows.append({
            "observation_day": day.isoformat(),
            "avg_hr": 75.0 + i * 0.2,
            "avg_br": 15.0 + i * 0.05,
        })
    return rows


def _build_temp_rows(pet_id: str, n: int = 30) -> list[dict]:
    end = date.today()
    return [
        {
            "observation_day": (end - timedelta(days=n - 1 - i)).isoformat(),
            "avg_temp": 101.0 + i * 0.01,
        }
        for i in range(n)
    ]


def _build_activity_rows(pet_id: str, n: int = 30) -> list[dict]:
    end = date.today()
    return [
        {
            "observation_day": (end - timedelta(days=n - 1 - i)).isoformat(),
            "activity_pct": 40.0 + i * 0.5,
        }
        for i in range(n)
    ]


PET_ID = "5835cc7d-a287-4735-b008-399fcb2999bf"


class TestComputeTrendsEndToEnd:

    def _run_with_mock(self, vitals=None, temp=None, weight=None, activity=None):
        vitals_rows   = vitals   if vitals   is not None else _build_vitals_rows(PET_ID)
        temp_rows     = temp     if temp     is not None else _build_temp_rows(PET_ID)
        weight_rows   = weight   if weight   is not None else []
        activity_rows = activity if activity is not None else _build_activity_rows(PET_ID)

        table_data = {
            "daily_vitals_mv":   vitals_rows,
            "daily_temp_mv":     temp_rows,
            "daily_weight_mv":   weight_rows,
            "daily_activity_mv": activity_rows,
        }
        mock_client = _mock_supabase_response(table_data)

        with patch("services.trends_service.get_client", return_value=mock_client):
            return compute_trends(PET_ID)

    def test_response_has_correct_pet_id(self):
        result = self._run_with_mock()
        assert result.pet_id == PET_ID

    def test_response_has_computed_at(self):
        result = self._run_with_mock()
        assert result.computed_at == date.today()

    def test_trends_list_is_not_empty(self):
        result = self._run_with_mock()
        assert len(result.trends) > 0

    def test_all_metric_keys_present(self):
        result = self._run_with_mock()
        keys = {t.metricKey for t in result.trends}
        # With full data, HR, BR, temp, and activity should all appear
        assert "heart_rate"     in keys
        assert "breathing_rate" in keys
        assert "temperature"    in keys
        assert "activity"       in keys

    def test_no_weight_when_empty(self):
        result = self._run_with_mock(weight=[])
        keys = {t.metricKey for t in result.trends}
        assert "weight" not in keys

    def test_all_statuses_are_valid(self):
        result = self._run_with_mock()
        valid = {"improved", "declining", "stable", "warning"}
        for t in result.trends:
            assert t.status in valid

    def test_sparkline_length_is_7(self):
        result = self._run_with_mock()
        for t in result.trends:
            assert len(t.sparklineData) == 7, f"{t.metricKey} sparkline should have 7 points"

    def test_all_data_missing_returns_empty_trends(self):
        result = self._run_with_mock(vitals=[], temp=[], weight=[], activity=[])
        assert result.trends == []

    def test_percentage_change_is_float(self):
        result = self._run_with_mock()
        for t in result.trends:
            assert isinstance(t.percentageChange, float)

    def test_ui_colors_are_hex(self):
        result = self._run_with_mock()
        for t in result.trends:
            assert t.uiColor.startswith("#"), f"Expected hex color, got {t.uiColor}"
            assert len(t.uiColor) == 7
