-- Daily vitals aggregation materialized view.
-- Collapses raw vitals rows into one row per (pet_id, day).
-- Run against the Supabase SQL editor before starting the service.
--
-- Source columns: pet_id, metric_type, data (jsonb), recorded_at
-- Vitals JSON:    {"breathRate": 12.7, "heartRate": 86.6, "hr_confidence": 98}
--
-- Refresh (non-blocking, requires the unique index):
--   REFRESH MATERIALIZED VIEW CONCURRENTLY daily_vitals_mv;

CREATE MATERIALIZED VIEW IF NOT EXISTS daily_vitals_mv AS
SELECT
    pet_id,
    DATE_TRUNC('day', recorded_at)::date            AS observation_day,
    AVG((data->>'heartRate')::numeric)              AS avg_hr,
    AVG((data->>'breathRate')::numeric)             AS avg_br,
    STDDEV((data->>'heartRate')::numeric)           AS stddev_hr,
    STDDEV((data->>'breathRate')::numeric)          AS stddev_br,
    COUNT(*)                                        AS total_readings
FROM   "AIML_mock_metrics"
WHERE  metric_type = 'vitals'
  AND  data->>'heartRate'  IS NOT NULL
  AND  data->>'breathRate' IS NOT NULL
GROUP  BY pet_id, DATE_TRUNC('day', recorded_at)
WITH DATA;

CREATE UNIQUE INDEX IF NOT EXISTS uix_daily_vitals_pet_day
    ON daily_vitals_mv (pet_id, observation_day);

CREATE INDEX IF NOT EXISTS idx_daily_vitals_pet_day_desc
    ON daily_vitals_mv (pet_id, observation_day DESC);
