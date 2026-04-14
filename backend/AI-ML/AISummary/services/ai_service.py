from google import genai
from config import settings
from models import HealthSnapshot

client = genai.Client(api_key=settings.gemini_api_key)
MODEL = "gemini-3.1-flash-lite-preview"

SYSTEM_PROMPT = (
    "You are a veterinary assistant. Compare today's metrics against the pet's "
    "baseline to provide a status update. Keep the summary to exactly 2-3 sentences. "
    "Be professional, empathetic, and data-driven."
)


async def generate_summary(snapshot: HealthSnapshot) -> str:
    today = snapshot.today.model_dump(exclude_none=True)
    baseline = snapshot.baseline.model_dump(exclude_none=True)

    user_prompt = (
        f"Pet ID: {snapshot.pet_id}\n\n"
        f"Today's Metrics: {today}\n\n"
        f"Historical Baseline: {baseline}\n\n"
        "Compare Today's metrics to the Baseline and summarize the change in 2-3 sentences."
    )

    response = await client.aio.models.generate_content(
        model=MODEL,
        contents=user_prompt,
        config=genai.types.GenerateContentConfig(
            system_instruction=SYSTEM_PROMPT,
            temperature=0.1,
            max_output_tokens=150,
        ),
    )

    return response.text
