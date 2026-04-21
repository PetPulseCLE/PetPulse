import datetime
from pathlib import Path
from pydantic_settings import BaseSettings

ENV_FILE = Path(__file__).resolve().parents[2] / ".env"


class Settings(BaseSettings):
    gemini_api_key: str
    supabase_url: str
    supabase_key: str

    # When True, the app maps real calendar days onto a fixed synthetic window
    # so the mock March dataset can be replayed indefinitely by a daily cron.
    # Set to False in real production environments.
    demo_mode: bool = True

    # The real-world date that corresponds to DEMO_START_VIRTUAL.
    # Every real day that elapses after this anchor advances the virtual
    # "today" by one day inside the synthetic window.
    demo_start_real: datetime.date = datetime.date(2026, 4, 17)

    # First day of the synthetic data window (the virtual "day 1").
    demo_start_virtual: datetime.date = datetime.date(2026, 3, 1)

    # Length of the synthetic window in days. The virtual date wraps modulo
    # this value so the demo never runs out of data.
    demo_window_days: int = 30

    model_config = {"env_file": str(ENV_FILE), "extra": "ignore"}


settings = Settings()
