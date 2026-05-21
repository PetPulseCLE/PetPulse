# AI Health Summary Pipeline

End-to-end technical reference for how PetPulse generates a daily, veterinarian-grade health summary for each pet. The pipeline is a RAG-Lite design: pre-aggregated Supabase metrics drive deterministic anomaly detection, matched anomalies retrieve curated clinical snippets, and Gemini renders the final 2–3 sentence update.

---

## 1. Architectural Overview

```
Raw Sensor Data (Supabase tables)
        │
        ▼
Materialized Views + RPCs  ──►  pet_repository.fetch_health_snapshot()
        │                               │
        │                               ▼
        │                        HealthSnapshot (today vs. baseline)
        │                               │
        │                               ▼
        │                   clinical_service.detect_conditions()
        │                               │
        │                               ▼
        │                   clinical_service.get_clinical_context()
        │                      (RAG-Lite over symptoms.json)
        │                               │
        │                               ▼
        │                     ai_service.generate_summary()
        │                   ┌───────────┴───────────┐
        │                   ▼                       ▼
        │           Primary: Gemini 3.1     Fallback: Gemini 2.5
        │           Flash-Lite (preview)    Flash-Lite
        │                   │                       │
        │                   └───────────┬───────────┘
        │                               ▼
        │                  Vet-consultation compliance guard
        │                               │
        ▼                               ▼
  POST /summarize  ◄──────  Final 2–3 sentence summary
```

The request boundary is a single FastAPI route (`POST /summarize`) defined in `api/routes.py`, which composes the repository and AI service layers.

---

## 2. Repository Layer — `repository/pet_repository.py`

The repository layer is the system's data gateway. It returns a `HealthSnapshot` (see `models.py`) containing two aligned structures: `TodayMetrics` and `BaselineMetrics`. All heavy aggregation is pushed down into Supabase so the FastAPI process never pulls raw per-minute sensor rows.

### 2.1 Materialized Views — "Today" metrics

Three materialized views are queried for the effective current day (resolved by `services/time_service.get_effective_today()`):

| View | Columns read | Purpose |
|---|---|---|
| `daily_vitals_mv` | `avg_hr`, `avg_br`, `stddev_hr`, `stddev_br` | Daily mean + dispersion of heart and breathing rate |
| `daily_activity_mv` | `total_steps`, `activity_pct` | Daily ambulation and % time active |
| `daily_weight_mv` | `avg_weight`, `target_weight` | Daily weight vs. clinician-set target |

Each query filters on `pet_id` and `observation_day = today`, orders by `observation_day DESC`, and limits to one row. The MV refresh cadence isolates the API from the cost of rolling up raw sensor ingest.

### 2.2 RPC Functions — "Baseline" metrics

Two Postgres functions compute the historical baseline for the same pet, explicitly excluding the effective current day so the comparison cannot leak today's values into its own reference:

| RPC | Returns | Populates |
|---|---|---|
| `get_baseline_vitals(p_pet_id, p_effective_today)` | `avg_hr`, `avg_br` | `BaselineMetrics.avg_hr`, `avg_br` |
| `get_baseline_activity(p_pet_id, p_effective_today)` | `avg_steps`, `avg_activity_pct` | `BaselineMetrics.avg_steps`, `avg_activity_pct` |

### 2.3 Fan-out Concurrency

All six reads (subject lookup + three MVs + two RPCs) are dispatched as independent async coroutines and awaited together via `asyncio.gather(...)`. End-to-end latency is bounded by the slowest query, not the sum. Missing rows are tolerated: each result is defensively unpacked with `.data[0] if .data else {}` and surfaced as `None` on the pydantic model.

---

## 3. Logic Layer — Anomaly Detection

The `HealthSnapshot` returned by the repository is fed into `services/clinical_service.detect_conditions(snapshot)`, which performs deterministic ratio-based threshold checks. The thresholds and the conditions they map to are declared as module-level constants so they can be audited without reading logic.

