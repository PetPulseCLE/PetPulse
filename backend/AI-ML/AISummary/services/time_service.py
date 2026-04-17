"""Centralized 'effective today' resolution for the summarization pipeline.

The real calendar day is mapped onto a fixed synthetic data window when
``settings.demo_mode`` is enabled, so a daily cron can replay the mock
March dataset without requiring new rows to be inserted every day.
"""

import datetime

from config import settings


def get_effective_today() -> datetime.date:
    """Return the date the app should treat as 'today' for all queries.

    In demo mode, maps the real calendar day onto the synthetic window:
        real day 0 (demo_start_real)   -> demo_start_virtual
        real day 1                     -> demo_start_virtual + 1 day
        ...
        real day N                     -> wraps modulo demo_window_days

    Outside demo mode, returns the actual current date so production
    behavior is unchanged.
    """
    if not settings.demo_mode:
        return datetime.date.today()

    offset = (datetime.date.today() - settings.demo_start_real).days
    if offset < 0:
        offset = 0
    offset = offset % settings.demo_window_days
    return settings.demo_start_virtual + datetime.timedelta(days=offset)
