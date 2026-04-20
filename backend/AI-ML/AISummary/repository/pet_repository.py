import asyncio

from supabase import AsyncClient, acreate_client

from config import settings
from models import BaselineMetrics, HealthSnapshot, TodayMetrics
from services.time_service import get_effective_today

_supabase: AsyncClient | None = None
_supabase_lock = asyncio.Lock()


async def get_supabase_client() -> AsyncClient:
    global _supabase
    if _supabase is None:
        async with _supabase_lock:
            if _supabase is None:
                _supabase = await acreate_client(
                    settings.supabase_url, settings.supabase_key
                )
    return _supabase


async def fetch_health_snapshot(pet_id: str) -> HealthSnapshot:
    supabase = await get_supabase_client()
    today_str = get_effective_today().isoformat()

    subject_q = (
        supabase.table("AIML_mock_subjects")
        .select("name")
        .eq("id", pet_id)
        .limit(1)
        .execute()
    )

    today_vitals_q = (
        supabase.table("daily_vitals_mv")
        .select("avg_hr, avg_br, stddev_hr, stddev_br")
        .eq("pet_id", pet_id)
        .eq("observation_day", today_str)
        .order("observation_day", desc=True)
        .limit(1)
        .execute()
    )

    today_activity_q = (
        supabase.table("daily_activity_mv")
        .select("total_steps, activity_pct")
        .eq("pet_id", pet_id)
        .eq("observation_day", today_str)
        .order("observation_day", desc=True)
        .limit(1)
        .execute()
    )

    today_weight_q = (
        supabase.table("daily_weight_mv")
        .select("avg_weight, target_weight")
        .eq("pet_id", pet_id)
        .eq("observation_day", today_str)
        .order("observation_day", desc=True)
        .limit(1)
        .execute()
    )

    baseline_vitals_q = supabase.rpc(
        "get_baseline_vitals",
        {"p_pet_id": pet_id, "p_effective_today": today_str},
    ).execute()

    baseline_activity_q = supabase.rpc(
        "get_baseline_activity",
        {"p_pet_id": pet_id, "p_effective_today": today_str},
    ).execute()

    (
        subject,
        today_vitals,
        today_activity,
        today_weight,
        baseline_vitals,
        baseline_activity,
    ) = await asyncio.gather(
        subject_q,
        today_vitals_q,
        today_activity_q,
        today_weight_q,
        baseline_vitals_q,
        baseline_activity_q,
    )

    tv = today_vitals.data[0] if today_vitals.data else {}
    ta = today_activity.data[0] if today_activity.data else {}
    tw = today_weight.data[0] if today_weight.data else {}
    bv = baseline_vitals.data[0] if baseline_vitals.data else {}
    ba = baseline_activity.data[0] if baseline_activity.data else {}
    sj = subject.data[0] if subject.data else {}

    return HealthSnapshot(
        pet_id=pet_id,
        pet_name=sj.get("name"),
        today=TodayMetrics(
            avg_hr=tv.get("avg_hr"),
            avg_br=tv.get("avg_br"),
            stddev_hr=tv.get("stddev_hr"),
            stddev_br=tv.get("stddev_br"),
            total_steps=ta.get("total_steps"),
            activity_pct=ta.get("activity_pct"),
            avg_weight=tw.get("avg_weight"),
            target_weight=tw.get("target_weight"),
        ),
        baseline=BaselineMetrics(
            avg_hr=bv.get("avg_hr"),
            avg_br=bv.get("avg_br"),
            avg_steps=ba.get("avg_steps"),
            avg_activity_pct=ba.get("avg_activity_pct"),
        ),
    )
