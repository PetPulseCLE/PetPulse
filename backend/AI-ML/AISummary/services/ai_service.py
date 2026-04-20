import logging

from fastapi import HTTPException
from google import genai
from google.genai import errors as genai_errors
from google.genai import types as genai_types

from config import settings
from models import HealthSnapshot
from services.clinical_service import build_clinical_reference

logger = logging.getLogger(__name__)

# Upstream Gemini call guardrails: a bounded request timeout (ms) and a small
# retry policy so a hung or transiently failing call cannot block the endpoint.
_HTTP_OPTIONS = genai_types.HttpOptions(
    timeout=30_000,
    retry_options=genai_types.HttpRetryOptions(
        attempts=3,
        initial_delay=1.0,
        max_delay=8.0,
        exp_base=2.0,
        jitter=0.2,
        http_status_codes=[408, 429, 500, 502, 503, 504],
    ),
)

client = genai.Client(api_key=settings.gemini_api_key, http_options=_HTTP_OPTIONS)
PRIMARY_MODEL = "gemini-3.1-flash-lite-preview"
FALLBACK_MODEL = "gemini-2.5-flash"

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


class _EmptyGeminiResponse(RuntimeError):
    """Raised when Gemini returns no usable text (empty body or non-STOP finish)."""


async def generate_summary(snapshot: HealthSnapshot) -> str:
    today = snapshot.today.model_dump(exclude_none=True)
    baseline = snapshot.baseline.model_dump(exclude_none=True)
    conditions, reference = build_clinical_reference(snapshot)

    separator = "=" * 60
    if conditions:
        anomaly_line = f"[Anomaly] Thresholds triggered: {', '.join(conditions)}"
    else:
        anomaly_line = "[Anomaly] No anomalies detected, skipping clinical lookup."
    logger.info(
        "\n%s\n[HealthSnapshot] Pet: %r (%s)\n  Today   \u2192 %s\n  Baseline\u2192 %s\n%s\n%s\n",
        separator,
        snapshot.pet_name,
        snapshot.pet_id,
        today,
        baseline,
        anomaly_line,
        separator,
    )

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

    try:
        return await _invoke_model(PRIMARY_MODEL, user_prompt, snapshot.pet_id)
    except (
        genai_errors.ClientError,
        genai_errors.ServerError,
        genai_errors.APIError,
        _EmptyGeminiResponse,
    ) as primary_exc:
        logger.warning(
            "[FALLBACK TRIGGERED] Primary model %s failed for pet_id=%s (%s: %s); retrying with %s",
            PRIMARY_MODEL,
            snapshot.pet_id,
            type(primary_exc).__name__,
            primary_exc,
            FALLBACK_MODEL,
        )

    try:
        text = await _invoke_model(FALLBACK_MODEL, user_prompt, snapshot.pet_id)
    except genai_errors.ClientError as e:
        logger.exception(
            "Gemini client error on fallback model for pet_id=%s (status=%s)",
            snapshot.pet_id,
            getattr(e, "code", None),
        )
        raise HTTPException(
            status_code=502,
            detail="failed to generate pet health summary",
        ) from e
    except genai_errors.ServerError as e:
        logger.exception(
            "Gemini server error on fallback model for pet_id=%s (status=%s)",
            snapshot.pet_id,
            getattr(e, "code", None),
        )
        raise HTTPException(
            status_code=503,
            detail="pet health summary service temporarily unavailable",
        ) from e
    except genai_errors.APIError as e:
        logger.exception(
            "Gemini API error on fallback model for pet_id=%s",
            snapshot.pet_id,
        )
        raise HTTPException(
            status_code=502,
            detail="failed to generate pet health summary",
        ) from e
    except _EmptyGeminiResponse as e:
        logger.warning(
            "Fallback model returned empty/invalid response for pet_id=%s: %s",
            snapshot.pet_id,
            e,
        )
        raise HTTPException(
            status_code=502,
            detail="failed to generate pet health summary",
        ) from e

    logger.info(
        "Fallback model %s succeeded for pet_id=%s",
        FALLBACK_MODEL,
        snapshot.pet_id,
    )
    return text


async def _invoke_model(model: str, user_prompt: str, pet_id: str) -> str:
    response = await client.aio.models.generate_content(
        model=model,
        contents=user_prompt,
        config=genai_types.GenerateContentConfig(
            system_instruction=SYSTEM_PROMPT,
            temperature=0.1,
            max_output_tokens=200,
        ),
    )

    candidates = getattr(response, "candidates", None) or []
    finish_reason = (
        getattr(candidates[0], "finish_reason", None) if candidates else None
    )
    if finish_reason is not None and finish_reason != genai_types.FinishReason.STOP:
        raise _EmptyGeminiResponse(
            f"non-STOP finish_reason={finish_reason} from model={model} for pet_id={pet_id}"
        )

    try:
        text = getattr(response, "text", None)
    except ValueError as e:
        raise _EmptyGeminiResponse(
            f"invalid text from model={model} for pet_id={pet_id} "
            f"(finish_reason={finish_reason}): {e}"
        ) from e
    if not text:
        raise _EmptyGeminiResponse(
            f"empty text from model={model} for pet_id={pet_id} (finish_reason={finish_reason})"
        )

    return text
