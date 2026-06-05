"""Runtime configuration sourced from environment variables.

DRS_SERVER_NTPQ_PATH       — absolute path to the Meinberg ntpq binary
                             (default: C:\\Program Files\\NTP\\bin\\ntpq.exe)
DRS_SERVER_KAFKA_BOOTSTRAP — Kafka bootstrap.servers (default: localhost:9092)
DRS_SERVER_POLL_SECONDS    — NtpMonitor poll interval (default: 5)
DRS_SERVER_LOG_LEVEL       — Python logging level (default: INFO)
DRS_SERVER_HOST            — uvicorn bind host (default: 0.0.0.0)
DRS_SERVER_PORT            — uvicorn bind port (default: 8000)
"""
from pydantic_settings import BaseSettings, SettingsConfigDict


class ServerSettings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="DRS_SERVER_", env_file=None)

    ntpq_path: str = r"C:\Program Files\NTP\bin\ntpq.exe"
    kafka_bootstrap: str = "localhost:9092"
    poll_seconds: int = 5
    log_level: str = "INFO"
    host: str = "0.0.0.0"
    port: int = 8000