| Signal | Rule | Constant | Condition Tag |
|---|---|---|---|
| Breathing rate spike | `today.avg_br / baseline.avg_br > 1.25` | `BR_SPIKE_RATIO = 1.25` | `Respiratory Issues` |
| Heart rate spike | `today.avg_hr / baseline.avg_hr > 1.20` | `HR_SPIKE_RATIO = 1.20` | `Heart Issues` |
| Activity drop | `activity_ratio < 0.60` | `ACTIVITY_DROP_RATIO = 0.60` | `Mobility Problems` |
| Weight loss | `today.avg_weight < target_weight * 0.95` | inline | `Digestive Issues` |

**Activity ratio resolution order.** Activity drop prefers `activity_pct` (percent-of-day active) when both today and baseline values are present; it falls back to `total_steps` only if percent data is unavailable. This keeps the signal robust to pets whose step counts vary widely by breed size while preserving a reasonable fallback.

**Why deterministic.** All four checks are executed in Python (not the LLM). The model is never asked to decide *whether* something is wrong — only to communicate *what* a rule-based system has already flagged. This keeps clinical triage reproducible and auditable.

**Null safety.** Every check is guarded by truthiness on both today and baseline inputs, so pets with incomplete data simply produce no false positives for the missing signal.

---

## 4. Knowledge Retrieval — RAG-Lite via `symptoms.json`

When `detect_conditions` returns one or more condition tags, `clinical_service.get_clinical_context(condition)` performs a RAG-Lite retrieval against `symptoms.json`, a curated corpus grounded in Merck Veterinary Manual standards.

### 4.1 Corpus shape

Each record in `symptoms.json` is:

```json
{
  "text":        "<clinical sentence>",
  "condition":   "Respiratory Issues | Heart Issues | Mobility Problems | Digestive Issues | ...",
  "record_type": "Clinical Notes | Owner Observation"
}
```

The dataset is loaded once per process via `@lru_cache(maxsize=1)` — no database or vector store is touched at request time.

### 4.2 Retrieval policy

1. Filter records whose `condition` equals the triggered tag.
2. Partition into two buckets: `Clinical Notes` (clinician-authored) and `Owner Observation` (layperson-reported).
3. Prefer `Clinical Notes` first, then fill from `Owner Observation`, up to `MAX_SNIPPETS_PER_CONDITION = 3` total snippets per condition.
4. Render each snippet prefixed with its provenance tag (`[Clinical Note] ...` or `[Owner Observation] ...`) under a `== <Condition> ==` header.

Clinical prioritization matters: it biases the LLM toward professional phrasing and away from anecdotal framing when both are available.

### 4.3 Composition

`build_clinical_reference(snapshot)` wraps the above: it returns `(conditions, reference)` where `reference` concatenates every per-condition block with blank-line separators. If no thresholds trip, `conditions` is empty and `reference` is the empty string — the retrieval step is skipped entirely.

---

## 5. AI Processing — `services/ai_service.py`

### 5.1 System Persona — "Warm Vet Assistant"

The `SYSTEM_PROMPT` establishes a single, stable persona across every invocation:

- **Personalization**: always addresses the pet by name; never "the pet" or "your animal".
- **Tone**: caring friend, not scientist.
- **No jargon**: terms like "threshold", "anomaly", "standard deviation", "clinical range", "metric" are explicitly forbidden.
- **Brevity**: strictly 2–3 sentences.
- **Branching response logic**:
  - *No anomaly* → warm, positive framing; **must not** mention a vet, clinic, or consultation.
  - *Anomaly detected* → calm and direct; explain the change using the `Clinical Reference`; **must** gently advise a veterinary consultation.

The user prompt itself is a structured block containing pet name, pet id, today's metrics, baseline metrics, and the composed `Clinical Reference` block (or an explicit "no threshold-breaching anomalies detected" marker).

### 5.2 Generation parameters

- `temperature = 0.1` — strong grounding in the supplied snapshot and clinical text; minimizes creative drift.
- `max_output_tokens = 200` — fits the 2–3 sentence constraint with headroom.
- `system_instruction` carries the persona; the user prompt carries the data.

### 5.3 High-availability fallback chain

Two models are configured:

| Role | Model ID |
|---|---|
| Primary | `gemini-3.1-flash-lite-preview` |
| Fallback | `gemini-2.5-flash-lite` |

