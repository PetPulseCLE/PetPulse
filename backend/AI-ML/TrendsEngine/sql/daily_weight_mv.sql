-- Daily weight aggregation materialized view.
-- Tracks average current weight and target weight per pet per day.
--
-- NOTE: The sample data does not include a "weight" metric_type yet.
-- This view is kept so the pipeline handles weight data as soon as it
-- becomes available.  It will simply return zero rows until then.
--
-- Source columns: pet_id, metric_type, data (jsonb), recorded_at
-- Expected weight JSON: {"current": 28.92, "target": 25}
--
-- Refresh:
--   REFRESH MATERIALIZED VIEW CONCURRENTLY daily_weight_mv;

CREATE MATERIALIZED VIEW IF NOT EXISTS daily_weight_mv AS
SELECT
    pet_id,
    DATE_TRUNC('day', recorded_at)::date   AS observation_day,
    AVG((data->>'current')::numeric)       AS avg_weight,
    MAX((data->>'target')::numeric)        AS target_weight,
    COUNT(*)                               AS total_readings
FROM   "AIML_mock_metrics"
WHERE  metric_type = 'weight'
  AND  data->>'current' IS NOT NULL
GROUP  BY pet_id, DATE_TRUNC('day', recorded_at)
WITH DATA;

CREATE UNIQUE INDEX IF NOT EXISTS uix_daily_weight_pet_day
    ON daily_weight_mv (pet_id, observation_day);

CREATE INDEX IF NOT EXISTS idx_daily_weight_pet_day_desc
    ON daily_weight_mv (pet_id, observation_day DESC);
