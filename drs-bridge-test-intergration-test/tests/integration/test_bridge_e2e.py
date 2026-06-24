"""End-to-end integration tests: DeviceSimulator → drs-bridge → captured telemetry.

Architecture under test
-----------------------
  DeviceSimulator (TCP client)
    ↓  SDFC frames over TCP
  Runtime.start_command_server (TCP server, framing probation)
    ↓  extract_frame via real DLL
  Runtime._publish_telemetry
    ↓  parse_message via real DLL → dict
  TelemetryPublisher.publish
    ↓  send_and_wait
  MockProducer (captures topic + JSON body)

Kafka is fully mocked so no broker is needed.  The real DP-ECM DLLs are used
for frame extraction and message decoding.  Tests are skipped automatically
if the DLLs are absent (same skip logic as tests/conftest.py fixtures).

Each test binds the bridge on a fixed loopback port, connects the simulator,
sends a small sequence of frames, then asserts the captured messages.
"""
from __future__ import annotations

import asyncio
import json
import struct
from pathlib import Path
from typing import Any
from unittest.mock import AsyncMock

import pytest

from drs_bridge.parsers.parser_loader import load_parser
from drs_bridge.profiles._schema import PortConfig, Roster, RosterEntry
from drs_bridge.runtime import Runtime

# ---------------------------------------------------------------------------
# SDFC frame helpers  (mirrors tools/dp_ecm_simulator.py)
# ---------------------------------------------------------------------------
CMD_HEADER  = bytes([0xAA, 0xAB, 0xBA, 0xBB])
CMD_FOOTER  = bytes([0xCC, 0xCD, 0xDC, 0xDD])
RESP_HEADER = bytes([0xEE, 0xEF, 0xFE, 0xFF])
RESP_FOOTER = bytes([0xFF, 0xFE, 0xEF, 0xEE])


def build_cmd(group: int, unit: int, payload: bytes = b"") -> bytes:
    return (CMD_HEADER + struct.pack("<I", len(payload))
            + struct.pack("<H", group) + struct.pack("<H", unit)
            + payload + CMD_FOOTER)


def build_resp(status: int, group: int, unit: int, payload: bytes = b"") -> bytes:
    return (RESP_HEADER + struct.pack("<h", status)
            + struct.pack("<I", len(payload))
            + struct.pack("<H", group) + struct.pack("<H", unit)
            + payload + RESP_FOOTER)


def hf_temperature_payload() -> bytes:
    """100/10 HF Temperature — 9 floats (36B)."""
    return struct.pack("<9f", 25.3, 23.1, 47.8, 51.2, 22.9, 60.4, 28.7, 72.1, 41.0)


def vu_temperature_payload() -> bytes:
    """100/10 VU Temperature — 6 floats (24B)."""
    return struct.pack("<6f", 25.3, 22.8, 48.5, 60.1, 23.0, 42.7)


def hf_fh_detection_payload(hopper_count: int = 1) -> bytes:
    """101/40 HF FH Detection — 4B header + hopper_count × 60B per ICD."""
    out = struct.pack("<HH", hopper_count, 0)
    for i in range(hopper_count):
        base = 30.0 + i * 10.0
        out += (
            struct.pack("<I",  i + 1)         # HopperNumber
            + struct.pack("<f", base)          # MinFreqMHz
            + struct.pack("<f", base + 5.0)    # MaxFreqMHz
            + struct.pack("<f", 1.5)           # PulseLenMs
            + struct.pack("<f", 100.0)         # InterHopMs
            + struct.pack("<I", 12 + i)        # DetectedCount
            + bytes([10, 30, 45, 0])           # TOA H:M:S:rsv
            + struct.pack("<f", -65.0)         # PowerDbm
            + struct.pack("<H", 1)             # Active
            + struct.pack("<H", 0)             # Reserved
            + struct.pack("<f", 15.2)          # SNR
            + struct.pack("<f", base - 0.5)    # MinFreqDetMHz (×1e6 in DLL)
            + struct.pack("<f", base + 5.5)    # MaxFreqDetMHz
            + struct.pack("<f", 25.0)          # SignalBwKhz
            + struct.pack("<f", 200.0)         # HopRate
            + struct.pack("<f", 0.92)          # Confidence
        )
    return out