The primary is attempted first via `_invoke_with_compliance_guard(PRIMARY_MODEL, ...)`. The fallback is triggered on any of:

- `genai_errors.ClientError` / `ServerError` / `APIError` — upstream transport or API failure.
- `_EmptyGeminiResponse` — empty body or a `finish_reason != STOP` (e.g., safety-blocked or truncated).
- `_NonCompliantSummary` — response failed the vet-consultation guard (§5.4) after retries.

The fallback path runs the same guarded invocation with the 2.5 model. If *it* also fails, the route surfaces a typed HTTP error (`502` for client/API/empty/non-compliant; `503` for upstream server errors).

Upstream calls are additionally hardened with an `HttpOptions` policy: 30-second timeout and an exponential-backoff retry (3 attempts, 1s→8s, jitter 0.2) for HTTP 408/429/500/502/503/504 — so transient upstream hiccups are absorbed *inside* a single model attempt before fallback is considered.

### 5.4 Compliance guard

`_invoke_with_compliance_guard` enforces a deterministic safety invariant: **if any condition was triggered, the generated summary must mention a veterinary consultation.** The check is a case-insensitive regex over word-boundaried tokens:

```
\bvet\b | \bvets\b | \bveterinar | \bclinic\b | \banimal hospital\b
```

If a response misses the pattern, the model is re-invoked up to `_MAX_COMPLIANCE_ATTEMPTS = 2` times. If every attempt fails, `_NonCompliantSummary` is raised, triggering the model fallback described above. When `conditions` is empty the guard is a no-op, consistent with the "no anomaly → no vet mention" persona rule.

### 5.5 Privacy-preserving logging

`_redact_pet_id` hashes the pet id to a 12-character SHA-256 prefix for INFO-level logs. Raw pet ids, pet names, and full today/baseline metric dumps are only emitted when `DEBUG` logging is explicitly enabled. The only identifier retained at INFO is the anomaly flag, which is useful for operational monitoring but not re-identifying.

---

## 6. End-to-End Data Flow

1. **Raw sensor data** is continuously ingested into base Supabase tables (heart rate, breathing rate, activity, weight).
2. **Supabase materialized views** (`daily_vitals_mv`, `daily_activity_mv`, `daily_weight_mv`) pre-aggregate the current day's metrics per pet.
3. **`POST /summarize`** receives a `pet_id`; the FastAPI route calls `fetch_health_snapshot`.
4. **Repository layer** fans out six async queries — subject lookup, three MVs (today), and two RPCs (baseline excluding today) — joined via `asyncio.gather`.
5. **FastAPI delta calculation** is performed in `detect_conditions`: today-vs-baseline ratios against the 25% / 20% / 40% / 5% thresholds produce a `conditions` list.
6. **RAG context** is assembled by `build_clinical_reference` — up to 3 Clinical-Notes-first snippets from `symptoms.json` per condition.
7. **Gemini invocation** runs the `Warm Vet Assistant` system prompt plus the structured user prompt through Gemini 3.1 Flash-Lite at `temperature=0.1`.
8. **Compliance guard** enforces the vet-consultation rule; non-compliant outputs trigger a bounded retry, then model fallback to Gemini 2.5 Flash-Lite.
9. **Final summary** — a strictly 2–3 sentence, personalized, jargon-free update — is returned in `SummarizeResponse`.

---

## 7. Reference — File Map

| File | Role |
|---|---|
| `api/routes.py` | FastAPI `POST /summarize` endpoint |
| `repository/pet_repository.py` | Supabase MV + RPC fan-out, builds `HealthSnapshot` |
| `services/time_service.py` | `get_effective_today()` — single source of truth for "today" |
| `services/clinical_service.py` | Threshold rules + RAG-Lite lookup over `symptoms.json` |
| `services/ai_service.py` | System prompt, Gemini invocation, fallback + compliance guard |
| `models.py` | `TodayMetrics`, `BaselineMetrics`, `HealthSnapshot` pydantic models |
| `symptoms.json` | Merck-Vet-Manual-grounded clinical corpus |
| `config.py` | Environment-backed settings (Supabase + Gemini credentials) |