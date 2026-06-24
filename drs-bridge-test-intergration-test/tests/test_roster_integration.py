"""End-to-end: published roster -> per-instance servers -> frame routing +
probation + hot-reload, against the REAL reference parser DLL on loopback
sockets. Graceful-skips when CMake is unavailable (reuses the
`built_reference_parser` session fixture in conftest.py). No Kafka broker
needed — Runtime.apply_roster is driven directly.
"""

import asyncio
from pathlib import Path
from unittest.mock import AsyncMock

import pytest

from drs_bridge.runtime import Runtime
from drs_bridge.parsers.parser_loader import load_parser
from drs_bridge.profiles._schema import Roster


def _write_profile(profiles_dir: Path, variant: str, dll: Path) -> None:
    (profiles_dir / f"{variant}.yaml").write_text(
        f"""variant: {variant}
parser_lib: {dll.as_posix()}
time_signal:
  embedded_in_messages: true
  periodic_distribution:
    enabled: false
    interval_ms: null
  precision_required_ms: 10
""",
        encoding="utf-8",
    )


def _entry(iid, port, enabled=True):
    return {
        "instance_id": iid,
        "variant": "ref",
        "host": "127.0.0.1",
        "command": {"port": port, "protocol": "tcp"},
        "response": {"port": 0, "protocol": "udp"},
        "port_source": "irs_fixed",
        "enabled": enabled,
    }


def _free_port() -> int:
    import socket

    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


@pytest.mark.asyncio
async def test_end_to_end_binding_probation_and_hot_reload(tmp_path, built_reference_parser):
    # profile points parser_lib at the freshly-built reference DLL (absolute path)
    _write_profile(tmp_path, "ref", built_reference_parser)
    frames: list[tuple[str, bytes]] = []

    rt = Runtime(
        profiles_dir=tmp_path,
        kafka_bootstrap="unused:9092",
        kafka_producer_factory=lambda b: AsyncMock(),
        parser_factory=load_parser,  # REAL parser
        sender_factory=lambda host, port: AsyncMock(),
        roster_consumer_factory=lambda bootstrap, on_roster: AsyncMock(),
        command_consumer_factory=lambda bootstrap, on_command: AsyncMock(),
        idle_timeout=1.0,
        garbage_ceiling=4096,
    )
    rt._test_frames = frames  # capture routed frames (test-only hook in Runtime._bind)
    await rt.start()

    p1, p2 = _free_port(), _free_port()
    await rt.apply_roster(
        Roster(roster_id="lab", version=1, entries=[_entry("ref#1", p1), _entry("ref#2", p2)])
    )
    assert set(rt._bound) == {"ref#1", "ref#2"}

    # Connect to ref#1 and send a valid reference 'time' frame; probation clears + routes.
    frame = bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])
    reader, writer = await asyncio.open_connection("127.0.0.1", p1)
    writer.write(frame)
    await writer.drain()
    await asyncio.sleep(0.2)
    writer.close()
    assert ("ref#1", frame) in frames  # routed to the right instance

    # Connect to ref#2 and send garbage; probation closes it (server stays up).
    r2, w2 = await asyncio.open_connection("127.0.0.1", p2)
    w2.write(b"\x00" * 5000)
    await w2.drain()
    await asyncio.sleep(0.2)
    w2.close()
    assert "ref#2" in rt._bound  # server itself still bound after rejecting one connection

    # Hot-reload: re-address ref#1 to a new port; only ref#1 rebinds.
    p1b = _free_port()
    original_1 = rt._bound["ref#1"].server
    original_2 = rt._bound["ref#2"].server
    await rt.apply_roster(
        Roster(roster_id="lab", version=2, entries=[_entry("ref#1", p1b), _entry("ref#2", p2)])
    )
    assert rt._bound["ref#1"].server is not original_1  # re-addressed -> rebound
    assert rt._bound["ref#2"].server is original_2  # unchanged -> same server object

    await rt.shutdown()
