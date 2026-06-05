from pathlib import Path
from unittest.mock import AsyncMock, MagicMock

import pytest

from drs_bridge.runtime import Runtime


def _write_profile(path: Path, variant: str) -> None:
    path.write_text(
        f"""variant: {variant}
parser_lib: parsers/{variant}/parser.dll
ports:
  command:  {{ host: 127.0.0.1, port: 0, protocol: tcp }}
  response: {{ host: 127.0.0.1, port: 5001, protocol: udp }}
time_signal:
  embedded_in_messages: true
  periodic_distribution:
    enabled: false
    interval_ms: null
  precision_required_ms: 10
""",
        encoding="utf-8",
    )


@pytest.mark.asyncio
async def test_runtime_loads_profile_and_registers_variant(tmp_path: Path):
    _write_profile(tmp_path / "rdfs.yaml", "rdfs")

    fake_parser = MagicMock()
    fake_sender = AsyncMock()
    fake_producer = AsyncMock()

    runtime = Runtime(
        profiles_dir=tmp_path,
        kafka_producer_factory=lambda bootstrap: fake_producer,
        parser_factory=lambda dll_path: fake_parser,
        sender_factory=lambda host, port: fake_sender,
        command_server_factory=lambda host, port, handler: AsyncMock(),
        kafka_bootstrap="dummy:9092",
    )
    await runtime.start()

    assert runtime.bridge is not None
    assert "rdfs" in runtime.bridge._variant_tasks  # registered
    fake_producer.start.assert_awaited_once()

    await runtime.shutdown()
    fake_producer.stop.assert_awaited_once()


@pytest.mark.asyncio
async def test_runtime_empty_dir_starts_without_variants(tmp_path: Path):
    fake_producer = AsyncMock()
    runtime = Runtime(
        profiles_dir=tmp_path,
        kafka_producer_factory=lambda bootstrap: fake_producer,
        parser_factory=lambda dll_path: MagicMock(),
        sender_factory=lambda host, port: AsyncMock(),
        command_server_factory=lambda host, port, handler: AsyncMock(),
        kafka_bootstrap="dummy:9092",
    )
    await runtime.start()
    assert runtime.bridge._variant_tasks == {}
    await runtime.shutdown()
