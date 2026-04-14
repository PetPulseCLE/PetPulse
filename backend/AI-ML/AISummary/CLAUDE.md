# Pet Health AI API: Phase 2 Rules (Data & Deltas)

## Tech Stack

- **Backend**: FastAPI (Python 3.11+)
- **Database**: Supabase (PostgreSQL) with Materialized Views:
  - `daily_vitals_mv` (avg_hr, avg_br, stddev_hr, stddev_br)
  - `daily_activity_mv` (total_steps, activity_pct)
  - `daily_weight_mv` (avg_weight, target_weight)
- **AI Model**: Gemini 3.1 Flash-Lite (`gemini-3.1-flash-lite-preview`)

## AI Strategy: The Delta Pattern

- **Objective**: Summarize health changes by comparing "Today" against "Historical Baseline."
- **Data Retrieval**:
  1. Fetch the most recent row for `pet_id` from all MVs where `observation_day` is CURRENT_DATE.
  2. Calculate the historical mean (Baseline) for HR, BR, and Steps from the same views for all previous dates.
- **Summary Constraint**: Strictly 2-3 sentences. Professional, data-driven, and empathetic.

## Development Standards

- Use `supabase-py` for database operations.
- Maintain a clear separation between `repository/` (DB queries) and `services/` (AI logic).
- Implement a `HealthSnapshot` Pydantic model to pass data between layers.
- Ensure all database calls are handled with proper error catching for missing pet data.

## Model Configuration

- **Temperature**: 0.1 (Stay grounded in sensor data).
- **System Instruction**: "You are a veterinary assistant. Compare today's metrics against the pet's baseline to provide a status update."
