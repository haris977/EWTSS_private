"""Consumes drs.commands — JSON command messages published by drs-server.

Each message contains instance_id, variant, group_id, unit_id, status, and
optional payload_hex.  The Runtime calls parser.format_response() to encode
the JSON to a binary SDFC frame and sends it to the device via UdpSender.
"""
from __future__ import annotations

import asyncio
import json
import logging
from typing import Callable, Awaitable

logger = logging.getLogger(__name__)

_TOPIC = "drs.commands"


class CommandConsumer:
    def __init__(
        self,
        bootstrap: str,
        on_command: Callable[[dict], Awaitable[None]],
        cmd_name_lookup: Callable[[str, int, int], str] | None = None,
    ) -> None:
        from aiokafka import AIOKafkaConsumer

        self._consumer = AIOKafkaConsumer(
            _TOPIC,
            bootstrap_servers=bootstrap,
            auto_offset_reset="latest",
            group_id="drs-bridge-commands",
            enable_auto_commit=True,
        )
        self._on_command = on_command
        self._cmd_name_lookup = cmd_name_lookup
        self._stop = asyncio.Event()

    async def start(self) -> None:
        await self._consumer.start()
        logger.info("command consumer started  topic=%s", _TOPIC)

    async def stop(self) -> None:
        self._stop.set()
        await self._consumer.stop()

    async def run(self) -> None:
        while not self._stop.is_set():
            try:
                batch = await asyncio.wait_for(
                    self._consumer.getmany(max_records=50), timeout=1.0
                )
                for _tp, messages in batch.items():
                    for msg in messages:
                        try:
                            data = json.loads(msg.value)
                            grp = data.get('group_id')
                            unit = data.get('unit_id')
                            variant = data.get('variant', '')
                            name_sfx = ''
                            if self._cmd_name_lookup is not None and grp is not None and unit is not None:
                                name_sfx = self._cmd_name_lookup(variant, grp, unit)
                            print(
                                f"\n[BRIDGE] ← drs.commands from drs-server  |  "
                                f"group={grp} unit={unit}{name_sfx}  "
                                f"inst={data.get('instance_id')}",
                                flush=True,
                            )
                            print(json.dumps(data, indent=2), flush=True)
                            await self._on_command(data)
                        except Exception:
                            logger.exception("command consumer dispatch error")
            except asyncio.TimeoutError:
                pass
            except asyncio.CancelledError:
                break
