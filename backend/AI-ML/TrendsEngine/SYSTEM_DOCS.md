# PetPulse Trends Engine — System Documentation

> **Generated:** 2026-04-02
> **Audience:** Engineers onboarding to the AI/ML backend

---

## High-Level Project Overview

The Trends Engine is a self-contained **FastAPI microservice** that transforms 30 days of
pre-aggregated pet health data into Apple Health-style trend insights for the Expo (React
Native) frontend.

| Attribute        | Value                                          |
|------------------|------------------------------------------------|
| Framework        | FastAPI 0.110 + Uvicorn                        |
| Language         | Python 3.10+                                   |
| Data layer       | Supabase (Postgres) via `supabase-py` SDK      |
| Data access mode | Server-side SDK only — no Edge Functions       |
| ML               | scikit-learn `IsolationForest`                 |
| Data wrangling   | Pandas 2.2 + NumPy 2.4                         |
| Response format  | JSON (Pydantic v2 models)                      |
| Auth model       | None on service boundary (see §4)              |
| RLS policies     | ⚠️ None defined in this repository             |

**Core design principle:** raw sensor rows (`AIML_mock_metrics`) are **never** loaded into
Python. Four Postgres materialized views pre-aggregate them to one row per
`(pet_id, day)`, keeping each API response well under the 100 ms target.

---

## Directory Structure & Component Breakdown

```
TrendsEngine/
├── main.py                   # FastAPI app factory, CORS, startup hook
├── requirements.txt          # Pinned dependencies
├── test_trends.py            # Offline unit + integration tests (mocked Supabase)
│
├── routers/
│   └── trends.py             # GET /trends/{pet_id} — request validation & dispatch
│
├── services/
│   └── trends_service.py     # Full analytics pipeline (fetch → align → detect → classify)
│
├── models/
│   └── schemas.py            # Pydantic response models: TrendMetric, TrendsResponse
│
├── db/
│   └── client.py             # Singleton supabase-py client; reads env vars at startup
│
└── sql/
    ├── daily_vitals_mv.sql   # CREATE MATERIALIZED VIEW daily_vitals_mv
    ├── daily_temp_mv.sql     # CREATE MATERIALIZED VIEW daily_temp_mv
    ├── daily_weight_mv.sql   # CREATE MATERIALIZED VIEW daily_weight_mv
    └── daily_activity_mv.sql # CREATE MATERIALIZED VIEW daily_activity_mv
```

> **Note:** There is no `supabase/` CLI project directory, no `migrations/` folder, and
> no `.env.example` in this repository. All DDL is applied manually via the Supabase SQL
> Editor using the files in `sql/`.

### Component Responsibilities

| Component | Responsibility |
|-----------|---------------|
| `main.py` | Mounts the `trends` router, configures CORS from `CORS_ALLOWED_ORIGINS`, calls `get_client()` at startup to fail fast on bad credentials |
| `routers/trends.py` | Validates `pet_id` as a UUID, delegates to `compute_trends()`, wraps all errors in a 500 HTTPException |
| `services/trends_service.py` | Fetches aggregated data from 4 materialized views, aligns each metric to a dense 30-day grid, runs IsolationForest on vitals, computes Z-scores, classifies statuses, builds `TrendsResponse` |
| `models/schemas.py` | `TrendMetric` (one metric row) + `TrendsResponse` (envelope with `pet_id`, `computed_at`, `trends[]`) |
| `db/client.py` | Lazy singleton using `supabase-py` `create_client()`; searches for `.env` in the TrendsEngine root then three levels up at the backend root; accepts `SUPABASE_*` or `EXPO_PUBLIC_SUPABASE_*` var names |
| `sql/*.sql` | DDL-only — run once in Supabase SQL Editor to create materialized views and indexes; refresh with `REFRESH MATERIALIZED VIEW CONCURRENTLY <name>` |

---

## Relational System Map

### Data Access Method

All database access in this service is **server-side only** via the `supabase-py` SDK.
There are no Supabase Edge Functions and no frontend client queries in this codebase.

```
Expo Frontend
    │
    │  GET /trends/{pet_id}  (HTTP — no auth token forwarded)
    ▼
FastAPI  (this service)
    │
    │  supabase-py SDK  →  Supabase PostgREST API
    │  authenticated with SUPABASE_KEY (anon or service_role)
    ▼
Supabase Postgres
    ├── daily_vitals_mv
    ├── daily_temp_mv
    ├── daily_weight_mv
    └── daily_activity_mv
         └── (all read from) AIML_mock_metrics
```

