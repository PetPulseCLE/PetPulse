import hashlib
import logging
import re

from fastapi import HTTPException
from google import genai
from google.genai import errors as genai_errors
from google.genai import types as genai_types

from config import settings
from models import HealthSnapshot
from services.clinical_service import build_clinical_reference

logger = logging.getLogger(__name__)


def _redact_pet_id(pet_id: str | None) -> str:
    """Return a short deterministic hash of pet_id for safe INFO-level logging."""
    if not pet_id:
        return "[REDACTED]"
    return hashlib.sha256(pet_id.encode("utf-8")).hexdigest()[:12]

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
FALLBACK_MODEL = "gemini-2.5-flash-lite"
# Max attempts for the deterministic vet-consultation compliance guard when
# conditions are non-empty. Bounded to avoid runaway retries on model drift.
_MAX_COMPLIANCE_ATTEMPTS = 2

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


class _NonCompliantSummary(RuntimeError):
    """Raised when a model response fails the veterinary-consultation guard."""


# Words/phrases that indicate the summary has advised a veterinary consultation.
# Matching is case-insensitive and word-boundaried so inflected forms are
# accepted (e.g., "vet", "veterinary", "veterinarian", "clinic").
_VET_CONSULT_PATTERNS = (
    r"\bvet\b",
    r"\bvets\b",
    r"\bveterinar",
    r"\bclinic\b",
    r"\banimal hospital\b",
)
_VET_CONSULT_RE = re.compile("|".join(_VET_CONSULT_PATTERNS), re.IGNORECASE)


def validate_anomaly_summary(text: str, conditions: list[str]) -> bool:
    """Return True iff the summary satisfies the veterinary-consultation rule.

    When `conditions` is non-empty (i.e., at least one clinical threshold was
    triggered), the summary MUST mention a veterinary consultation in some
    form. When `conditions` is empty, any response is considered compliant.
    """
    if not conditions:
        return True
    if not text:
        return False
    return bool(_VET_CONSULT_RE.search(text))


