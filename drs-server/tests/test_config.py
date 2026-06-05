import pytest

from drs_server.config import ServerSettings


def test_defaults(monkeypatch: pytest.MonkeyPatch):
    for k in [
        "DRS_SERVER_NTPQ_PATH", "DRS_SERVER_KAFKA_BOOTSTRAP",
        "DRS_SERVER_POLL_SECONDS", "DRS_SERVER_LOG_LEVEL",
        "DRS_SERVER_HOST", "DRS_SERVER_PORT",
    ]:
        monkeypatch.delenv(k, raising=False)
    s = ServerSettings()
    assert s.ntpq_path == r"C:\Program Files\NTP\bin\ntpq.exe"
    assert s.kafka_bootstrap == "localhost:9092"
    assert s.poll_seconds == 5
    assert s.log_level == "INFO"
    assert s.host == "0.0.0.0"
    assert s.port == 8000


def test_env_override(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setenv("DRS_SERVER_NTPQ_PATH", "/usr/bin/ntpq")
    monkeypatch.setenv("DRS_SERVER_KAFKA_BOOTSTRAP", "broker.lan:9092")
    monkeypatch.setenv("DRS_SERVER_POLL_SECONDS", "10")
    monkeypatch.setenv("DRS_SERVER_LOG_LEVEL", "DEBUG")
    monkeypatch.setenv("DRS_SERVER_HOST", "127.0.0.1")
    monkeypatch.setenv("DRS_SERVER_PORT", "9000")
    s = ServerSettings()
    assert s.ntpq_path == "/usr/bin/ntpq"
    assert s.kafka_bootstrap == "broker.lan:9092"
    assert s.poll_seconds == 10
    assert s.log_level == "DEBUG"
    assert s.host == "127.0.0.1"
    assert s.port == 9000