### Endpoint Map

| Endpoint | Method | Handler | DB Objects Read | Data Access Method | RLS / Access Control |
|----------|--------|---------|-----------------|-------------------|----------------------|
| `/health` | GET | `main.health()` | — | None | None |
| `/trends/{pet_id}` | GET | `routers/trends.get_trends()` → `compute_trends()` | `daily_vitals_mv`, `daily_temp_mv`, `daily_weight_mv`, `daily_activity_mv` | `supabase-py` SDK (server-side) | ⚠️ **No RLS defined** — access controlled only by which Supabase key is configured (see §4) |

### Materialized View → Source Table Map

| Materialized View | Source `metric_type` | Source Table | Key Output Columns | RLS Policy |
|-------------------|---------------------|--------------|-------------------|------------|
| `daily_vitals_mv` | `vitals` | `AIML_mock_metrics` | `avg_hr`, `avg_br`, `stddev_hr`, `stddev_br`, `total_readings` | ⚠️ None defined |
| `daily_temp_mv` | `env` | `AIML_mock_metrics` | `avg_temp`, `avg_humidity`, `stddev_temp`, `total_readings` | ⚠️ None defined |
| `daily_weight_mv` | `weight` | `AIML_mock_metrics` | `avg_weight`, `target_weight`, `total_readings` | ⚠️ None defined |
| `daily_activity_mv` | `activity` | `AIML_mock_metrics` | `activity_pct`, `total_steps`, `active_count`, `total_readings` | ⚠️ None defined |
| `AIML_mock_metrics` | (source table) | — | All raw sensor rows | ⚠️ None defined |

All views group by `(pet_id, DATE_TRUNC('day', recorded_at))` and carry a
`UNIQUE INDEX ON (pet_id, observation_day)` enabling `REFRESH … CONCURRENTLY`.

> **Important:** Postgres materialized views do **not** inherit RLS from their source
> table. Each view requires its own `ENABLE ROW LEVEL SECURITY` + `CREATE POLICY`
> statements to restrict access by `pet_id`.

### Analytics Pipeline Detail

| Stage | Function | Input | Output |
|-------|----------|-------|--------|
| 1. Fetch | `_fetch_vitals/temp/weight/activity` | `pet_id`, 30-day date range | Sparse DataFrame (≤30 rows) |
| 2. Align | `_align_to_window` | Sparse DataFrame | Dense `pd.Series` of exactly 30 values |
| 3. Anomaly detection | `_detect_anomalies` | `vitals_df` with `avg_hr`, `avg_br` | `ndarray` of 1 (normal) / -1 (anomaly) |
| 4. Score | `_z_score_windows` | 30-value Series | `(z_score, pct_change)` tuple |
| 5. Classify | `_classify_vitals` / `_classify_activity` | z, pct_change | `"improved"` / `"declining"` / `"stable"` / `"warning"` |
| 6. Build | `_build_trend` | Series + metric_key | `TrendMetric` \| `None` |
| 7. Assemble | `compute_trends` | All metrics | `TrendsResponse` |

**Z-score windows:**
- Baseline: `iloc[:14]` (days 1–14)
- Trend: `iloc[23:]` (days 24–30)
- Formula: `Z = (trend_mean - baseline_mean) / baseline_std`

**Classification thresholds:**

| Metric type | Rule | Status |
|-------------|------|--------|
| Vitals | `\|Z\| >= 2.0` OR `\|Δ%\| >= 10` | `warning` |
| Vitals | `Z > 1.0` | `improved` |
| Vitals | `Z < -1.0` | `declining` |
| Vitals | otherwise | `stable` |
| Activity | `Z <= -1.5` | `declining` |
| Activity | `Z > 1.0` | `improved` |
| Activity | otherwise | `stable` |

---

## Authentication & Authorization Overview

### Service boundary

The Trends Engine has **no authentication middleware**. It is designed as an internal
microservice that trusts its upstream caller (the PetPulse app server or API gateway) to
have already authenticated the user and confirmed ownership of the requested `pet_id`.

