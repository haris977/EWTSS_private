"""Tick-consume-lag detection — publishes health events when drs-bridge falls
behind on tick consumption.

Per B1.3 design spec §6 step 3. Tracks (consumed - intended) per tick and
publishes warning / alert health events when consecutive lag exceeds
configured thresholds.
"""
from collections import deque
from datetime import datetime
from typing import Protocol


class HealthPublisher(Protocol):
    async def publish(self, event_type: str, payload: dict) -> None: ...


class TickLagDetector:
    def __init__(
        self,
        publisher: HealthPublisher,
        warn_threshold_ms: int = 100,
        alert_threshold_ms: int = 500,
        consecutive_required: int = 5,
    ):
        self._publisher = publisher
        self._warn = warn_threshold_ms
        self._alert = alert_threshold_ms
        self._required = consecutive_required
        self._recent_lags_ms: deque[float] = deque(maxlen=consecutive_required)
        self._warn_fired = False
        self._alert_fired = False

    async def record_tick(
        self,
        tick: int,
        intended: datetime,
        consumed: datetime,
    ) -> None:
        lag_ms = (consumed - intended).total_seconds() * 1000.0
        self._recent_lags_ms.append(lag_ms)

        if len(self._recent_lags_ms) < self._required:
            return

        if all(lag > self._alert for lag in self._recent_lags_ms):
            if not self._alert_fired:
                await self._publisher.publish(
                    "tick.lag.alert",
                    {"tick": tick, "lag_ms": lag_ms, "consecutive": self._required},
                )
                self._alert_fired = True
                self._warn_fired = True
        elif all(lag > self._warn for lag in self._recent_lags_ms):
            if not self._warn_fired:
                await self._publisher.publish(
                    "tick.lag.warning",
                    {"tick": tick, "lag_ms": lag_ms, "consecutive": self._required},
                )
                self._warn_fired = True
        else:
            # Reset latches when lag returns to normal
            self._warn_fired = False
            self._alert_fired = False
