from pathlib import Path

import pytest

from drs_bridge.profile_loader import load_profiles


def _write_profile(path: Path, variant: str, lib: str = "stub.dll") -> None:
    path.write_text(
        f"""variant: {variant}
parser_lib: {lib}
ports:
  command:  {{ host: 0.0.0.0, port: 5001, protocol: tcp }}
  response: {{ host: 0.0.0.0, port: 5002, protocol: udp }}
time_signal:
  embedded_in_messages: true
  periodic_distribution:
    enabled: false
    interval_ms: null
  precision_required_ms: 10
""",
        encoding="utf-8",
    )


def test_loads_all_yaml_in_directory(tmp_path: Path):
    _write_profile(tmp_path / "rdfs.yaml", "rdfs")
    _write_profile(tmp_path / "iff.yaml", "iff")
    profiles = load_profiles(tmp_path)
    assert set(profiles.keys()) == {"rdfs", "iff"}
    assert profiles["rdfs"].time_signal.precision_required_ms == 10


def test_skips_non_yaml(tmp_path: Path):
    _write_profile(tmp_path / "rdfs.yaml", "rdfs")
    (tmp_path / "README.md").write_text("ignored", encoding="utf-8")
    profiles = load_profiles(tmp_path)
    assert set(profiles.keys()) == {"rdfs"}


def test_empty_dir_returns_empty_dict(tmp_path: Path):
    assert load_profiles(tmp_path) == {}


def test_invalid_yaml_raises(tmp_path: Path):
    (tmp_path / "bad.yaml").write_text("variant: bad\nports: not-a-dict\n", encoding="utf-8")
    with pytest.raises(Exception):
        load_profiles(tmp_path)