| Layer | Mechanism | Notes |
|-------|-----------|-------|
| Transport | CORS (`CORSMiddleware`) | Origins set by `CORS_ALLOWED_ORIGINS` env var; only `http://` or `https://` origins accepted; empty var = all cross-origin requests blocked |
| Route | UUID format validation | FastAPI rejects malformed `pet_id` values with 422 before any DB call |
| Service-to-service | None | No API key or Bearer token check; **must be added before exposing to any public gateway** |

### Supabase key and RLS interaction

The service authenticates to Supabase using the key in `SUPABASE_KEY`. The behavior
depends entirely on which key type is provided:

| Key type | RLS behaviour | Recommendation |
|----------|--------------|----------------|
| `anon` key | RLS policies **are enforced** — but none exist yet, so all rows are readable | Define RLS policies immediately (see below) |
| `service_role` key | RLS is **bypassed entirely** | Only use if this service runs in a fully private network with no public ingress |

### ⚠️ RLS audit findings

No `CREATE POLICY` or `ENABLE ROW LEVEL SECURITY` statements were found anywhere in
this repository. The following objects are confirmed to have **no RLS policies defined
in this codebase**:

| Object | Type | Risk |
|--------|------|------|
| `AIML_mock_metrics` | Table | Any Supabase anon key holder can read all pets' raw sensor data |
| `daily_vitals_mv` | Materialized view | Cross-pet HR/BR data readable without restriction |
| `daily_temp_mv` | Materialized view | Cross-pet temperature data readable without restriction |
| `daily_weight_mv` | Materialized view | Cross-pet weight data readable without restriction |
| `daily_activity_mv` | Materialized view | Cross-pet activity data readable without restriction |

### Recommended RLS policies

Run the following in the Supabase SQL Editor once a `pet_owners` (or equivalent) table
linking `auth.uid()` to `pet_id` exists in your project:

```sql
-- Source table
ALTER TABLE "AIML_mock_metrics" ENABLE ROW LEVEL SECURITY;
CREATE POLICY "owner_read_metrics"
    ON "AIML_mock_metrics"
    FOR SELECT
    USING (
        pet_id IN (
            SELECT pet_id FROM pet_owners WHERE user_id = auth.uid()
        )
    );

-- Repeat the pattern for each materialized view:
ALTER TABLE daily_vitals_mv   ENABLE ROW LEVEL SECURITY;
CREATE POLICY "owner_read_vitals"
    ON daily_vitals_mv FOR SELECT
    USING (pet_id IN (SELECT pet_id FROM pet_owners WHERE user_id = auth.uid()));

ALTER TABLE daily_temp_mv     ENABLE ROW LEVEL SECURITY;
CREATE POLICY "owner_read_temp"
    ON daily_temp_mv FOR SELECT
    USING (pet_id IN (SELECT pet_id FROM pet_owners WHERE user_id = auth.uid()));

ALTER TABLE daily_weight_mv   ENABLE ROW LEVEL SECURITY;
CREATE POLICY "owner_read_weight"
    ON daily_weight_mv FOR SELECT
    USING (pet_id IN (SELECT pet_id FROM pet_owners WHERE user_id = auth.uid()));

ALTER TABLE daily_activity_mv ENABLE ROW LEVEL SECURITY;
CREATE POLICY "owner_read_activity"
    ON daily_activity_mv FOR SELECT
    USING (pet_id IN (SELECT pet_id FROM pet_owners WHERE user_id = auth.uid()));
```

If using the `service_role` key (private network only), these policies are optional but
still recommended as defence-in-depth.

---

## Local Development Setup Guide

### Prerequisites

- Python 3.10 or later
- A Supabase project (cloud or local CLI) with the four materialized views applied
- Supabase project URL and API key

> **No Supabase CLI project is set up in this directory.** The service targets a
> cloud-hosted Supabase project directly. If you want a fully local stack, see the
> optional local Supabase section below.

---

### 1. Navigate to the service root

```bash
cd /Users/anthonyriad/PetPulse/backend/AI-ML/TrendsEngine
```

### 2. Create and activate a virtual environment

```bash
python3 -m venv .venv
source .venv/bin/activate        # macOS / Linux
# .venv\Scripts\activate         # Windows
```

### 3. Install dependencies

```bash
pip install -r requirements.txt
```

### 4. Configure environment variables

Create a `.env` file in `TrendsEngine/` (or at the backend root three levels up).
There is no `.env.example` — use the template below:

