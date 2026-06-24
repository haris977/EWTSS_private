"""Composes config + profile templates + parsers + transport + Kafka into a
runnable service. Roster-driven: the active roster (received over Kafka
`drs.roster`) decides which per-instance servers are bound. Hot-reloads on
each new snapshot.
"""

from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from drs_bridge.bridge import Bridge
from drs_bridge.kafka.command_consumer import CommandConsumer
from drs_bridge.kafka.control_publisher import ControlPublisher
from drs_bridge.kafka.health_publisher import HealthPublisher
from drs_bridge.kafka.roster_consumer import RosterConsumer
from drs_bridge.kafka.telemetry_publisher import TelemetryPublisher
from drs_bridge.parsers.parser_loader import frame_type_from_bytes, load_parser
from drs_bridge.parsers.profile_loader import load_commands, load_profiles
from drs_bridge.profiles._schema import Roster, RosterEntry
from drs_bridge.transport import UdpSender, start_command_server

logger = logging.getLogger(__name__)


def _default_kafka_producer_factory(bootstrap: str):
    # Imported lazily so unit tests that mock the factory don't pay aiokafka import cost.
    from aiokafka import AIOKafkaProducer

    return AIOKafkaProducer(bootstrap_servers=bootstrap)


def _default_roster_consumer_factory(bootstrap: str, on_roster):
    from aiokafka import AIOKafkaConsumer

    # group_id=None + earliest => always read the retained compacted snapshot,
    # then tail live updates. Single-partition compacted topic.
    consumer = AIOKafkaConsumer(
        "drs.roster",
        bootstrap_servers=bootstrap,
        auto_offset_reset="earliest",
        group_id=None,
        enable_auto_commit=False,
    )
    return RosterConsumer(consumer, on_roster=on_roster)


async def _default_sender_factory(host: str, port: int):
    return await UdpSender.connect(host, port)


@dataclass
class _BoundInstance:
    entry: RosterEntry
    server: object
    sender: object


