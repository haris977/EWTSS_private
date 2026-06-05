import pytest
from datetime import datetime, timedelta, timezone

from drs_server.timesync.sync_state_engine import (
    SyncStateEngine, SyncStatus, SyncThresholds
)
from drs_server.timesync.ntp_monitor import NtpSample


def sample(offset_ms: float, t: datetime | None = None) -> NtpSample:
    return NtpSample(
        offset_ms=offset_ms,
        jitter_ms=0.1,
        stratum=2,
        sampled_at=t or datetime.now(timezone.utc),
        peer="SG",
    )


@pytest.mark.asyncio
async def test_initial_status_is_warming():
    engine = SyncStateEngine(SyncThresholds())
    assert await engine.current_status() == SyncStatus.WARMING


@pytest.mark.asyncio
async def test_six_consecutive_healthy_samples_transition_to_healthy():
    engine = SyncStateEngine(SyncThresholds())
    base = datetime.now(timezone.utc)
    for i in range(6):
        await engine.record(sample(2.0, base + timedelta(seconds=5 * i)))
    assert await engine.current_status() == SyncStatus.HEALTHY


@pytest.mark.asyncio
async def test_five_consecutive_over_warn_threshold_transition_to_drift_warn():
    engine = SyncStateEngine(SyncThresholds(warn_ms=10.0))
    base = datetime.now(timezone.utc)
    for i in range(6):
        await engine.record(sample(2.0, base + timedelta(seconds=5 * i)))
    for i in range(5):
        await engine.record(sample(15.0, base + timedelta(seconds=30 + 5 * i)))
    assert await engine.current_status() == SyncStatus.DRIFT_WARN


@pytest.mark.asyncio
async def test_five_consecutive_over_alert_threshold_transition_to_drift_alert():
    engine = SyncStateEngine(SyncThresholds(warn_ms=10.0, alert_ms=50.0))
    base = datetime.now(timezone.utc)
    for i in range(6):
        await engine.record(sample(2.0, base + timedelta(seconds=5 * i)))
    for i in range(5):
        await engine.record(sample(75.0, base + timedelta(seconds=30 + 5 * i)))
    assert await engine.current_status() == SyncStatus.DRIFT_ALERT


@pytest.mark.asyncio
async def test_single_sample_over_lost_threshold_transitions_to_sync_lost():
    engine = SyncStateEngine(
        SyncThresholds(warn_ms=10.0, alert_ms=50.0, lost_ms=200.0)
    )
    base = datetime.now(timezone.utc)
    for i in range(6):
        await engine.record(sample(2.0, base + timedelta(seconds=5 * i)))
    await engine.record(sample(500.0, base + timedelta(seconds=35)))
    assert await engine.current_status() == SyncStatus.SYNC_LOST


@pytest.mark.asyncio
async def test_state_change_callbacks_fire_on_transition():
    engine = SyncStateEngine(SyncThresholds())
    transitions = []
    engine.on_transition(
        lambda scope, old, new: transitions.append((scope, old, new))
    )
    base = datetime.now(timezone.utc)
    for i in range(6):
        await engine.record(sample(2.0, base + timedelta(seconds=5 * i)))
    assert ("global", SyncStatus.WARMING, SyncStatus.HEALTHY) in transitions


@pytest.mark.asyncio
async def test_per_variant_threshold_can_be_stricter_than_global():
    engine = SyncStateEngine(SyncThresholds(warn_ms=10.0))
    engine.register_variant("rdfs_strict", precision_required_ms=1.0)

    base = datetime.now(timezone.utc)
    # Global is fine, variant is over its strict threshold.
    for i in range(5):
        await engine.record(sample(3.0, base + timedelta(seconds=5 * i)))

    assert await engine.current_status() == SyncStatus.WARMING  # global still warming
    assert (
        await engine.current_variant_status("rdfs_strict")
        == SyncStatus.DRIFT_ALERT
    )


@pytest.mark.asyncio
async def test_per_variant_status_isolated_from_other_variants():
    engine = SyncStateEngine(SyncThresholds())
    engine.register_variant("strict", precision_required_ms=1.0)
    engine.register_variant("relaxed", precision_required_ms=100.0)

    base = datetime.now(timezone.utc)
    for i in range(5):
        await engine.record(sample(3.0, base + timedelta(seconds=5 * i)))

    assert await engine.current_variant_status("strict") == SyncStatus.DRIFT_ALERT
    assert await engine.current_variant_status("relaxed") == SyncStatus.WARMING
