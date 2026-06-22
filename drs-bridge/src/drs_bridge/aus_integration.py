"""Wires AusPoller (HTTP transport) + C++ aus.dll (JSON parsing) + AusPublisher
into a runnable asyncio task.

Data flow per poll cycle:
  1. AusPoller.poll_raw()         -> raw HTTP response bytes (GET /system)
  2. ParserHandle.extract_frame() -> validated JSON frame bytes
  3. ParserHandle.parse_message() -> normalized bridge JSON string
  4. AusPublisher.publish()       -> Kafka topic aus.detections

Fits into the existing Runtime alongside DLL-based variants:
  runtime.py creates AusIntegration when a profile has http_source + parser_lib.

Can also run standalone for smoke-testing (see standalone() at the bottom).
"""

from __future__ import annotations

import asyncio
import logging
from pathlib import Path

from aiokafka import AIOKafkaProducer

from drs_bridge.aus_poller import AusConfig, AusPoller
from drs_bridge.aus_publisher import AusPublisher
from drs_bridge.control_publisher import ControlPublisher
from drs_bridge.parser_loader import ParserHandle, load_parser

logger = logging.getLogger(__name__)


class AusIntegration:
    """Manages the lifecycle of one AUS-C2 polling connection with C++ DLL parsing."""

    def __init__(
        self,
        config: AusConfig,
        parser: ParserHandle,
        publisher: AusPublisher,
        control: ControlPublisher,
        variant: str = "aus",
    ) -> None:
        self._config = config
        self._parser = parser
        self._publisher = publisher
        self._control = control
        self._variant = variant
        self._task: asyncio.Task | None = None

    async def start(self) -> None:
        """Register variant on Kafka and launch the background poll task."""
        await self._control.publish_variant_registration(
            variant=self._variant,
            precision_required_ms=self._config.poll_interval_s * 1000,
        )
        logger.info(
            "AUS integration starting: variant=%s host=%s poll_interval=%.1fs",
            self._variant, self._config.host, self._config.poll_interval_s,
        )
        self._task = asyncio.create_task(self._run(), name=f"aus-poller-{self._variant}")

    async def stop(self) -> None:
        if self._task and not self._task.done():
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
        logger.info("AUS integration stopped: variant=%s", self._variant)

    # ------------------------------------------------------------------

    async def _run(self) -> None:
        poller = AusPoller(self._config)

        async def handle_raw(raw_bytes: bytes) -> None:
            frame_type, frame_bytes = self._parser.extract_frame(raw_bytes)
            if frame_type <= 0:
                if frame_type == -1:
                    logger.warning("AUS extract_frame: corrupt/invalid response (variant=%s)", self._variant)
                return

            result_json = self._parser.parse_message(frame_bytes, frame_type)
            if result_json is None:
                logger.error("AUS parse_message returned None (variant=%s)", self._variant)
                return

            await self._publisher.publish(result_json)

        await poller.run_forever(handle_raw)


# ---------------------------------------------------------------------------
# Standalone runner (for smoke-testing without the full bridge stack)
# ---------------------------------------------------------------------------

async def standalone(
    config: AusConfig,
    dll_path: Path,
    kafka_bootstrap: str,
) -> None:
    """Run the AUS integration by itself."""
    parser = load_parser(dll_path)
    producer = AIOKafkaProducer(bootstrap_servers=kafka_bootstrap)
    await producer.start()
    try:
        control = ControlPublisher(producer=producer)
        publisher = AusPublisher(producer=producer)
        integration = AusIntegration(
            config=config,
            parser=parser,
            publisher=publisher,
            control=control,
        )
        await integration.start()
        await asyncio.get_event_loop().create_future()  # run until Ctrl-C
    finally:
        await producer.stop()
