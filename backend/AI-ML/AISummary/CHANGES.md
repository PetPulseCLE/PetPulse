# PetPulse AI Summary Service — Change Log & Developer Guide

**Date:** April 17, 2026  
**Scope:** `backend/AI-ML/AISummary/`  
**Session summary:** Validation, bug fixes, and the Demo Mode date system.

---

## Table of Contents

1. [Background](#1-background)
2. [Bug Fixes](#2-bug-fixes)
3. [New Feature: Demo Mode (Anchored Rolling Clock)](#3-new-feature-demo-mode-anchored-rolling-clock)
4. [Database Changes](#4-database-changes)
5. [Diagnostic Logging](#5-diagnostic-logging)
6. [How to Use](#6-how-to-use)
7. [File Reference](#7-file-reference)

---

## 1. Background

The AI Summary service (`/summarize`) generates a daily plain-language health update for a pet by:

1. Fetching today's vitals, activity, and weight from materialized views over `AIML_mock_metrics`.
2. Computing a historical baseline from the same views via two Postgres RPCs.
3. Running anomaly detection against three clinical thresholds.
4. Injecting any triggered conditions as a "Clinical Reference" into a Gemini prompt.
5. Returning a 2–3 sentence summary to the pet parent.

The mock dataset covers **March 1–30, 2026** across 1,001 synthetic pets. Because no live sensor data is available beyond that window, the service needed a way to advance its sense of "today" dynamically — without requiring new database rows every day and without leaving hardcoded date strings in source code.

---

## 2. Bug Fixes

### 2a. `AIML_mock_subjects` lookup — wrong column name

**File:** `repository/pet_repository.py`

**Problem:** The subject lookup was filtering by `.eq("subject_id", pet_id)`, but the primary key column in `AIML_mock_subjects` is named `id`, not `subject_id`. This caused every name lookup to silently return an empty result, so `pet_name` was always `None` and Gemini had to operate without the pet's name.

**Fix:** Changed the filter to `.eq("id", pet_id)`.

```python
# Before
.eq("subject_id", pet_id)

# After
.eq("id", pet_id)
```

### 2b. `AIML_mock_subjects` RLS policy — anon key blocked

**Location:** Supabase Dashboard → Authentication → Policies

**Problem:** Row Level Security was enabled on `AIML_mock_subjects` with a single policy requiring `auth.uid() = user_id`. The FastAPI backend uses the `anon` (publishable) key, which carries no authenticated session. Postgres evaluates `null = user_id` as false for every row and returns an empty result set — no error is raised, the data is simply invisible.

The materialized views and RPC functions were unaffected because they run as `SECURITY DEFINER`, bypassing RLS.

**Fix:** A second SELECT policy was added in the Supabase dashboard:

| Setting | Value |
|---|---|
| Policy name | `aiml_mock_subjects_anon_select` |
| Role | `anon` |
| Command | `SELECT` |
| Condition | `true` (all rows) |

The original authenticated-user policy remains in place for the mobile app's own queries.

### 2c. Baseline leakage — "today" included in its own baseline

**File:** Supabase — `get_baseline_vitals`, `get_baseline_activity` RPCs

**Problem:** The baseline RPCs originally used `CURRENT_DATE` (real Postgres server time) as the cutoff:

```sql
WHERE pet_id = p_pet_id AND observation_day < CURRENT_DATE
```

When `CURRENT_DATE` is 2026-04-17 (today in real time), every day in the March window is `< CURRENT_DATE`, so the baseline includes the very day being analyzed. This dilutes anomaly signals — a pet's unusually high breathing rate on March 29 was averaged into its own baseline, making the spike appear smaller than it actually was.

**Fix:** See [Section 4](#4-database-changes). The RPCs now accept an explicit `p_effective_today` date and use that as the cutoff instead of the server clock.

---

## 3. New Feature: Demo Mode (Anchored Rolling Clock)

### The problem

The service is designed to run a new summary every 24 hours. But the mock dataset only covers March 1–30, 2026. Without intervention, `datetime.date.today()` on the server would return April 17, 2026 (or later), which matches no rows in any materialized view — producing empty snapshots and meaningless summaries indefinitely.

Previous workaround was hardcoding dates directly in source code (e.g., `"2026-03-25"`), which required manual edits before and after every test run and could not support automated daily execution.

### The solution: Anchored Rolling Clock

A virtual clock maps real calendar days onto the synthetic March window using a fixed anchor point and a modular offset. Once deployed, the system advances its virtual "today" by one day for every real day that passes — automatically, with no manual intervention.

```
real_day_0 (demo_start_real)   →  virtual_day_0 (demo_start_virtual = 2026-03-01)
real_day_1                     →  virtual_day_1 (2026-03-02)
real_day_2                     →  virtual_day_2 (2026-03-03)
...
real_day_29                    →  virtual_day_29 (2026-03-30)
real_day_30                    →  virtual_day_0  (wraps back to 2026-03-01)
```

The 30-day modular wrap means the demo never runs out of data. After 30 real days the cycle replays from the beginning.

### Formula

```python
offset = (today_real - demo_start_real).days
offset = max(offset, 0) % demo_window_days
effective_today = demo_start_virtual + timedelta(days=offset)
```

### Configuration

All parameters are controlled via environment variables and have sensible defaults set in code:

| Env var | Default | Description |
|---|---|---|
| `DEMO_MODE` | `true` | Enable/disable the rolling clock. Set `false` in production. |
| `DEMO_START_REAL` | `2026-04-17` | The real-world date that maps to `DEMO_START_VIRTUAL`. Update this when you redeploy. |
| `DEMO_START_VIRTUAL` | `2026-03-01` | The first day of the synthetic data window. |
| `DEMO_WINDOW_DAYS` | `30` | Length of the synthetic window before wrapping. |

---

## 4. Database Changes

**Migration name:** `baseline_rpcs_effective_today`  
**Applied via:** Supabase MCP `apply_migration`

Both baseline RPCs were updated to accept an optional `p_effective_today` parameter. The default is `CURRENT_DATE`, preserving backward compatibility for any caller that does not pass the parameter.

### `get_baseline_vitals`

```sql
CREATE OR REPLACE FUNCTION public.get_baseline_vitals(
    p_pet_id uuid,
    p_effective_today date DEFAULT CURRENT_DATE
)
RETURNS TABLE(avg_hr double precision, avg_br double precision)
LANGUAGE sql STABLE AS $$
    SELECT AVG(avg_hr), AVG(avg_br)
    FROM daily_vitals_mv
    WHERE pet_id = p_pet_id
      AND observation_day < p_effective_today;
$$;
```

### `get_baseline_activity`

```sql
CREATE OR REPLACE FUNCTION public.get_baseline_activity(
    p_pet_id uuid,
    p_effective_today date DEFAULT CURRENT_DATE
)
RETURNS TABLE(avg_steps double precision)
LANGUAGE sql STABLE AS $$
    SELECT AVG(total_steps)::DOUBLE PRECISION
    FROM daily_activity_mv
    WHERE pet_id = p_pet_id
      AND observation_day < p_effective_today;
$$;
```

**Effect:** The baseline is now computed from all days strictly before the virtual "today". This ensures today's values — whether healthy or anomalous — never influence their own baseline comparison.

---

## 5. Diagnostic Logging

**File:** `services/ai_service.py`

Before every Gemini API call, the service prints a structured debug block to stdout. This is intentional for the development/demo phase and makes it easy to verify correct data flow without an external monitoring tool.

```
============================================================
[HealthSnapshot] Pet: 'Charlie' (02b5e5a7-ad9a-430c-bef2-bfdcf0e58ee6)
  Today   → {'avg_hr': 77.38, 'avg_br': 28.38, 'total_steps': 5833, ...}
  Baseline→ {'avg_hr': 76.61, 'avg_br': 27.89, 'avg_steps': 6651.43}
[Anomaly] No anomalies detected, skipping clinical lookup.
============================================================
```

When anomalies are detected:

```
[Anomaly] Thresholds triggered: Respiratory Issues, Mobility Problems
```

Remove or gate these `print()` calls behind a `settings.debug` flag before a production release.

---

## 6. How to Use

### Running the server

The `.env` at `backend/` does not use `SUPABASE_URL`/`SUPABASE_KEY` variable names — it uses the Expo-prefixed names. Until that is aligned, pass the keys explicitly when starting the server:

```bash
cd backend/AI-ML/AISummary

SUPABASE_URL="<YOUR_SUPABASE_URL>" \
SUPABASE_KEY="<YOUR_SUPABASE_PUBLISHABLE_KEY>" \
GEMINI_API_KEY="<YOUR_GEMINI_API_KEY>" \
venv/bin/uvicorn main:app --host 0.0.0.0 --port 8000
```

All other settings (`DEMO_MODE`, `DEMO_START_REAL`, etc.) are read from `backend/.env` automatically.

### Calling `/summarize`

```bash
curl -X POST http://localhost:8000/summarize \
  -H "Content-Type: application/json" \
  -d '{"pet_id": "<uuid>"}'
```

**Response:**

```json
{
  "pet_id": "...",
  "summary": "Charlie is doing wonderful today...",
  "success": true
}
```

### Simulating a specific virtual date

Override `DEMO_START_REAL` at server startup to shift the virtual window to any day in the dataset.

**Formula:** `DEMO_START_REAL = today_real - desired_virtual_offset`

Where `desired_virtual_offset = desired_virtual_date - DEMO_START_VIRTUAL`.

**Example — simulate virtual March 25:**

Virtual offset = March 25 − March 1 = 24 days.  
Real anchor = April 17 − 24 days = **March 24, 2026**.

```bash
DEMO_START_REAL=2026-03-24 \
SUPABASE_URL="<YOUR_SUPABASE_URL>" \
SUPABASE_KEY="<YOUR_SUPABASE_PUBLISHABLE_KEY>" \
GEMINI_API_KEY="<YOUR_GEMINI_API_KEY>" \
venv/bin/uvicorn main:app --host 0.0.0.0 --port 8000
```

**Example — simulate Luna's dual-anomaly day (virtual March 29):**

Virtual offset = 28 days. Real anchor = April 17 − 28 = **March 20, 2026**.

```bash
DEMO_START_REAL=2026-03-20 \
SUPABASE_URL="<YOUR_SUPABASE_URL>" \
SUPABASE_KEY="<YOUR_SUPABASE_PUBLISHABLE_KEY>" \
GEMINI_API_KEY="<YOUR_GEMINI_API_KEY>" \
venv/bin/uvicorn main:app --host 0.0.0.0 --port 8000
```

Then POST with Luna's pet ID: `c34659b9-0914-4f5c-a21a-1007ad8f6968`

### Switching to production mode

Set `DEMO_MODE=false` in `backend/.env`. The service will use the real system clock for all queries. No code changes are needed — the `get_effective_today()` function returns `datetime.date.today()` directly when demo mode is off.

### Daily cron setup

With demo mode enabled and `DEMO_START_REAL` set to your deployment date, a simple daily cron is all that is needed:

```bash
# crontab entry — runs at 7am every day
0 7 * * * curl -X POST http://localhost:8000/summarize \
  -H "Content-Type: application/json" \
  -d '{"pet_id": "<pet_uuid>"}'
```

No date management is required in the cron itself. The server resolves the correct virtual date automatically on every request.

---

## 7. File Reference

| File | What changed |
|---|---|
| `config.py` | Added `demo_mode`, `demo_start_real`, `demo_start_virtual`, `demo_window_days` settings |
| `services/time_service.py` | **New file.** Single source of truth for `get_effective_today()` |
| `repository/pet_repository.py` | Fixed `subject_id` → `id` column name; replaced `datetime.date.today()` with `get_effective_today()`; passes `p_effective_today` into both baseline RPCs |
| `services/ai_service.py` | Added pre-Gemini diagnostic logging block |
| `backend/.env` | Added `DEMO_MODE`, `DEMO_START_REAL`, `DEMO_START_VIRTUAL`, `DEMO_WINDOW_DAYS` keys |
| Supabase — `get_baseline_vitals` | Added `p_effective_today date DEFAULT CURRENT_DATE` parameter |
| Supabase — `get_baseline_activity` | Added `p_effective_today date DEFAULT CURRENT_DATE` parameter |
| Supabase — `AIML_mock_subjects` | Added `aiml_mock_subjects_anon_select` RLS policy for `anon` role |
