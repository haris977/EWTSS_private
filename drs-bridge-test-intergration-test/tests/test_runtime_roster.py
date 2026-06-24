import asyncio
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock

import pytest

from drs_bridge.runtime import Runtime
from drs_bridge.profiles._schema import Roster


def _write_profile(profiles_dir: Path, variant: str) -> None:
    (profiles_dir / f"{variant}.yaml").write_text(
        f"""variant: {variant}
parser_lib: parsers/{variant}/parser.dll
time_signal:
  embedded_in_messages: true
  periodic_distribution:
    enabled: false
    interval_ms: null
  precision_required_ms: 10
""",
        encoding="utf-8",
    )


def _roster(*instances) -> Roster:
    entries = [
        {
            "instance_id": iid,
            "variant": variant,
            "host": "127.0.0.1",
            "command": {"port": 0, "protocol": "tcp"},
            "response": {"port": 0, "protocol": "udp"},
            "port_source": "irs_fixed",
            "enabled": enabled,
        }
        for (iid, variant, enabled) in instances
    ]
    return Roster(roster_id="t", version=1, entries=entries)


def _fake_server():
    """Server double: sync close() (like asyncio.Server) + awaitable wait_closed()."""
    srv = MagicMock()
    srv.wait_closed = AsyncMock()
    return srv


def _runtime(profiles_dir, servers):
    """Runtime with all I/O faked; command_server_factory records bind calls."""

    def server_factory(host, port, **kw):
        srv = _fake_server()
        servers.append((kw["instance_id"], srv))
        return srv

    return Runtime(
        profiles_dir=profiles_dir,
        kafka_bootstrap="dummy:9092",
        kafka_producer_factory=lambda b: AsyncMock(),
        parser_factory=lambda p: MagicMock(extract_frame=lambda buf: (-1, None)),
        sender_factory=lambda host, port: AsyncMock(),
        command_server_factory=server_factory,
        roster_consumer_factory=lambda bootstrap, on_roster: AsyncMock(),
        command_consumer_factory=lambda bootstrap, on_command: AsyncMock(),
    )


@pytest.mark.asyncio
async def test_apply_roster_binds_one_server_per_enabled_instance(tmp_path):
    _write_profile(tmp_path, "rdfs")
    servers = []
    rt = _runtime(tmp_path, servers)
    await rt.start()
    await rt.apply_roster(
        _roster(("rdfs#1", "rdfs", True), ("rdfs#2", "rdfs", True), ("rdfs#9", "rdfs", False))
    )
    bound = sorted(iid for iid, _ in servers)
    assert bound == ["rdfs#1", "rdfs#2"]  # disabled rdfs#9 not bound
    await rt.shutdown()


@pytest.mark.asyncio
async def test_parser_loaded_once_per_variant(tmp_path):
    _write_profile(tmp_path, "rdfs")
    calls = []
    rt = Runtime(
        profiles_dir=tmp_path,
        kafka_bootstrap="d:9092",
        kafka_producer_factory=lambda b: AsyncMock(),
        parser_factory=lambda p: calls.append(p) or MagicMock(extract_frame=lambda buf: (-1, None)),
        sender_factory=lambda host, port: AsyncMock(),
        command_server_factory=lambda host, port, **kw: _fake_server(),
        roster_consumer_factory=lambda bootstrap, on_roster: AsyncMock(),
        command_consumer_factory=lambda bootstrap, on_command: AsyncMock(),
    )
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True), ("rdfs#2", "rdfs", True)))
    assert len(calls) == 1  # one .dll load for two rdfs instances
    await rt.shutdown()


@pytest.mark.asyncio
async def test_hot_reload_adds_removes_and_keeps_unaffected(tmp_path):
    _write_profile(tmp_path, "rdfs")
    servers = []
    rt = _runtime(tmp_path, servers)
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True), ("rdfs#2", "rdfs", True)))
    first_for_1 = rt._bound["rdfs#1"].server

    # v2: drop #2, add #3, leave #1 untouched.
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True), ("rdfs#3", "rdfs", True)))
    assert set(rt._bound) == {"rdfs#1", "rdfs#3"}
    assert rt._bound["rdfs#1"].server is first_for_1  # unaffected: same server object
    await rt.shutdown()


@pytest.mark.asyncio
async def test_hot_reload_rebinds_on_address_change(tmp_path):
    _write_profile(tmp_path, "rdfs")
    servers = []
    rt = _runtime(tmp_path, servers)
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True)))
    original = rt._bound["rdfs#1"].server

    # Same id, different command port -> rebind.
    changed = Roster(
        roster_id="t",
        version=2,
        entries=[
            {
                "instance_id": "rdfs#1",
                "variant": "rdfs",
                "host": "127.0.0.1",
                "command": {"port": 12345, "protocol": "tcp"},
                "response": {"port": 0, "protocol": "udp"},
                "port_source": "irs_fixed",
                "enabled": True,
            }
        ],
    )
    await rt.apply_roster(changed)
    assert rt._bound["rdfs#1"].server is not original  # rebound
    await rt.shutdown()


@pytest.mark.asyncio
async def test_unknown_variant_isolated(tmp_path):
    _write_profile(tmp_path, "rdfs")
    servers = []
    rt = _runtime(tmp_path, servers)
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True), ("ghost#1", "ghost", True)))
    assert set(rt._bound) == {"rdfs#1"}  # ghost isolated, rdfs bound
    await rt.shutdown()


@pytest.mark.asyncio
async def test_shutdown_leaves_no_leaked_tasks(tmp_path):
    _write_profile(tmp_path, "rdfs")
    servers = []
    rt = _runtime(tmp_path, servers)
    before = {t for t in asyncio.all_tasks()}
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True)))
    await rt.shutdown()
    await asyncio.sleep(0)  # let cancellations settle
    after = {t for t in asyncio.all_tasks() if not t.done()}
    leaked = after - before - {asyncio.current_task()}
    assert not leaked, f"leaked tasks: {leaked}"
