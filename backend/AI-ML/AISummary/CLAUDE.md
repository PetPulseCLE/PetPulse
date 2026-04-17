# Pet Health AI API: Phase 3 Rules (Intelligence Layer)

## Tech Stack
- **Backend**: FastAPI (Python 3.11+)
- **Database**: Supabase (Materialized Views + RPCs for baselines)
- **Clinical Data**: `symptoms.json` (Grounded in Merck Veterinary Manual standards)
- **AI Model**: Gemini 3.1 Flash-Lite (`gemini-3.1-flash-lite`)

## Intelligence Strategy: RAG-Lite & Clinical Reasoning
- **Anomaly Detection**: Compare `HealthSnapshot` today values against historical baselines.
- **Threshold Triggers**:
  - **Respiratory Issues**: (avg_br / base_br) > 1.25 (25% spike)
  - **Heart Issues**: (avg_hr / base_hr) > 1.20 (20% spike)
  - **Mobility/Lethargy**: (activity_pct / base_activity_pct) < 0.60 (40% drop)
- **Retrieval**: 
  - If a threshold is met, query `symptoms.json` for the corresponding `condition`.
  - Extract 2-3 relevant 'Clinical Notes' or 'Owner Observations'.
- **Contextual Summary**: Inject these notes as "Clinical Reference" into the Gemini prompt.

## Implementation Standards
- Maintain a `services/clinical_service.py` to handle JSON lookup logic.
- **Temperature**: 0.1 (Strict grounding in sensor data and clinical text).
- **Summary Constraint**: Strictly 2-3 sentences. Professional, empathetic, and expert.
- **Safety**: Always include a suggestion for veterinary consultation when an anomaly is detected.