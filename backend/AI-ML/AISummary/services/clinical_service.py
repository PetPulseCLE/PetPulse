import json
from functools import lru_cache
from pathlib import Path

from models import HealthSnapshot

DATASET_PATH = Path(__file__).resolve().parent.parent / "symptoms.json"

BR_SPIKE_RATIO = 1.25
HR_SPIKE_RATIO = 1.20
ACTIVITY_DROP_RATIO = 0.60

RESPIRATORY = "Respiratory Issues"
HEART = "Heart Issues"
MOBILITY = "Mobility Problems"
DIGESTIVE = "Digestive Issues"

MAX_SNIPPETS_PER_CONDITION = 3


@lru_cache(maxsize=1)
def _load_dataset() -> list[dict]:
    with DATASET_PATH.open() as f:
        return json.load(f)


def detect_conditions(snapshot: HealthSnapshot) -> list[str]:
    today = snapshot.today
    baseline = snapshot.baseline
    conditions: list[str] = []

    if today.avg_br and baseline.avg_br:
        if (today.avg_br / baseline.avg_br) > BR_SPIKE_RATIO:
            conditions.append(RESPIRATORY)

    if today.avg_hr and baseline.avg_hr:
        if (today.avg_hr / baseline.avg_hr) > HR_SPIKE_RATIO:
            conditions.append(HEART)

    activity_ratio = None
    if today.activity_pct is not None and baseline.avg_activity_pct:
        activity_ratio = today.activity_pct / baseline.avg_activity_pct
    elif today.total_steps is not None and baseline.avg_steps:
        activity_ratio = today.total_steps / baseline.avg_steps
    if activity_ratio is not None and activity_ratio < ACTIVITY_DROP_RATIO:
        conditions.append(MOBILITY)

    if (
        today.avg_weight
        and today.target_weight
        and today.avg_weight < today.target_weight * 0.95
    ):
        conditions.append(DIGESTIVE)

    return conditions


def get_clinical_context(condition: str, top_k: int = MAX_SNIPPETS_PER_CONDITION) -> str:
    """Return formatted clinical snippets for a condition, prioritizing Clinical Notes."""
    dataset = _load_dataset()
    clinical: list[str] = []
    owner: list[str] = []
    for entry in dataset:
        if entry.get("condition") != condition:
            continue
        text = entry.get("text")
        if not text:
            continue
        if entry.get("record_type") == "Clinical Notes":
            clinical.append(text)
        elif entry.get("record_type") == "Owner Observation":
            owner.append(text)

    selected: list[str] = []
    for text in clinical:
        if len(selected) >= top_k:
            break
        selected.append(f"[Clinical Note] {text}")
    for text in owner:
        if len(selected) >= top_k:
            break
        selected.append(f"[Owner Observation] {text}")

    if not selected:
        return ""

    header = f"== {condition} =="
    return header + "\n" + "\n".join(f"- {s}" for s in selected)


def build_clinical_reference(snapshot: HealthSnapshot) -> tuple[list[str], str]:
    conditions = detect_conditions(snapshot)
    blocks = [get_clinical_context(c) for c in conditions]
    reference = "\n\n".join(b for b in blocks if b)
    return conditions, reference
