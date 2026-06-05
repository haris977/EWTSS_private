"""Verifies every committed variant YAML profile is loadable by VariantProfile."""
from pathlib import Path

import pytest
import yaml

from drs_bridge.profiles._schema import VariantProfile

PROFILES_DIR = Path(__file__).resolve().parents[2] / "src" / "drs_bridge" / "profiles"


def _profile_files():
    return sorted(PROFILES_DIR.glob("*.yaml"))


@pytest.mark.parametrize("path", _profile_files(), ids=lambda p: p.name)
def test_variant_profile_yaml_parses(path: Path):
    with path.open() as f:
        raw = yaml.safe_load(f)
    profile = VariantProfile(**raw)
    assert profile.variant == path.stem
    assert profile.time_signal.precision_required_ms > 0


def test_at_least_one_variant_yaml_exists():
    assert _profile_files(), "expected at least one variant yaml under profiles/"
