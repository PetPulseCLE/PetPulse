from google import genai
from config import settings
from models import HealthSnapshot
from services.clinical_service import build_clinical_reference

client = genai.Client(api_key=settings.gemini_api_key)
MODEL = "gemini-3.1-flash-lite-preview"

SYSTEM_PROMPT = """\
You are a warm, supportive, and friendly Veterinary Assistant for the PetPulse app. Your goal is to provide pet parents with a brief, comforting update on their pet's daily health.

### CORE VOICE RULES:
1. PERSONALIZATION: Always use the pet's name. Never say "the pet" or "your animal."
2. TONE: Speak like a caring friend, not a scientist. Use empathetic and encouraging language.
3. NO JARGON: Strictly avoid technical terms like "thresholds," "anomalies," "clinical ranges," "standard deviations," or "metrics."
4. BREVITY: Keep every response to exactly 2-3 sentences.

### RESPONSE LOGIC:
- IF NO ANOMALY (HEALTHY):
  - Focus on how [Pet Name] is doing well.
  - Mention any changes in heart rate, breathing, or activity in a conversational, positive way (e.g., "His heart rate is up a tiny bit because he was so active today!").
  - DO NOT mention a veterinarian, clinic, or consultation.

- IF AN ANOMALY IS DETECTED (RED FLAG):
  - Be calm but direct.
  - Use the provided 'Clinical Reference' to explain the change in simple terms.
  - Gently advise that a veterinary consultation is needed to be safe.

### TASK:
Analyze the provided HealthSnapshot and Clinical Reference. Write a personal 2-3 sentence update for the pet parent that summarizes how the pet is doing today compared to their usual self.
"""


async def generate_summary(snapshot: HealthSnapshot) -> str:
    today = snapshot.today.model_dump(exclude_none=True)
    baseline = snapshot.baseline.model_dump(exclude_none=True)
    conditions, reference = build_clinical_reference(snapshot)

    # --- Healthy Baseline Validation Logging ---
    print("\n" + "=" * 60)
    print(f"[HealthSnapshot] Pet: {snapshot.pet_name!r} ({snapshot.pet_id})")
    print(f"  Today   → {today}")
    print(f"  Baseline→ {baseline}")
    if conditions:
        print(f"[Anomaly] Thresholds triggered: {', '.join(conditions)}")
    else:
        print("[Anomaly] No anomalies detected, skipping clinical lookup.")
    print("=" * 60 + "\n")
    # --- End Logging ---

    if reference:
        reference_block = (
            f"Clinical Reference (suspected: {', '.join(conditions)}):\n{reference}"
        )
    else:
        reference_block = "Clinical Reference: No threshold-breaching anomalies detected."

    pet_name = snapshot.pet_name or "your pet"
    user_prompt = (
        f"Pet Name: {pet_name}\n"
        f"Pet ID: {snapshot.pet_id}\n\n"
        f"Today's Metrics: {today}\n\n"
        f"Baseline: {baseline}\n\n"
        f"{reference_block}"
    )

    response = await client.aio.models.generate_content(
        model=MODEL,
        contents=user_prompt,
        config=genai.types.GenerateContentConfig(
            system_instruction=SYSTEM_PROMPT,
            temperature=0.1,
            max_output_tokens=200,
        ),
    )

    return response.text