async def generate_summary(snapshot: HealthSnapshot) -> str:
    today = snapshot.today.model_dump(exclude_none=True)
    baseline = snapshot.baseline.model_dump(exclude_none=True)
    conditions, reference = build_clinical_reference(snapshot)

    if conditions:
        anomaly_line = f"[Anomaly] Thresholds triggered: {', '.join(conditions)}"
    else:
        anomaly_line = "[Anomaly] No anomalies detected, skipping clinical lookup."

    pet_ref = _redact_pet_id(snapshot.pet_id)
    # INFO-level message is intentionally minimal: no pet name, no raw pet_id,
    # and no raw metrics. Only the (non-identifying) anomaly flag is retained
    # because it is useful for operational monitoring.
    logger.info(
        "[HealthSnapshot] pet_ref=%s %s",
        pet_ref,
        anomaly_line,
    )
    # Full snapshot dumps (pet name, raw pet_id, today/baseline metrics) are
    # sensitive and must only be emitted when debug logging is explicitly
    # enabled by the operator.
    if logger.isEnabledFor(logging.DEBUG):
        separator = "=" * 60
        logger.debug(
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
        return await _invoke_with_compliance_guard(
            PRIMARY_MODEL, user_prompt, snapshot.pet_id, conditions
        )
    except (
        genai_errors.ClientError,
        genai_errors.ServerError,
        genai_errors.APIError,
        _EmptyGeminiResponse,
        _NonCompliantSummary,
    ) as primary_exc:
        logger.warning(
            "[FALLBACK TRIGGERED] Primary model %s failed for pet_ref=%s (%s: %s); retrying with %s",
            PRIMARY_MODEL,
            pet_ref,
            type(primary_exc).__name__,
            primary_exc,
            FALLBACK_MODEL,
        )

    try:
        text = await _invoke_with_compliance_guard(
            FALLBACK_MODEL, user_prompt, snapshot.pet_id, conditions
        )
    except genai_errors.ClientError as e:
        logger.exception(
            "Gemini client error on fallback model for pet_ref=%s (status=%s)",
            pet_ref,
            getattr(e, "code", None),
        )
        raise HTTPException(
            status_code=502,
            detail="failed to generate pet health summary",
        ) from e
    except genai_errors.ServerError as e:
        logger.exception(
            "Gemini server error on fallback model for pet_ref=%s (status=%s)",
            pet_ref,
            getattr(e, "code", None),
        )
        raise HTTPException(
            status_code=503,
            detail="pet health summary service temporarily unavailable",
        ) from e
    except genai_errors.APIError as e:
        logger.exception(
            "Gemini API error on fallback model for pet_ref=%s",
            pet_ref,
        )
        raise HTTPException(
            status_code=502,
            detail="failed to generate pet health summary",
        ) from e
    except _EmptyGeminiResponse as e:
        logger.warning(
            "Fallback model returned empty/invalid response for pet_ref=%s: %s",
            pet_ref,
            e,
        )
        raise HTTPException(
            status_code=502,
            detail="failed to generate pet health summary",
        ) from e
    except _NonCompliantSummary as e:
        logger.error(
            "Fallback model produced non-compliant summary (missing vet "
            "consultation) for pet_ref=%s after retries: %s",
            pet_ref,
            e,
        )
        raise HTTPException(
            status_code=502,
            detail="failed to generate pet health summary",
        ) from e

    logger.info(
        "Fallback model %s succeeded for pet_ref=%s",
        FALLBACK_MODEL,
        pet_ref,
    )
    return text


async def _invoke_with_compliance_guard(
    model: str,
    user_prompt: str,
    pet_id: str,
    conditions: list[str],
) -> str:
    """Invoke `model` and enforce the veterinary-consultation rule.

    When `conditions` is non-empty the response must mention a veterinary
    consultation; if it does not, the model is re-invoked up to
    `_MAX_COMPLIANCE_ATTEMPTS` times total. If every attempt fails the guard
    a `_NonCompliantSummary` is raised so callers can trigger model fallback
    or surface a 502 to the client. Upstream Gemini errors are not caught
    here and propagate unchanged.
    """
    pet_ref = _redact_pet_id(pet_id)
    last_text = ""
    for attempt in range(1, _MAX_COMPLIANCE_ATTEMPTS + 1):
        text = await _invoke_model(model, user_prompt, pet_id)
        if validate_anomaly_summary(text, conditions):
            return text
        last_text = text
        logger.warning(
            "[COMPLIANCE] model=%s attempt=%d/%d produced summary without "
            "vet-consultation advice despite conditions=%s for pet_ref=%s",
            model,
            attempt,
            _MAX_COMPLIANCE_ATTEMPTS,
            conditions,
            pet_ref,
        )
    raise _NonCompliantSummary(
        f"model={model} failed veterinary-consultation guard after "
        f"{_MAX_COMPLIANCE_ATTEMPTS} attempts for pet_ref={pet_ref} "
        f"(last_text_len={len(last_text)})"
    )


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

    pet_ref = _redact_pet_id(pet_id)
    candidates = getattr(response, "candidates", None) or []
    finish_reason = (
        getattr(candidates[0], "finish_reason", None) if candidates else None
    )
    if finish_reason is not None and finish_reason != genai_types.FinishReason.STOP:
        raise _EmptyGeminiResponse(
            f"non-STOP finish_reason={finish_reason} from model={model} for pet_ref={pet_ref}"
        )

    try:
        text = getattr(response, "text", None)
    except ValueError as e:
        raise _EmptyGeminiResponse(
            f"invalid text from model={model} for pet_ref={pet_ref} "
            f"(finish_reason={finish_reason}): {e}"
        ) from e
    if not text:
        raise _EmptyGeminiResponse(
            f"empty text from model={model} for pet_ref={pet_ref} (finish_reason={finish_reason})"
        )

    return text
