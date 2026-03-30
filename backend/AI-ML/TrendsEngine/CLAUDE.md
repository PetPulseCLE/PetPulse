# Pet Health Trends Engine - Engineering Guide

## Project Context

You are an AI coding agent responsible for building a scalable pet
health analytics engine.

- **Objective:** Replicate Apple Health "Trends" for pet health
  metrics (vitals, temperature, weight, activity).
- **Goal:** Transform 630,000+ raw data rows into meaningful 30-day
  health insights for 20+ pets.
- **Tech Stack:** Supabase (Postgres), Python 3.10+, Pandas, NumPy,
  Scikit-learn, FastAPI.
- **Frontend Target:** Expo (React Native).

## Data Source Structure (IMPORTANT)

**Table:** AIML_mock_metrics

**Columns:**
- pet_id (text/uuid)
- metric_type (text): vitals, activity, env, weight, raw_motion
- data (jsonb)
- recorded_at (timestamptz)

### Example JSON

**Vitals**

```json
{ "heartRate": 86.6, "breathRate": 12.7, "hr_confidence": 98 }
```

**Environment (temperature)**

```json
{ "temperature": 100.9, "humidity": 48.1 }
```

**Activity**

```json
{
  "classifier": {
    "accuracy": 3,
    "activityClass": 4,
    "confidenceArray": [0, 0, 0, 0, 97, 0, 0, 0, 0, 0]
  },
  "stepCount": { "accuracy": 3, "latency": 54588, "steps": 0 }
}
```

Activity class mapping (verify against classifier docs):
  0 = Unknown, 1 = Resting, 2 = Walking, 3 = Running,
  4 = Sleeping, 5 = Playing, 6 = Eating, 7 = Grooming,
  8 = Panting, 9 = Other.
Inactive classes (excluded from active count): 0, 1, 4.

**Weight** (not yet present in sample data)

```json
{ "target": 25, "current": 28.92 }
```

**Raw Motion**

```json
{
  "accel": { "x": -0.058, "y": -0.227, "z": -0.998, "accuracy": 3 },
  "gyro":  { "x": 8.519, "y": -21.803, "z": -5.910, "accuracy": 3 },
  "magf":  { "x": 23.877, "y": 22.998, "z": -64.307, "accuracy": 3 },
  "rv":    { "real": 0.707, "x": 0.0, "y": 0.0, "z": 0.707, "rad_accuracy": 0.01, "accuracy": 3 }
}
```

---

## Data Architecture & Aggregation (CRITICAL)

DO NOT load raw rows into Python.

### Materialized Views

Four materialized views pre-aggregate data per (pet_id, day):

| View               | Source metric_type | Key columns                    |
|--------------------|--------------------|--------------------------------|
| daily_vitals_mv    | vitals             | avg_hr, avg_br                 |
| daily_temp_mv      | env                | avg_temp, avg_humidity          |
| daily_weight_mv    | weight             | avg_weight, target_weight       |
| daily_activity_mv  | activity           | activity_pct, total_steps       |

### Example aggregation (vitals)

```sql
SELECT
    pet_id,
    DATE_TRUNC('day', recorded_at) AS observation_day,
    AVG((data->>'heartRate')::numeric) AS avg_hr,
    AVG((data->>'breathRate')::numeric) AS avg_br,
    STDDEV((data->>'heartRate')::numeric) AS stddev_hr,
    COUNT(*) AS total_readings
FROM "AIML_mock_metrics"
WHERE metric_type = 'vitals'
GROUP BY pet_id, observation_day
ORDER BY observation_day ASC;
```

### Rules

- Aggregate daily
- Filter by metric_type
- Use materialized views or caching

---

## Statistical Analysis

### Windows

- Baseline: Days 1--14
- Trend: Days 24--30

### Formula

Z = (current - μ) / σ

### Logic

- Activity declining: Z \< -1.5
- Vitals warning: \|Z\| \> 2.0 OR \>10% change

### Statuses

- improved
- declining
- stable
- warning

### ML

- IsolationForest on aggregated vitals (HR + BR)

---

## API Requirements

- Response time \<100ms
- Use cached/precomputed data

### JSON

```json
{
  "pet_id": "uuid",
  "trends": [
    {
      "metricKey": "heart_rate",
      "status": "improved",
      "displayString": "Example text",
      "uiColor": "#FF3B30",
      "percentageChange": 12.5,
      "sparklineData": [62, 64, 65, 68, 70]
    }
  ]
}
```

---

## Coding Standards

- Vectorized Pandas/NumPy only
- No loops
- Handle missing data with interpolation or forward fill

### Environment

- SUPABASE_URL
- SUPABASE_KEY

### Documentation

All functions must include docstrings explaining statistical logic.

---

## Principles

- Apple Health style insights
- Human-readable outputs
- Production-ready

---

## Do NOT

- Fetch raw large datasets
- Use loops
- Compute trends per request
- Ignore missing data

---

## Final Instruction

Act as a senior backend + data engineer.