class Runtime:
    def __init__(
        self,
        profiles_dir: Path,
        kafka_bootstrap: str,
        kafka_producer_factory: Callable | None = None,
        parser_factory: Callable | None = None,
        sender_factory: Callable | None = None,
        command_server_factory: Callable | None = None,
        roster_consumer_factory: Callable | None = None,
        command_consumer_factory: Callable | None = None,
        garbage_ceiling: int = 4096,
        idle_timeout: float = 10.0,
    ) -> None:
        self._profiles_dir = profiles_dir
        self._kafka_bootstrap = kafka_bootstrap
        self._kafka_producer_factory = kafka_producer_factory or _default_kafka_producer_factory
        self._parser_factory = parser_factory or load_parser
        self._sender_factory = sender_factory or _default_sender_factory
        self._command_server_factory = command_server_factory or start_command_server
        self._roster_consumer_factory = roster_consumer_factory or _default_roster_consumer_factory
        self._command_consumer_factory = command_consumer_factory or (
            lambda bootstrap, on_command: CommandConsumer(bootstrap, on_command, self._cmd_name)
        )
        self._garbage_ceiling = garbage_ceiling
        self._idle_timeout = idle_timeout

        self._producer = None
        self._health = None
        self._control = None
        self._telemetry = None
        self._profiles: dict = {}
        self._cmd_names: dict[str, dict[str, str]] = {}  # variant -> {"group/unit" -> name}
        self._parsers: dict[str, object] = {}  # variant -> parser (loaded once)
        self._registered_variants: set[str] = set()  # control-plane dedupe
        self._bound: dict[str, _BoundInstance] = {}  # instance_id -> bound
        self.bridge: Bridge | None = None
        self._roster_consumer = None
        self._consumer_task: asyncio.Task | None = None
        self._command_consumer: CommandConsumer | None = None
        self._command_task: asyncio.Task | None = None
        self._lock = asyncio.Lock()
        self._stop_event = asyncio.Event()

    async def start(self) -> None:
        logger.info(
            "Runtime starting; profiles_dir=%s kafka=%s", self._profiles_dir, self._kafka_bootstrap
        )
        self._producer = self._kafka_producer_factory(self._kafka_bootstrap)
        await self._producer.start()
        self._control = ControlPublisher(producer=self._producer)
        self._health = HealthPublisher(producer=self._producer)
        self._telemetry = TelemetryPublisher(producer=self._producer)
        self.bridge = Bridge(control_publisher=self._control, health_publisher=self._health)
        self._profiles = load_profiles(self._profiles_dir)
        self._cmd_names = load_commands(self._profiles_dir)
        self._roster_consumer = self._roster_consumer_factory(
            self._kafka_bootstrap, self._on_roster
        )
        await self._roster_consumer.start()
        self._consumer_task = asyncio.create_task(
            self._roster_consumer.run(), name="roster-consumer"
        )
        self._command_consumer = self._command_consumer_factory(
            self._kafka_bootstrap, self._on_command
        )
        await self._command_consumer.start()
        self._command_task = asyncio.create_task(
            self._command_consumer.run(), name="command-consumer"
        )

    def _cmd_name(self, variant: str, group, unit) -> str:
        name = self._cmd_names.get(variant, {}).get(f"{group}/{unit}", "")
        return f"  ({name})" if name else ""

    def _on_roster(self, roster: Roster):
        return self.apply_roster(roster)

    async def _on_command(self, msg: dict) -> None:
        """Receive a JSON command from drs.commands, encode to binary, send via UDP.

        msg fields: instance_id, variant, group_id, unit_id, status, payload_hex (opt).
        """
        instance_id = msg.get("instance_id")
        bound = self._bound.get(instance_id)
        if not bound:
            logger.warning("drs.commands: unknown instance_id=%s (not bound)", instance_id)
            return
        parser = self._parsers.get(bound.entry.variant)
        if not parser:
            logger.warning("drs.commands: no parser for variant=%s", bound.entry.variant)
            return
        try:
            import json as _json
            binary = parser.format_response(msg)
            await bound.sender.send(binary)
            _grp = msg.get('group_id')
            _unit = msg.get('unit_id')
            print(
                f"\n[BRIDGE] → UDP binary sent to device  |  "
                f"group={_grp} unit={_unit}"
                f"{self._cmd_name(bound.entry.variant, _grp, _unit)}  "
                f"inst={instance_id}  total={len(binary)}B",
                flush=True,
            )
            print(f"  Command JSON (from drs-server):")
            print(_json.dumps(msg, indent=4), flush=True)
            print(f"  Binary (SDFC frame sent via UDP)  {len(binary)}B:")
            print(f"    hex    : {binary.hex()}", flush=True)
            print(f"    header : {binary[:4].hex()}", flush=True)
            if len(binary) >= 14:
                import struct as _struct
                status = _struct.unpack_from("<h", binary, 4)[0]
                size   = _struct.unpack_from("<I", binary, 6)[0]
                group  = _struct.unpack_from("<H", binary, 10)[0]
                unit   = _struct.unpack_from("<H", binary, 12)[0]
                payload = binary[14:14 + size]
                print(f"    status : {status}", flush=True)
                print(f"    size   : {size}B", flush=True)
                print(f"    group  : {group}", flush=True)
                print(f"    unit   : {unit}", flush=True)
                print(f"    payload: {payload.hex() if payload else '(empty)'}", flush=True)
            print(f"    footer : {binary[-4:].hex()}", flush=True)
        except Exception:
            logger.exception("format_response/send failed instance=%s", instance_id)

    def _parser_for(self, variant: str):
        if variant not in self._parsers:
            profile = self._profiles[variant]
            lib = Path(profile.parser_lib)
            # Relative paths are anchored to the repo root (CWD at service start),
            # not to profiles_dir — parsers/ and profiles/ are sibling top-level dirs.
            if not lib.is_absolute():
                lib = Path.cwd() / lib
            self._parsers[variant] = self._parser_factory(lib)
        return self._parsers[variant]

    async def apply_roster(self, roster: Roster) -> None:
        """Diff the desired (enabled) instances against bound ones; bind added,
        tear down removed/disabled, rebind addressing changes. Defensive: an
        entry whose variant has no profile, uses a non-tcp command transport, or
        fails to bind is isolated (logged + FAILED_BIND health) — siblings are
        unaffected.
        """
        async with self._lock:
            desired = {e.instance_id: e for e in roster.entries if e.enabled}
            # Remove instances no longer desired (gone or disabled) or re-addressed.
            for iid in list(self._bound):
                if iid not in desired or self._addr(self._bound[iid].entry) != self._addr(
                    desired[iid]
                ):
                    await self._teardown(iid)
            # Bind new/changed.
            for iid, entry in desired.items():
                if iid in self._bound:
                    continue
                await self._bind(entry)
            logger.info("roster %s applied; bound=%s", roster.etag, sorted(self._bound))

    @staticmethod
    def _addr(e: RosterEntry):
        return (e.host, e.command.port, e.command.protocol, e.response.port, e.response.protocol)

    async def _publish_telemetry(
        self,
        instance_id: str,
        variant: str,
        frame: bytes,
        parser,
        timestamp_ns: int,
    ) -> None:
        """Decode ``frame`` and publish to hw.<variant>.telemetry.

        Silently skips if:
          - frame type is unrecognised (corrupt/incomplete bytes in hook)
          - parse_message returns None (DLL-level parse failure)
        Logs and swallows all exceptions so a telemetry failure never brings
        down the command-handling path.
        """
        frame_type = frame_type_from_bytes(frame)
        if frame_type <= 0:
            return
        try:
            import json as _json
            parsed = parser.parse_message(frame, frame_type)
            if parsed is None:
                return
            ftype = parsed.get("frame_type", "?")
            grp   = parsed.get("group_id", "?")
            unit  = parsed.get("unit_id", "?")
            display = {k: v for k, v in parsed.items()
                       if k not in ("instance_id", "variant", "timestamp_ns")}
            print(
                f"\n[BRIDGE] DLL decoded → Kafka  |  {ftype.upper()} "
                f"group={grp} unit={unit}{self._cmd_name(variant, grp, unit)}  inst={instance_id}",
                flush=True,
            )
            print(_json.dumps(display, indent=2), flush=True)
            await self._telemetry.publish(
                instance_id=instance_id,
                variant=variant,
                timestamp_ns=timestamp_ns,
                parsed=parsed,
            )
        except Exception:
            logger.exception("telemetry publish failed instance=%s", instance_id)

    async def _ensure_variant_registered(self, variant: str) -> None:
        """Announce the variant to the drs-server control plane exactly once, so
        SyncStateEngine learns its precision threshold. Per-instance time beacons
        are NOT started here (see plan 'Out of scope')."""
        if variant in self._registered_variants:
            return
        profile = self._profiles[variant]
        await self._control.publish_variant_registration(
            variant=variant,
            precision_required_ms=profile.time_signal.precision_required_ms,
        )
        self._registered_variants.add(variant)

    async def _make_sender(self, host: str, port: int):
        result = self._sender_factory(host, port)
        return await result if asyncio.iscoroutine(result) else result

    async def _bind(self, entry: RosterEntry) -> None:
        if entry.variant not in self._profiles:
            logger.error(
                "instance %s names unknown variant %s; isolating", entry.instance_id, entry.variant
            )
            await self._health.publish(
                "instance.failed_bind",
                {"instance_id": entry.instance_id, "reason": "UNKNOWN_VARIANT"},
            )
            return
        if entry.command.protocol != "tcp":
            logger.error(
                "instance %s uses %s command transport; only tcp supported in Plan 1; isolating",
                entry.instance_id,
                entry.command.protocol,
            )
            await self._health.publish(
                "instance.failed_bind",
                {"instance_id": entry.instance_id, "reason": "UDP_COMMAND_UNSUPPORTED"},
            )
            return
        try:
            await self._ensure_variant_registered(entry.variant)
            parser = self._parser_for(entry.variant)
            sender = await self._make_sender(entry.host, entry.response.port)

            def _on_frame(instance_id: str, frame: bytes) -> None:
                import time
                logger.debug("instance=%s frame=%dB", instance_id, len(frame))
                hook = getattr(self, "_test_frames", None)
                if hook is not None:
                    hook.append((instance_id, frame))
                asyncio.create_task(
                    self._publish_telemetry(
                        instance_id, entry.variant, frame, parser, time.time_ns()
                    )
                )

            def _on_connect(instance_id: str) -> None:
                asyncio.create_task(
                    self._health.publish("instance.connected", {"instance_id": instance_id})
                )

            def _on_reject(instance_id: str, reason: str) -> None:
                logger.warning("instance=%s rejected: %s", instance_id, reason)
                asyncio.create_task(
                    self._health.publish(
                        "instance.framing_mismatch", {"instance_id": instance_id, "reason": reason}
                    )
                )

            def _on_disconnect(instance_id: str) -> None:
                asyncio.create_task(
                    self._health.publish("instance.disconnected", {"instance_id": instance_id})
                )

            server = self._command_server_factory(
                entry.host,
                entry.command.port,
                instance_id=entry.instance_id,
                detector=parser.extract_frame,
                on_frame=_on_frame,
                garbage_ceiling=self._garbage_ceiling,
                idle_timeout=self._idle_timeout,
                on_connect=_on_connect,
                on_reject=_on_reject,
                on_disconnect=_on_disconnect,
            )
            server = await server if asyncio.iscoroutine(server) else server
            self._bound[entry.instance_id] = _BoundInstance(
                entry=entry, server=server, sender=sender
            )
            logger.info(
                "bound instance=%s variant=%s %s:%d",
                entry.instance_id,
                entry.variant,
                entry.host,
                entry.command.port,
            )
        except Exception:
            logger.exception("failed to bind instance %s; isolating", entry.instance_id)
            await self._health.publish(
                "instance.failed_bind", {"instance_id": entry.instance_id, "reason": "BIND_ERROR"}
            )

    async def _teardown(self, instance_id: str) -> None:
        bound = self._bound.pop(instance_id, None)
        if bound is None:
            return
        try:
            bound.server.close()
            await bound.server.wait_closed()
        except Exception:
            logger.exception("server close failed for %s", instance_id)
        try:
            await bound.sender.close()
        except Exception:
            logger.exception("sender close failed for %s", instance_id)
        logger.info("tore down instance=%s", instance_id)

    async def run_until_stopped(self) -> None:
        await self._stop_event.wait()

    def request_stop(self) -> None:
        self._stop_event.set()

    async def shutdown(self) -> None:
        logger.info("Runtime shutting down")
        if self._command_task is not None:
            self._command_task.cancel()
            try:
                await self._command_task
            except asyncio.CancelledError:
                pass
        if self._command_consumer is not None:
            try:
                await self._command_consumer.stop()
            except Exception:
                logger.exception("command consumer stop failed")
        if self._consumer_task is not None:
            self._consumer_task.cancel()
            try:
                await self._consumer_task
            except asyncio.CancelledError:
                pass
        if self._roster_consumer is not None:
            try:
                await self._roster_consumer.stop()
            except Exception:
                logger.exception("roster consumer stop failed")
        for iid in list(self._bound):
            await self._teardown(iid)
        if self.bridge is not None:
            await self.bridge.shutdown()
        if self._producer is not None:
            await self._producer.stop()
