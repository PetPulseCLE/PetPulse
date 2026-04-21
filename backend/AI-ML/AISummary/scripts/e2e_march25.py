"""End-to-end test: March 25th snapshot for pet 02b5e5a7-ad9a-430c-bef2-bfdcf0e58ee6."""
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from models import BaselineMetrics, HealthSnapshot, TodayMetrics
from services.ai_service import generate_summary
from services.clinical_service import build_clinical_reference

PET_ID = "02b5e5a7-ad9a-430c-bef2-bfdcf0e58ee6"

snapshot = HealthSnapshot(
    pet_id=PET_ID,
    today=TodayMetrics(
        avg_hr=80.85,
        avg_br=28.05,
        stddev_hr=28.95,
        stddev_br=12.57,
        total_steps=7979,
        activity_pct=33.33,
    ),
    baseline=BaselineMetrics(
        avg_hr=76.64,
        avg_br=27.89,
        avg_steps=6588.54,
        avg_activity_pct=33.33,
    ),
)


anomalous_snapshot = HealthSnapshot(
    pet_id=PET_ID,
    today=TodayMetrics(
        avg_hr=110.0,
        avg_br=42.0,
        stddev_hr=12.0,
        stddev_br=6.0,
        total_steps=2100,
        activity_pct=15.0,
    ),
    baseline=BaselineMetrics(
        avg_hr=76.64,
        avg_br=27.89,
        avg_steps=6588.54,
        avg_activity_pct=33.33,
    ),
)


async def run(label: str, s: HealthSnapshot) -> None:
    conditions, reference = build_clinical_reference(s)
    print(f"\n########## {label} ##########")
    print("=== Detected conditions ===")
    print(conditions or "<none>")
    print("\n=== Clinical Reference ===")
    print(reference or "<empty>")
    print("\n=== Gemini Summary ===")
    print(await generate_summary(s))


async def main() -> None:
    await run("March 25, 2026 (actual MV data)", snapshot)
    await run("Anomalous time-travel snapshot", anomalous_snapshot)


if __name__ == "__main__":
    asyncio.run(main())