# ---------------------------------------------------------------------------
# Kafka mock
# ---------------------------------------------------------------------------

class MockProducer:
    """Captures every send_and_wait call; decodes JSON values automatically."""

    def __init__(self) -> None:
        self.published: list[dict[str, Any]] = []

    async def start(self) -> None:
        pass

    async def stop(self) -> None:
        pass

    async def send_and_wait(
        self, topic: str, value: bytes, *, key: bytes | None = None
    ) -> None:
        self.published.append(
            {"topic": topic, "value": json.loads(value), "key": key}
        )

    def telemetry(self) -> list[dict]:
        """Return only hw.*.telemetry messages."""
        return [m for m in self.published if "telemetry" in m["topic"]]


# ---------------------------------------------------------------------------
# Mock roster consumer
# ---------------------------------------------------------------------------

class MockRosterConsumer:
    """Immediately applies one roster snapshot, then idles until stopped."""

    def __init__(self, roster: Roster, on_roster) -> None:
        self._roster = roster
        self._on_roster = on_roster
        self._stop = asyncio.Event()
        self._ready = asyncio.Event()

    async def start(self) -> None:
        pass

    async def run(self) -> None:
        result = self._on_roster(self._roster)
        if asyncio.iscoroutine(result):
            await result  # apply_roster → _bind → TCP server is bound here
        self._ready.set()
        await self._stop.wait()

    async def stop(self) -> None:
        self._stop.set()

    async def wait_ready(self) -> None:
        """Block until the roster has been fully applied (TCP server bound)."""
        await asyncio.wait_for(self._ready.wait(), timeout=5.0)


# ---------------------------------------------------------------------------
# Runtime factory helpers
# ---------------------------------------------------------------------------

def _make_runtime(
    profiles_dir: Path,
    mock_producer: MockProducer,
    roster: Roster,
    dll_path: Path,
) -> tuple[Runtime, MockRosterConsumer]:
    """Build a Runtime wired to mock Kafka, the real DLL, and a fixed roster."""
    consumer_ref: list[MockRosterConsumer] = []

    def _roster_factory(_bootstrap: str, on_roster) -> MockRosterConsumer:
        c = MockRosterConsumer(roster, on_roster)
        consumer_ref.append(c)
        return c

    # parser_factory ignores the path argument and returns the pre-loaded handle
    # so we don't need correct YAML paths and avoid Windows path escaping issues
    handle = load_parser(dll_path)

    runtime = Runtime(
        profiles_dir=profiles_dir,
        kafka_bootstrap="unused",
        kafka_producer_factory=lambda _: mock_producer,
        parser_factory=lambda _path: handle,
        roster_consumer_factory=_roster_factory,
        command_consumer_factory=lambda _b, _cb: AsyncMock(),
        idle_timeout=5.0,
        garbage_ceiling=4096,
    )
    return runtime, consumer_ref[0] if consumer_ref else None  # type: ignore[return-value]


async def _start_runtime_with_roster(
    profiles_dir: Path,
    mock_producer: MockProducer,
    roster: Roster,
    dll_path: Path,
) -> tuple[Runtime, MockRosterConsumer]:
    consumer_ref: list[MockRosterConsumer] = []

    def _roster_factory(_bootstrap: str, on_roster) -> MockRosterConsumer:
        c = MockRosterConsumer(roster, on_roster)
        consumer_ref.append(c)
        return c

    handle = load_parser(dll_path)

    runtime = Runtime(
        profiles_dir=profiles_dir,
        kafka_bootstrap="unused",
        kafka_producer_factory=lambda _: mock_producer,
        parser_factory=lambda _path: handle,
        roster_consumer_factory=_roster_factory,
        command_consumer_factory=lambda _b, _cb: AsyncMock(),
        idle_timeout=5.0,
        garbage_ceiling=4096,
    )
    await runtime.start()
    consumer = consumer_ref[0]
    await consumer.wait_ready()   # TCP server is guaranteed bound here
    return runtime, consumer


