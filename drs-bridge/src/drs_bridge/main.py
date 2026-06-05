"""Production entry point for drs-bridge. Loads BridgeSettings from env,
constructs a Runtime, installs signal handlers, and runs until stopped.
"""
from __future__ import annotations

import asyncio
import logging
import logging.config
import signal
import sys
from pathlib import Path

from drs_bridge.config import BridgeSettings
from drs_bridge.runtime import Runtime

logger = logging.getLogger(__name__)


def configure_logging(level: str) -> None:
    logging.config.dictConfig(
        {
            "version": 1,
            "disable_existing_loggers": False,
            "formatters": {
                "default": {
                    "format": "%(asctime)s %(levelname)-7s %(name)s: %(message)s",
                },
            },
            "handlers": {
                "console": {
                    "class": "logging.StreamHandler",
                    "formatter": "default",
                },
            },
            "root": {"level": level, "handlers": ["console"]},
        }
    )


def _install_signal_handlers(loop: asyncio.AbstractEventLoop, runtime: Runtime) -> None:
    """SIGINT + SIGTERM on POSIX; SIGINT only on Windows (asyncio doesn't
    support add_signal_handler on Windows). Both route to runtime.request_stop.
    """
    if sys.platform == "win32":
        signal.signal(signal.SIGINT, lambda *_: runtime.request_stop())
    else:
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, runtime.request_stop)


async def run(profiles_dir: Path, kafka_bootstrap: str) -> None:
    runtime = Runtime(profiles_dir=profiles_dir, kafka_bootstrap=kafka_bootstrap)
    loop = asyncio.get_running_loop()
    _install_signal_handlers(loop, runtime)
    await runtime.start()
    try:
        await runtime.run_until_stopped()
    finally:
        await runtime.shutdown()


def main() -> None:
    settings = BridgeSettings()
    configure_logging(settings.log_level)
    logger.info("drs-bridge starting (profiles_dir=%s, kafka=%s)",
                settings.profiles_dir, settings.kafka_bootstrap)
    asyncio.run(run(profiles_dir=settings.profiles_dir, kafka_bootstrap=settings.kafka_bootstrap))


if __name__ == "__main__":
    main()
