import pytest
from pydantic import ValidationError

from drs_bridge.profiles._schema import VariantProfile, TimeSignalConfig


def test_minimal_profile_valid():
    profile = VariantProfile(
        variant="rdfs",
        parser_lib="parsers/rdfs/parser.dll",
        ports={
            "command": {"host": "0.0.0.0", "port": 5001, "protocol": "tcp"},
            "response": {"host": "0.0.0.0", "port": 5002, "protocol": "udp"},
        },
        time_signal=TimeSignalConfig(
            embedded_in_messages=True,
            periodic_distribution={"enabled": False, "interval_ms": None},
            precision_required_ms=10.0,
        ),
    )
    assert profile.variant == "rdfs"
    assert profile.time_signal.embedded_in_messages is True


def test_periodic_distribution_enabled_requires_interval():
    with pytest.raises(ValidationError, match="interval_ms"):
        TimeSignalConfig(
            embedded_in_messages=True,
            periodic_distribution={"enabled": True, "interval_ms": None},
            precision_required_ms=10.0,
        )


def test_precision_required_ms_must_be_positive():
    with pytest.raises(ValidationError, match="greater than"):
        TimeSignalConfig(
            embedded_in_messages=True,
            periodic_distribution={"enabled": False, "interval_ms": None},
            precision_required_ms=0.0,
        )
