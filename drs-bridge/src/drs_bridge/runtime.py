"""Composes config + profiles + parsers + transport + Kafka into a runnable
service. The factory parameters in __init__ exist so tests can inject fakes;
production callers use the default factories (real AIOKafkaProducer, real
ctypes-loaded parsers, real asyncio sockets).
"""
from __future__ import annotations

import asyncio
import logging
from pathlib import Path
from typing import Callable

from drs_bridge.aus_integration import AusIntegration
from drs_bridge.aus_poller import AusConfig
from drs_bridge.aus_publisher import AusPublisher
from drs_bridge.bridge import Bridge
from drs_bridge.control_publisher import ControlPublisher
from drs_bridge.health_publisher import HealthPublisher
from drs_bridge.parser_loader import load_parser
from drs_bridge.profile_loader import load_profiles
from drs_bridge.transport import UdpSender, start_command_server

logger = logging.getLogger(__name__)


def _default_kafka_producer_factory(bootstrap: str):
    # Imported lazily so unit tests that mock the factory don't pay aiokafka import cost.
    from aiokafka import AIOKafkaProducer
    return AIOKafkaProducer(bootstrap_servers=bootstrap)


async def _default_sender_factory(host: str, port: int):
    return await UdpSender.connect(host, port)


async def _default_command_server_factory(host: str, port: int, handler):
    return await start_command_server(host, port, handler)


class Runtime:
    def __init__(
        self,
        profiles_dir: Path,
        kafka_bootstrap: str,
        kafka_producer_factory: Callable | None = None,
        parser_factory: Callable | None = None,
        sender_factory: Callable | None = None,
        command_server_factory: Callable | None = None,
    ) -> None:
        self._profiles_dir = profiles_dir
        self._kafka_bootstrap = kafka_bootstrap
        self._kafka_producer_factory = kafka_producer_factory or _default_kafka_producer_factory
        self._parser_factory = parser_factory or load_parser
        self._sender_factory = sender_factory or _default_sender_factory
        self._command_server_factory = command_server_factory or _default_command_server_factory

        self._producer = None
        self._command_servers: list = []
        self._senders: list = []
        self._aus_integrations: list[AusIntegration] = []
        self.bridge: Bridge | None = None
        self._stop_event = asyncio.Event()

    async def start(self) -> None:
        logger.info("Runtime starting; profiles_dir=%s kafka=%s", self._profiles_dir, self._kafka_bootstrap)

        self._producer = self._kafka_producer_factory(self._kafka_bootstrap)
        await self._producer.start()

        control = ControlPublisher(producer=self._producer)
        health = HealthPublisher(producer=self._producer)
        self.bridge = Bridge(control_publisher=control, health_publisher=health)

        profiles = load_profiles(self._profiles_dir)
        for variant, profile in profiles.items():

            # ── HTTP-polling variant (e.g. AUS-C2) ─────────────────────
            if profile.http_source is not None:
                if profile.parser_lib is None:
                    logger.error(
                        "HTTP variant=%s has no parser_lib — skipping (no DLL to parse JSON)",
                        variant,
                    )
                    continue

                parser = self._parser_factory(self._profiles_dir / profile.parser_lib)
                aus_cfg = AusConfig(
                    host=profile.http_source.host,
                    port=profile.http_source.port,
                    username=profile.http_source.username,
                    password=profile.http_source.password,
                    poll_interval_s=profile.http_source.poll_interval_s,
                    api_version=profile.http_source.api_version,
                )
                aus_pub = AusPublisher(
                    producer=self._producer,
                    variant=variant,
                    topic=profile.kafka_topic or "aus.detections",
                )
                aus = AusIntegration(
                    config=aus_cfg,
                    parser=parser,
                    publisher=aus_pub,
                    control=control,
                    variant=variant,
                )
                await aus.start()
                self._aus_integrations.append(aus)
                logger.info("registered HTTP+DLL variant=%s host=%s dll=%s",
                            variant, aus_cfg.host, profile.parser_lib)
                continue

            # ── DLL / binary-frame variant ──────────────────────────────
            parser = self._parser_factory(self._profiles_dir / profile.parser_lib)

            _sender_result = self._sender_factory(
                profile.ports["response"].host, profile.ports["response"].port
            )
            sender = await _sender_result if asyncio.iscoroutine(_sender_result) else _sender_result
            self._senders.append(sender)

            async def _on_frame(data: bytes, v: str = variant) -> None:
                logger.debug("variant=%s received %d bytes", v, len(data))

            _server_result = self._command_server_factory(
                profile.ports["command"].host, profile.ports["command"].port, _on_frame
            )
            server = await _server_result if asyncio.iscoroutine(_server_result) else _server_result
            self._command_servers.append(server)

            await self.bridge.register_variant(profile=profile, parser=parser, sender=sender)
            logger.info("registered DLL variant=%s", variant)

    async def run_until_stopped(self) -> None:
        await self._stop_event.wait()

    def request_stop(self) -> None:
        self._stop_event.set()

    async def shutdown(self) -> None:
        logger.info("Runtime shutting down")
        for aus in self._aus_integrations:
            await aus.stop()
        if self.bridge is not None:
            await self.bridge.shutdown()
        for s in self._senders:
            try:
                await s.close()
            except Exception:
                logger.exception("sender close failed")
        for s in self._command_servers:
            try:
                s.close()
                await s.wait_closed()
            except Exception:
                logger.exception("command server close failed")
        if self._producer is not None:
            await self._producer.stop()