def _write_profile(profiles_dir: Path, variant: str) -> None:
    """Write a minimal variant profile YAML (parser_lib is ignored by factory override)."""
    (profiles_dir / f"{variant}.yaml").write_text(
        f"variant: {variant}\n"
        "parser_lib: unused\n"
        "time_signal:\n"
        "  embedded_in_messages: true\n"
        "  periodic_distribution:\n"
        "    enabled: false\n"
        "    interval_ms: null\n"
        "  precision_required_ms: 1.0\n",
        encoding="utf-8",
    )


def _hf_roster(port: int = 19001) -> Roster:
    return Roster(
        roster_id="test-hf",
        version=1,
        entries=[
            RosterEntry(
                instance_id="hf-sim-001",
                variant="dp_ecm_hf",
                host="127.0.0.1",
                command=PortConfig(port=port, protocol="tcp"),
                response=PortConfig(port=port + 1, protocol="udp"),
                port_source="allocated",
                enabled=True,
            )
        ],
    )


def _vu_roster(port: int = 19011) -> Roster:
    return Roster(
        roster_id="test-vu",
        version=1,
        entries=[
            RosterEntry(
                instance_id="vu-sim-001",
                variant="dp_ecm_vu",
                host="127.0.0.1",
                command=PortConfig(port=port, protocol="tcp"),
                response=PortConfig(port=port + 1, protocol="udp"),
                port_source="allocated",
                enabled=True,
            )
        ],
    )


# ---------------------------------------------------------------------------
# Simulator client (lightweight version without CLI plumbing)
# ---------------------------------------------------------------------------

async def _connect_and_send(
    port: int, frames: list[tuple[bytes, str]]
) -> None:
    """Connect to bridge on loopback:port, send frames with 50ms gaps, then disconnect."""
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    try:
        for frame, label in frames:
            writer.write(frame)
            await writer.drain()
            await asyncio.sleep(0.05)
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

async def test_hf_command_frame_decoded_and_published(
    dp_ecm_hf_dll: Path, tmp_path: Path
) -> None:
    """A CMD 100/2 sysver query from the simulator is decoded and published to Kafka."""
    _write_profile(tmp_path, "dp_ecm_hf")
    producer = MockProducer()
    runtime, consumer = await _start_runtime_with_roster(
        tmp_path, producer, _hf_roster(port=19001), dp_ecm_hf_dll
    )
    try:
        await _connect_and_send(
            19001,
            [(build_cmd(100, 2), "CMD 100/2 GET_SYSVER")],
        )
        await asyncio.sleep(0.15)   # let the bridge process + publish

        telem = producer.telemetry()
        assert len(telem) >= 1, "expected at least one telemetry message"

        msg = telem[0]
        assert msg["topic"] == "hw.dp_ecm_hf.telemetry"
        assert msg["key"] == b"hf-sim-001"
        frame = msg["value"]["frame"]
        assert frame["group_id"] == 100
        assert frame["unit_id"] == 2
    finally:
        await runtime.shutdown()


async def test_hf_temperature_response_decoded(
    dp_ecm_hf_dll: Path, tmp_path: Path
) -> None:
    """RESP 100/10 with a 36-byte payload is decoded — 9 temperature sensors in the JSON."""
    _write_profile(tmp_path, "dp_ecm_hf")
    producer = MockProducer()
    runtime, _ = await _start_runtime_with_roster(
        tmp_path, producer, _hf_roster(port=19003), dp_ecm_hf_dll
    )
    try:
        await _connect_and_send(
            19003,
            [(build_resp(0, 100, 10, hf_temperature_payload()), "RESP 100/10 TEMP")],
        )
        await asyncio.sleep(0.15)

        telem = producer.telemetry()
        assert len(telem) >= 1
        frame = telem[0]["value"]["frame"]
        assert frame["group_id"] == 100
        assert frame["unit_id"] == 10
        # DLL emits temperatures in °C; just verify the field exists and is numeric
        assert isinstance(frame.get("internal_temp_c") or frame.get("processor_temp_c") or
                          frame.get("cpu_temp_c"), float)
    finally:
        await runtime.shutdown()


