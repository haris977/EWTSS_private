"""SyncStatus enum and SyncStateEngine for the time-sync subsystem.

Per B1.3 design spec §7.2.
"""
from collections import deque
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from enum import Enum
from typing import Callable

from drs_server.timesync.ntp_monitor import NtpSample


class SyncStatus(str, Enum):
    HEALTHY = "healthy"
    WARMING = "warming"
    DRIFT_WARN = "drift_warn"
    DRIFT_ALERT = "drift_alert"
    SYNC_LOST = "sync_lost"


@dataclass(frozen=True)
class SyncThresholds:
    warn_ms: float = 10.0
    alert_ms: float = 50.0
    lost_ms: float = 200.0
    healthy_required_samples: int = 6   # 6 samples * 5 s = 30 s sustained
    drift_required_samples: int = 5     # 5 samples * 5 s = 25 s window
    lost_silence_seconds: int = 60      # no NTP response for > 60 s


TransitionCallback = Callable[[str, SyncStatus, SyncStatus], None]
"""Callback signature: (scope, old, new). Scope is "global" or a variant name."""


class SyncStateEngine:
    def __init__(self, thresholds: SyncThresholds):
        self._thresholds = thresholds
        self._status: SyncStatus = SyncStatus.WARMING
        self._window: deque[NtpSample] = deque(
            maxlen=max(
                thresholds.healthy_required_samples,
                thresholds.drift_required_samples,
            )
            + 1
        )
        self._callbacks: list[TransitionCallback] = []
        # Callbacks fire with scope "global" for the engine-level state machine,
        # or with the variant name for per-variant transitions registered via
        # register_variant().
        # Per-variant overrides — name -> {"precision_ms": float, "status": SyncStatus}
        self._variants: dict[str, dict] = {}

    def on_transition(self, callback: TransitionCallback) -> None:
        self._callbacks.append(callback)

    async def current_status(self) -> SyncStatus:
        """Return the current sync status.

        Note: this method has a side effect. If no NtpSample has been
        recorded for longer than `lost_silence_seconds`, the engine
        transitions to `SYNC_LOST` here (fires the transition callbacks)
        before returning. This keeps silence detection co-located with
        state queries; downstream consumers do not need a separate poller.
        Concurrent callers must serialise their awaits.
        """
        # Check for silence — most recent sample older than lost_silence_seconds
        if self._window:
            last = self._window[-1].sampled_at
            if (datetime.now(timezone.utc) - last) > timedelta(
                seconds=self._thresholds.lost_silence_seconds
            ):
                await self._transition_to(SyncStatus.SYNC_LOST)
        return self._status

    def register_variant(self, name: str, precision_required_ms: float) -> None:
        """Register a variant with its own precision threshold."""
        self._variants[name] = {
            "precision_ms": precision_required_ms,
            "status": SyncStatus.WARMING,
        }

    async def current_variant_status(self, name: str) -> SyncStatus:
        if name not in self._variants:
            raise KeyError(f"Variant {name} not registered")
        return self._variants[name]["status"]

    async def record(self, sample: NtpSample) -> None:
        self._window.append(sample)
        new_status = self._classify()
        if new_status != self._status:
            await self._transition_to(new_status)
        # Per-variant classification
        for variant_name, variant in self._variants.items():
            new_variant_status = self._classify_for_threshold(
                variant["precision_ms"]
            )
            if new_variant_status != variant["status"]:
                old = variant["status"]
                variant["status"] = new_variant_status
                for cb in self._callbacks:
                    cb(variant_name, old, new_variant_status)

    def _classify(self) -> SyncStatus:
        if not self._window:
            return SyncStatus.WARMING

        latest = self._window[-1]
        abs_latest = abs(latest.offset_ms)

        # SYNC_LOST takes priority — single sample over lost_ms.
        if abs_latest > self._thresholds.lost_ms:
            return SyncStatus.SYNC_LOST

        t = self._thresholds

        # DRIFT_ALERT — 5 consecutive over alert_ms
        if len(self._window) >= t.drift_required_samples:
            last_n = list(self._window)[-t.drift_required_samples:]
            if all(abs(s.offset_ms) > t.alert_ms for s in last_n):
                return SyncStatus.DRIFT_ALERT

            # DRIFT_WARN — 5 consecutive over warn_ms
            if all(abs(s.offset_ms) > t.warn_ms for s in last_n):
                return SyncStatus.DRIFT_WARN

        # HEALTHY — 6 consecutive at or under warn_ms
        if len(self._window) >= t.healthy_required_samples:
            last_n = list(self._window)[-t.healthy_required_samples:]
            if all(abs(s.offset_ms) <= t.warn_ms for s in last_n):
                return SyncStatus.HEALTHY

        return self._status  # stay in current state

    def _classify_for_threshold(self, warn_ms: float) -> SyncStatus:
        """Classify the current window against an arbitrary warn threshold.

        Per-variant uses simpler classification — if the latest sample
        exceeds the variant's threshold, the variant is in ALERT (so the
        variant's feed pauses). Otherwise HEALTHY after enough consecutive
        samples; WARMING in between.
        """
        if not self._window:
            return SyncStatus.WARMING

        latest = self._window[-1]
        abs_latest = abs(latest.offset_ms)
        if abs_latest > warn_ms:
            return SyncStatus.DRIFT_ALERT
        t = self._thresholds
        if len(self._window) >= t.healthy_required_samples:
            last_n = list(self._window)[-t.healthy_required_samples:]
            if all(abs(s.offset_ms) <= warn_ms for s in last_n):
                return SyncStatus.HEALTHY
        return SyncStatus.WARMING

    async def _transition_to(self, new_status: SyncStatus) -> None:
        old = self._status
        self._status = new_status
        for cb in self._callbacks:
            cb("global", old, new_status)
