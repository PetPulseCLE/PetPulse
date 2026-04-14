from pathlib import Path
from pydantic_settings import BaseSettings

ENV_FILE = Path(__file__).resolve().parents[2] / ".env"

class Settings(BaseSettings):
    gemini_api_key: str
    supabase_url: str
    supabase_key: str

    model_config = {"env_file": str(ENV_FILE), "extra": "ignore"}

settings = Settings()