```dotenv
# ── Required ──────────────────────────────────────────────────────────────────
# Your Supabase project URL (Settings → API → Project URL)
SUPABASE_URL=https://<project-ref>.supabase.co

# Anon key (safe for development; enforce RLS before using in production)
# OR service_role key (bypasses RLS — private network only)
SUPABASE_KEY=<your-anon-or-service-role-key>

# ── Optional ──────────────────────────────────────────────────────────────────
# Comma-separated allowed CORS origins; empty = block all cross-origin requests
CORS_ALLOWED_ORIGINS=http://localhost:8081,https://yourapp.example.com
```

**Fallback variable names** (accepted automatically if the above are absent):

```dotenv
EXPO_PUBLIC_SUPABASE_URL=https://<project-ref>.supabase.co
EXPO_PUBLIC_SUPABASE_ANON_KEY=<your-anon-key>
```

### 5. Apply the materialized views to Supabase

Open the **Supabase SQL Editor** (cloud dashboard or local Studio at
`http://localhost:54323`) and run each file in order:

```
sql/daily_vitals_mv.sql
sql/daily_temp_mv.sql
sql/daily_weight_mv.sql
sql/daily_activity_mv.sql
```

Subsequent refreshes (non-blocking):

```sql
REFRESH MATERIALIZED VIEW CONCURRENTLY daily_vitals_mv;
REFRESH MATERIALIZED VIEW CONCURRENTLY daily_temp_mv;
REFRESH MATERIALIZED VIEW CONCURRENTLY daily_weight_mv;
REFRESH MATERIALIZED VIEW CONCURRENTLY daily_activity_mv;
```

### 6. (Optional) Run a fully local Supabase stack

If you want a local Postgres + PostgREST environment instead of targeting the cloud project:

```bash
# Install the Supabase CLI if not already present
brew install supabase/tap/supabase   # macOS

# From the TrendsEngine directory (or any parent that should own the project)
supabase init          # creates supabase/ config — commit this directory
supabase start         # starts local Postgres, PostgREST, Studio, etc.

# The CLI prints local credentials — use them in your .env:
# SUPABASE_URL=http://127.0.0.1:54321
# SUPABASE_KEY=<local-anon-key shown by supabase start>

# Apply the materialized views locally
supabase db reset      # resets to migrations; then apply sql/ files manually, OR
# paste each sql/*.sql file into the local Studio at http://localhost:54323

# Stop the stack when done
supabase stop
```

> **Note:** `supabase db reset` applies files from a `supabase/migrations/` directory.
> Since no migrations folder exists yet in this repo, apply the `sql/` files manually
> via Studio or `psql` after `supabase start`.

### 7. Run the server

```bash
uvicorn main:app --reload --port 8000
```

- API: `http://localhost:8000`
- Interactive docs: `http://localhost:8000/docs`
- Health check: `http://localhost:8000/health`

### 8. Run the test suite (no database required)

All tests mock Supabase — no live connection needed:

```bash
python -m pytest test_trends.py -v
```

### 9. Example request

```bash
curl http://localhost:8000/trends/5835cc7d-a287-4735-b008-399fcb2999bf
```

Expected response shape:

```json
{
  "pet_id": "5835cc7d-a287-4735-b008-399fcb2999bf",
  "computed_at": "2026-04-02",
  "trends": [
    {
      "metricKey": "heart_rate",
      "status": "improved",
      "displayString": "Heart Rate has been trending upward",
      "uiColor": "#34C759",
      "percentageChange": 5.3,
      "sparklineData": [72.1, 73.4, 74.0, 74.5, 75.2, 76.1, 76.8]
    }
  ]
}
```

---

## Refreshing Materialized Views in Production

The views are **not** auto-refreshed. Options:

| Approach | How |
|----------|-----|
| Supabase `pg_cron` | `SELECT cron.schedule('refresh-trends', '*/15 * * * *', $$REFRESH MATERIALIZED VIEW CONCURRENTLY daily_vitals_mv$$)` — repeat per view |
| External cron | Call the Supabase REST API or `psql` on a schedule |
| On-demand trigger | Call `REFRESH` after each batch data ingest from the IoT pipeline |

`CONCURRENTLY` requires the unique index (already included in each `sql/` file) and
prevents table locks during refresh.
