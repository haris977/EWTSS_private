import asyncio

import pytest
from unittest.mock import AsyncMock, MagicMock

from drs_bridge.bridge import Bridge
from drs_bridge.control_publisher import ControlPublisher
from drs_bridge.health_publisher import HealthPublisher
from drs_bridge.profiles._schema import (
    PeriodicDistribution,
    PortConfig,
    TimeSignalConfig,
    VariantProfile,
)


def _profile(*, variant: str, periodic_enabled: bool, interval_ms: int | None) -> VariantProfile:
    return VariantProfile(
        variant=variant,
        parser_lib=f"parsers/{variant}/parser.dll",
        ports={
            "command": PortConfig(host="0.0.0.0", port=5001, protocol="tcp"),
            "response": PortConfig(host="0.0.0.0", port=5002, protocol="udp"),
        },
        time_signal=TimeSignalConfig(
            embedded_in_messages=True,
            periodic_distribution=PeriodicDistribution(
                enabled=periodic_enabled, interval_ms=interval_ms
            ),
            precision_required_ms=10.0,
        ),
    )


def _bridge() -> tuple[Bridge, AsyncMock, AsyncMock]:
    producer = AsyncMock()  # used for both publishers
    control = ControlPublisher(producer=producer)
    health = HealthPublisher(producer=producer)
    return Bridge(control_publisher=control, health_publisher=health), producer, producer


@pytest.mark.asyncio
async def test_register_variant_announces_to_control_plane():
    bridge, producer, _ = _bridge()
    profile = _profile(variant="rdfs", periodic_enabled=False, interval_ms=None)

    await bridge.register_variant(profile, parser=MagicMock(), sender=AsyncMock())

    # send_and_wait was called at least once and the first arg was the control topic.
    producer.send_and_wait.assert_awaited()
    assert producer.send_and_wait.call_args.args[0] == "drs.control"


@pytest.mark.asyncio
async def test_register_variant_skips_beacon_when_periodic_disabled():
    bridge, _, _ = _bridge()
    profile = _profile(variant="rdfs", periodic_enabled=False, interval_ms=None)

    await bridge.register_variant(profile, parser=MagicMock(), sender=AsyncMock())

    assert bridge._variant_tasks["rdfs"] == []  # no beacon created


@pytest.mark.asyncio
async def test_register_variant_starts_beacon_when_periodic_enabled():
    bridge, _, _ = _bridge()
    profile = _profile(variant="iff", periodic_enabled=True, interval_ms=100)
    parser = MagicMock()
    parser.format_response.return_value = b"beacon"
    sender = AsyncMock()

    await bridge.register_variant(profile, parser=parser, sender=sender)

    assert len(bridge._variant_tasks["iff"]) == 1
    task = bridge._variant_tasks["iff"][0]
    assert task.get_name() == "time-beacon-iff"
    assert not task.done()

    await bridge.shutdown()
    assert task.done()


@pytest.mark.asyncio
async def test_tick_lag_detector_registered_per_variant():
    bridge, _, _ = _bridge()
    profile = _profile(variant="rdfs", periodic_enabled=False, interval_ms=None)
    await bridge.register_variant(profile, parser=MagicMock(), sender=AsyncMock())

    detector = bridge.tick_lag_detector_for("rdfs")
    assert detector is not None  # TickLagDetector instance


@pytest.mark.asyncio
async def test_shutdown_cancels_all_running_tasks():
    bridge, _, _ = _bridge()
    profile_a = _profile(variant="a", periodic_enabled=True, interval_ms=50)
    profile_b = _profile(variant="b", periodic_enabled=True, interval_ms=50)
    parser = MagicMock()
    parser.format_response.return_value = b"x"
    sender = AsyncMock()

    await bridge.register_variant(profile_a, parser=parser, sender=sender)
    await bridge.register_variant(profile_b, parser=parser, sender=sender)

    await bridge.shutdown()

    assert bridge._variant_tasks == {}
