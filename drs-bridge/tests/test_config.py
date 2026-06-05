import pytest

from drs_bridge.config import BridgeSettings


def test_defaults(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.delenv("DRS_BRIDGE_PROFILES_DIR", raising=False)
    monkeypatch.delenv("DRS_BRIDGE_KAFKA_BOOTSTRAP", raising=False)
    monkeypatch.delenv("DRS_BRIDGE_LOG_LEVEL", raising=False)
    s = BridgeSettings()
    assert s.profiles_dir.name == "profiles"
    assert s.kafka_bootstrap == "localhost:9092"
    assert s.log_level == "INFO"


def test_env_override(monkeypatch: pytest.MonkeyPatch, tmp_path):
    monkeypatch.setenv("DRS_BRIDGE_PROFILES_DIR", str(tmp_path))
    monkeypatch.setenv("DRS_BRIDGE_KAFKA_BOOTSTRAP", "broker.lan:9092")
    monkeypatch.setenv("DRS_BRIDGE_LOG_LEVEL", "DEBUG")
    s = BridgeSettings()
    assert s.profiles_dir == tmp_path
    assert s.kafka_bootstrap == "broker.lan:9092"
    assert s.log_level == "DEBUG"
