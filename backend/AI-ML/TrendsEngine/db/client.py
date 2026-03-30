import os
from pathlib import Path

from dotenv import load_dotenv
from supabase import Client, create_client

# Load env files from deterministic locations so startup cwd does not matter.
_TRENDS_ENGINE_ROOT = Path(__file__).resolve().parents[1]
_BACKEND_ROOT = Path(__file__).resolve().parents[3]

for _env_file in (_TRENDS_ENGINE_ROOT / ".env", _BACKEND_ROOT / ".env"):
    if _env_file.exists():
        load_dotenv(dotenv_path=_env_file, override=False)

_client: Client | None = None


def get_client() -> Client:
    """
    Return a singleton Supabase client.

    Reads SUPABASE_URL/SUPABASE_KEY from environment.
    Falls back to EXPO_PUBLIC_SUPABASE_URL/EXPO_PUBLIC_SUPABASE_ANON_KEY.
    Raises RuntimeError on first call if required variables are missing.
    """
    global _client
    if _client is None:
        supabase_url = os.getenv("SUPABASE_URL") or os.getenv("EXPO_PUBLIC_SUPABASE_URL")
        supabase_key = os.getenv("SUPABASE_KEY") or os.getenv("EXPO_PUBLIC_SUPABASE_ANON_KEY")

        missing_vars: list[str] = []
        if not supabase_url:
            missing_vars.append("SUPABASE_URL (or EXPO_PUBLIC_SUPABASE_URL)")
        if not supabase_key:
            missing_vars.append("SUPABASE_KEY (or EXPO_PUBLIC_SUPABASE_ANON_KEY)")

        if missing_vars:
            raise RuntimeError(
                "Missing required Supabase env var(s): " + ", ".join(missing_vars)
            )

        _client = create_client(
            supabase_url,
            supabase_key,
        )
    return _client
