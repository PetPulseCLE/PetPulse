-- Daily activity aggregation materialized view.
-- Computes an activity percentage based on the classifier's activityClass.
--
-- Source columns: pet_id, metric_type, data (jsonb), recorded_at
-- Activity JSON:  {"classifier": {"activityClass": 4, ...}, "stepCount": {"steps": 0, ...}}
--
-- activityClass mapping (verify against your classifier documentation):
--   0 = Unknown,  1 = Resting,  2 = Walking,  3 = Running,
--   4 = Sleeping, 5 = Playing,  6 = Eating,   7 = Grooming,
--   8 = Panting,  9 = Other
-- Inactive classes (excluded from active count): 0, 1, 4
--
-- Refresh:
--   REFRESH MATERIALIZED VIEW CONCURRENTLY daily_activity_mv;

CREATE MATERIALIZED VIEW IF NOT EXISTS daily_activity_mv AS
SELECT
    pet_id,
    DATE_TRUNC('day', recorded_at)::date                AS observation_day,
    COUNT(*)                                            AS total_readings,
    SUM((data->'stepCount'->>'steps')::numeric)         AS total_steps,
    COUNT(*) FILTER (
        WHERE data->'classifier'->>'activityClass' IS NOT NULL
          AND (data->'classifier'->>'activityClass')::int NOT IN (0, 1, 4)
    )                                                   AS active_count,
    ROUND(
        COUNT(*) FILTER (
            WHERE data->'classifier'->>'activityClass' IS NOT NULL
              AND (data->'classifier'->>'activityClass')::int NOT IN (0, 1, 4)
        )::numeric
        / NULLIF(COUNT(*) FILTER (
            WHERE data->'classifier'->>'activityClass' IS NOT NULL
        ), 0) * 100,
        2
    )                                                   AS activity_pct
FROM   "AIML_mock_metrics"
WHERE  metric_type = 'activity'
GROUP  BY pet_id, DATE_TRUNC('day', recorded_at)
WITH DATA;

CREATE UNIQUE INDEX IF NOT EXISTS uix_daily_activity_pet_day
    ON daily_activity_mv (pet_id, observation_day);

CREATE INDEX IF NOT EXISTS idx_daily_activity_pet_day_desc
    ON daily_activity_mv (pet_id, observation_day DESC);