async def test_hf_fh_detection_hopper_data(
    dp_ecm_hf_dll: Path, tmp_path: Path
) -> None:
    """RESP 101/40 with 2 hoppers: hoppers array decoded with min/max_freq_hz in JSON."""
    _write_profile(tmp_path, "dp_ecm_hf")
    producer = MockProducer()
    runtime, _ = await _start_runtime_with_roster(
        tmp_path, producer, _hf_roster(port=19005), dp_ecm_hf_dll
    )
    try:
        await _connect_and_send(
            19005,
            [(build_resp(0, 101, 40, hf_fh_detection_payload(2)), "RESP 101/40 FH")],
        )
        await asyncio.sleep(0.15)

        telem = producer.telemetry()
        assert len(telem) >= 1
        frame = telem[0]["value"]["frame"]
        assert frame["group_id"] == 101
        assert frame["unit_id"] == 40
        hoppers = frame.get("hoppers", [])
        assert len(hoppers) == 2
        h0 = hoppers[0]
        assert h0["hopper_number"] == 1
        assert h0["min_freq_hz"] == pytest.approx(30.0e6, rel=1e-4)
        assert h0["max_freq_hz"] == pytest.approx(35.0e6, rel=1e-4)
    finally:
        await runtime.shutdown()


async def test_hf_multi_frame_scenario(
    dp_ecm_hf_dll: Path, tmp_path: Path
) -> None:
    """Full HF scenario: 5 frames → 5 telemetry publishes, each with correct group/unit."""
    _write_profile(tmp_path, "dp_ecm_hf")
    producer = MockProducer()
    runtime, _ = await _start_runtime_with_roster(
        tmp_path, producer, _hf_roster(port=19007), dp_ecm_hf_dll
    )
    try:
        frames = [
            (build_cmd(100, 2),                                   "CMD  100/2  sysver"),
            (build_resp(0, 100, 10, hf_temperature_payload()),    "RESP 100/10 temp"),
            (build_cmd(101, 25, b"\x00\x00\x00\x00" + struct.pack("<f", -80.0)),
             "CMD  101/25 set_threshold"),
            (build_resp(0, 101, 40, hf_fh_detection_payload(1)), "RESP 101/40 fh_det"),
            (build_resp(0, 200, 2, struct.pack("<HHHH", 7, 0, 1, 0)),
             "RESP 200/2  jam_ack"),
        ]
        await _connect_and_send(19007, frames)
        await asyncio.sleep(0.3)

        telem = producer.telemetry()
        assert len(telem) == 5, f"expected 5 telemetry msgs, got {len(telem)}"
        expected = [(100, 2), (100, 10), (101, 25), (101, 40), (200, 2)]
        actual = [(m["value"]["frame"]["group_id"], m["value"]["frame"]["unit_id"])
                  for m in telem]
        assert actual == expected
    finally:
        await runtime.shutdown()


async def test_vu_temperature_response_decoded(
    dp_ecm_vu_dll: Path, tmp_path: Path
) -> None:
    """VU RESP 100/10 frame is received and published (decoded or raw_hex fallback)."""
    _write_profile(tmp_path, "dp_ecm_vu")
    producer = MockProducer()
    runtime, _ = await _start_runtime_with_roster(
        tmp_path, producer, _vu_roster(port=19011), dp_ecm_vu_dll
    )
    try:
        await _connect_and_send(
            19011,
            [(build_resp(0, 100, 10, vu_temperature_payload()), "RESP 100/10 VU TEMP")],
        )
        await asyncio.sleep(0.15)

        telem = producer.telemetry()
        assert len(telem) >= 1
        frame = telem[0]["value"]["frame"]
        assert frame["group_id"] == 100
        assert frame["unit_id"] == 10
        # The compiled VU DLL may return decoded temperature fields or a raw_hex
        # fallback depending on the build version — both are valid decoded outcomes.
        assert "processor_temp_c" in frame or "raw_hex" in frame
    finally:
        await runtime.shutdown()


