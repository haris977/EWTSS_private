"""drs-bridge per-variant lifecycle owner.

Per B1.3 design spec §6. Loads variant profiles, registers each with the
drs-server control plane, optionally starts a Pattern-2 TimeBeacon
coroutine, and instantiates a TickLagDetector for tick-consume-lag
health events.
"""
import asyncio
from typing import Protocol

from drs_bridge.control_publisher import ControlPublisher
from drs_bridge.health_publisher import HealthPublisher
from drs_bridge.profiles._schema import VariantProfile
from drs_bridge.timesync.tick_lag_detector import TickLagDetector
from drs_bridge.timesync.time_beacon import TimeBeaconCoroutine


class Parser(Protocol):
    def format_response(self, kind: str, timestamp_ns: int, **kwargs) -> bytes: ...


class Sender(Protocol):
    async def send(self, payload: bytes) -> None: ...


class Bridge:
    def __init__(
        self,
        control_publisher: ControlPublisher,
        health_publisher: HealthPublisher,
    ):
        self._control_publisher = control_publisher
        self._health_publisher = health_publisher
        self._variant_tasks: dict[str, list[asyncio.Task]] = {}
        self._tick_lag_detectors: dict[str, TickLagDetector] = {}

    async def register_variant(
        self,
        profile: VariantProfile,
        parser: Parser,
        sender: Sender,
    ) -> None:
        """Register a variant: announce to drs-server, start beacon (if
        periodic distribution is enabled), and arm a TickLagDetector for
        the variant.
        """
        self._variant_tasks.setdefault(profile.variant, [])

        await self._control_publisher.publish_variant_registration(
            variant=profile.variant,
            precision_required_ms=profile.time_signal.precision_required_ms,
        )

        if (
            profile.time_signal.periodic_distribution.enabled
            and profile.time_signal.periodic_distribution.interval_ms is not None
        ):
            beacon = TimeBeaconCoroutine(
                variant=profile.variant,
                parser=parser,
                sender=sender,
                interval_ms=profile.time_signal.periodic_distribution.interval_ms,
            )
            task = asyncio.create_task(
                beacon.run(), name=f"time-beacon-{profile.variant}"
            )
            self._variant_tasks[profile.variant].append(task)

        self._tick_lag_detectors[profile.variant] = TickLagDetector(
            publisher=self._health_publisher,
        )

    def tick_lag_detector_for(self, variant: str) -> TickLagDetector:
        """Lookup the detector for a variant. The per-tick consumer
        (ResponseRouter for Scenario mode — outside this file's scope) calls
        `record_tick` on the returned detector."""
        return self._tick_lag_detectors[variant]

    async def shutdown(self) -> None:
        """Cancel all per-variant tasks and wait for them to finish."""
        all_tasks: list[asyncio.Task] = []
        for variant_tasks in self._variant_tasks.values():
            for task in variant_tasks:
                if not task.done():
                    task.cancel()
                all_tasks.append(task)
        for task in all_tasks:
            try:
                await task
            except asyncio.CancelledError:
                pass
        self._variant_tasks.clear()
