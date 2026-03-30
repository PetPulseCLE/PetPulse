-- Daily temperature aggregation materialized view.
-- Pulls environmental temperature from the "env" metric type.
--
-- Source columns: pet_id, metric_type, data (jsonb), recorded_at
-- Env JSON:       {"temperature": 100.9, "humidity": 48.1}
--
-- Refresh:
--   REFRESH MATERIALIZED VIEW CONCURRENTLY daily_temp_mv;

CREATE MATERIALIZED VIEW IF NOT EXISTS daily_temp_mv AS
SELECT
    pet_id,
    DATE_TRUNC('day', recorded_at)::date        AS observation_day,
    AVG((data->>'temperature')::numeric)        AS avg_temp,
    AVG((data->>'humidity')::numeric)           AS avg_humidity,
    STDDEV((data->>'temperature')::numeric)     AS stddev_temp,
    COUNT(*)                                    AS total_readings
FROM   "AIML_mock_metrics"
WHERE  metric_type = 'env'
  AND  data->>'temperature' IS NOT NULL
GROUP  BY pet_id, DATE_TRUNC('day', recorded_at)
WITH DATA;

CREATE UNIQUE INDEX IF NOT EXISTS uix_daily_temp_pet_day
    ON daily_temp_mv (pet_id, observation_day);

CREATE INDEX IF NOT EXISTS idx_daily_temp_pet_day_desc
    ON daily_temp_mv (pet_id, observation_day DESC);