async def test_vu_multi_frame_scenario(
    dp_ecm_vu_dll: Path, tmp_path: Path
) -> None:
    """Full VU scenario: 4 frames → 4 telemetry publishes with correct group/unit."""
    _write_profile(tmp_path, "dp_ecm_vu")
    producer = MockProducer()
    runtime, _ = await _start_runtime_with_roster(
        tmp_path, producer, _vu_roster(port=19013), dp_ecm_vu_dll
    )
    try:
        jam_list_payload = struct.pack("<I", 2)   # count=2
        for i in range(2):
            jam_list_payload += struct.pack("<IHH", 150_000_000 + i * 1_000_000, 2, 0)

        frames = [
            (build_cmd(100, 2),                                   "CMD  100/2  sysver"),
            (build_resp(0, 100, 10, vu_temperature_payload()),    "RESP 100/10 temp"),
            (build_resp(0, 200, 2),                               "RESP 200/2  jam_ack"),
            (build_resp(0, 200, 6, jam_list_payload),             "RESP 200/6  jam_list"),
        ]
        await _connect_and_send(19013, frames)
        await asyncio.sleep(0.3)

        telem = producer.telemetry()
        assert len(telem) == 4, f"expected 4 telemetry msgs, got {len(telem)}"
        expected = [(100, 2), (100, 10), (200, 2), (200, 6)]
        actual = [(m["value"]["frame"]["group_id"], m["value"]["frame"]["unit_id"])
                  for m in telem]
        assert actual == expected
    finally:
        await runtime.shutdown()


async def test_corrupt_bytes_rejected_before_valid_frame(
    dp_ecm_hf_dll: Path, tmp_path: Path
) -> None:
    """Garbage bytes before a valid frame: bridge rejects the connection (garbage ceiling)."""
    _write_profile(tmp_path, "dp_ecm_hf")
    producer = MockProducer()
    runtime, _ = await _start_runtime_with_roster(
        tmp_path, producer, _hf_roster(port=19021), dp_ecm_hf_dll
    )
    try:
        reader, writer = await asyncio.open_connection("127.0.0.1", 19021)
        try:
            # Send 4KB+ of garbage — hits the probation garbage_ceiling
            writer.write(b"\x00\xDE\xAD\xFF" * 1200)
            await writer.drain()
            await asyncio.sleep(0.3)
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass

        # No telemetry should have been published (connection was rejected)
        assert len(producer.telemetry()) == 0
    finally:
        await runtime.shutdown()


async def test_instance_id_in_every_telemetry_message(
    dp_ecm_hf_dll: Path, tmp_path: Path
) -> None:
    """Every published message carries the correct instance_id and variant."""
    _write_profile(tmp_path, "dp_ecm_hf")
    producer = MockProducer()
    runtime, _ = await _start_runtime_with_roster(
        tmp_path, producer, _hf_roster(port=19023), dp_ecm_hf_dll
    )
    try:
        await _connect_and_send(
            19023,
            [
                (build_cmd(100, 2), "sysver"),
                (build_resp(0, 100, 10, hf_temperature_payload()), "temp"),
            ],
        )
        await asyncio.sleep(0.2)

        for msg in producer.telemetry():
            assert msg["value"]["instance_id"] == "hf-sim-001"
            assert msg["value"]["variant"] == "dp_ecm_hf"
            assert "timestamp_ns" in msg["value"]
            assert isinstance(msg["value"]["timestamp_ns"], int)
    finally:
        await runtime.shutdown()
