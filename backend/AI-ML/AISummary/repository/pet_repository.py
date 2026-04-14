from supabase import create_client

from config import settings
from models import BaselineMetrics, HealthSnapshot, TodayMetrics

supabase = create_client(settings.supabase_url, settings.supabase_key)


async def fetch_health_snapshot(pet_id: str) -> HealthSnapshot:
    today_vitals = (
        supabase.table("daily_vitals_mv")
        .select("avg_hr, avg_br, stddev_hr, stddev_br")
        .eq("pet_id", pet_id)
        .eq("observation_day", "today()")
        .order("observation_day", desc=True)
        .limit(1)
        .execute()
    )

    today_activity = (
        supabase.table("daily_activity_mv")
        .select("total_steps, activity_pct")
        .eq("pet_id", pet_id)
        .eq("observation_day", "today()")
        .order("observation_day", desc=True)
        .limit(1)
        .execute()
    )

    today_weight = (
        supabase.table("daily_weight_mv")
        .select("avg_weight, target_weight")
        .eq("pet_id", pet_id)
        .eq("observation_day", "today()")
        .order("observation_day", desc=True)
        .limit(1)
        .execute()
    )

    baseline_vitals = (
        supabase.rpc(
            "get_baseline_vitals",
            {"p_pet_id": pet_id},
        ).execute()
    )

    baseline_activity = (
        supabase.rpc(
            "get_baseline_activity",
            {"p_pet_id": pet_id},
        ).execute()
    )

    tv = today_vitals.data[0] if today_vitals.data else {}
    ta = today_activity.data[0] if today_activity.data else {}
    tw = today_weight.data[0] if today_weight.data else {}
    bv = baseline_vitals.data[0] if baseline_vitals.data else {}
    ba = baseline_activity.data[0] if baseline_activity.data else {}

    return HealthSnapshot(
        pet_id=pet_id,
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
        ),
    )
