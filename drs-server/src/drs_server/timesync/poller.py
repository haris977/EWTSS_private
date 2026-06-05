"""Supervised poll loop that drives SyncStateEngine from NtpMonitor samples.

Designed to be wrapped in an `asyncio.Task` from the FastAPI lifespan.
Exits cleanly on cancellation; logs and skips iterations where the
monitor raises (transient `ntpq` failures), so one bad poll doesn't
kill the entire state-engine feed.
"""
from __future__ import annotations

import asyncio
import logging

from drs_server.timesync.ntp_monitor import NtpMonitor
from drs_server.timesync.sync_state_engine import SyncStateEngine

logger = logging.getLogger(__name__)


async def poll_loop(
    monitor: NtpMonitor,
    engine: SyncStateEngine,
    interval_s: float,
) -> None:
    """Loop until cancelled.

    Each iteration:
      1. Call monitor.sample().
      2. Pass the sample to engine.record().
      3. Sleep `interval_s` before the next iteration.

    Exceptions from step 1 or 2 are logged and the loop continues to
    the sleep. CancelledError from the sleep propagates out cleanly.
    """
    while True:
        try:
            sample = await monitor.sample()
            await engine.record(sample)
        except asyncio.CancelledError:
            raise
        except Exception:
            logger.exception("poll iteration failed; continuing")
        await asyncio.sleep(interval_s)
