"""FastAPI app construction. The lifespan handler wires the time-sync
subsystem; routes are included from drs_server.api.*.
"""
import logging
import logging.config

from fastapi import FastAPI

from drs_server.api import time_status
from drs_server.config import ServerSettings
from drs_server.lifespan import make_lifespan

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
                "console": {"class": "logging.StreamHandler", "formatter": "default"},
            },
            "root": {"level": level, "handlers": ["console"]},
        }
    )


_settings = ServerSettings()
configure_logging(_settings.log_level)

app = FastAPI(
    title="EWTSS drs-server",
    version="0.1.0",
    lifespan=make_lifespan(
        ntpq_path=_settings.ntpq_path,
        kafka_bootstrap=_settings.kafka_bootstrap,
        poll_seconds=_settings.poll_seconds,
    ),
)
app.include_router(time_status.router)


@app.get("/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}
