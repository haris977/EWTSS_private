"""Runtime configuration sourced from environment variables.

DRS_BRIDGE_PROFILES_DIR  — directory containing variant *.yaml profiles
                          (default: src/drs_bridge/profiles/)
DRS_BRIDGE_KAFKA_BOOTSTRAP — Kafka bootstrap.servers (default: localhost:9092)
DRS_BRIDGE_LOG_LEVEL     — Python logging level (default: INFO)
DRS_BRIDGE_PROBATION_GARBAGE_CEILING_BYTES — max bytes a provisional (un-framed)
                          connection may buffer before close (default: 4096)
DRS_BRIDGE_PROBATION_IDLE_TIMEOUT_S — seconds a provisional connection may idle
                          with no valid frame before close (default: 10.0)
"""

from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict


_PACKAGE_ROOT = Path(__file__).resolve().parent


class BridgeSettings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="DRS_BRIDGE_", env_file=None)

    profiles_dir: Path = _PACKAGE_ROOT / "profiles"
    kafka_bootstrap: str = "localhost:9092"
    log_level: str = "INFO"
    probation_garbage_ceiling_bytes: int = 4096
    probation_idle_timeout_s: float = 10.0
