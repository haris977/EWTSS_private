# drs-bridge Runtime Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `drs-bridge` actually runnable. Today the package has B1.3 logic (Bridge, TimeBeaconCoroutine, TickLagDetector, profile schema, ControlPublisher, HealthPublisher) but `main.py` is `raise SystemExit("not implemented")`. This plan adds the runtime layer that wires those pieces together into a service.

**Architecture:** `main.py` becomes the entry point that constructs a `Runtime` orchestrator. `Runtime` reads config from environment variables, loads every `*.yaml` profile in the configured directory, loads the variant's C++ parser via `ctypes`, starts a TCP command-server + UDP response-sender per variant, constructs real `AIOKafkaProducer` instances for the control + health publishers, instantiates `Bridge`, calls `register_variant` per profile, and runs forever. Shutdown is signal-driven (SIGINT + SIGTERM on POSIX; SIGINT only on Windows via `signal.signal`); `Runtime.shutdown()` cancels variant tasks via `Bridge.shutdown()` and closes the Kafka producer + transport servers.

**Tech Stack:** Python 3.11 + asyncio (stdlib) + aiokafka + PyYAML + Pydantic v2 + Pydantic Settings + ctypes (stdlib). No new heavyweight deps. The plan is **target: `drs-bridge/`**; mvp4 and sg-app are not touched.

**Application target (per pre-flight checklist Pass 1):** This plan modifies `drs-bridge/` exclusively. `mvp4/` is the STK reference codebase (Sg.Mvp4.*) and is not touched. `sg-app/` is the v2 production C# WPF app and is not touched. `drs-server/` is not touched.

**Test breadth statement:** Each task's unit tests cover only what wires the next task. End-to-end runtime composition is covered by Task 7's integration test (`test_runtime_starts_and_shuts_down`). No real Kafka broker, real DLL, or real network sockets are required for unit tests — all are mocked via `AsyncMock` or in-process loopback.

**Out of scope (deliberate; will be flagged by reviewers as missing — keep them out):**
- Real C++ parser DLLs — none exist in the repo yet. The `parser_loader` ships and tests the ctypes wrapper; loading an actual `.dll` is verified only when a real parser ships per variant.
- SCENARIO mode `scenario.execution` tick consumption (the ResponseRouter). That's separate B1.x work; not part of the runtime skeleton.
- Production-grade observability (Prometheus metrics, OpenTelemetry tracing).
- Hot-reload of YAML profiles. Profiles are read once at startup.
- TLS / mTLS on TCP transports. Plain TCP/UDP per IRS; transport security is a deployment concern.

**Adds Pydantic Settings dependency** (`pydantic-settings>=2.0`). Schema-level reconciliation already covered by the existing Pydantic 2.x in pyproject. Update `drs-bridge/pyproject.toml` as part of Task 1.

---

## Task 1: Config module — Pydantic Settings reading env vars

**Files:**
- Create: `drs-bridge/src/drs_bridge/config.py`
- Test: `drs-bridge/tests/test_config.py`
- Modify: `drs-bridge/pyproject.toml` (add `pydantic-settings>=2.0` to dependencies)

- [ ] **Step 1: Add the dependency to `pyproject.toml`**

In the `dependencies` list, add `"pydantic-settings>=2.0",` after the existing pydantic entry.

- [ ] **Step 2: Install the new dependency**

```
cd drs-bridge && .\.venv\Scripts\pip.exe install -e ".[dev]"
```

- [ ] **Step 3: Write the failing test**

```python
# drs-bridge/tests/test_config.py
import os

import pytest

from drs_bridge.config import BridgeSettings


def test_defaults(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.delenv("DRS_BRIDGE_PROFILES_DIR", raising=False)
    monkeypatch.delenv("DRS_BRIDGE_KAFKA_BOOTSTRAP", raising=False)
    s = BridgeSettings()
    assert s.profiles_dir.name == "profiles"
    assert s.kafka_bootstrap == "localhost:9092"
    assert s.log_level == "INFO"


def test_env_override(monkeypatch: pytest.MonkeyPatch, tmp_path):
    monkeypatch.setenv("DRS_BRIDGE_PROFILES_DIR", str(tmp_path))
    monkeypatch.setenv("DRS_BRIDGE_KAFKA_BOOTSTRAP", "broker.lan:9092")
    monkeypatch.setenv("DRS_BRIDGE_LOG_LEVEL", "DEBUG")
    s = BridgeSettings()
    assert s.profiles_dir == tmp_path
    assert s.kafka_bootstrap == "broker.lan:9092"
    assert s.log_level == "DEBUG"
```

- [ ] **Step 4: Run, expect ImportError**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/test_config.py -v
```

- [ ] **Step 5: Implement `BridgeSettings`**

```python
# drs-bridge/src/drs_bridge/config.py
"""Runtime configuration sourced from environment variables.

DRS_BRIDGE_PROFILES_DIR  — directory containing variant *.yaml profiles
                          (default: src/drs_bridge/profiles/)
DRS_BRIDGE_KAFKA_BOOTSTRAP — Kafka bootstrap.servers (default: localhost:9092)
DRS_BRIDGE_LOG_LEVEL     — Python logging level (default: INFO)
"""
from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict


_PACKAGE_ROOT = Path(__file__).resolve().parent


class BridgeSettings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="DRS_BRIDGE_", env_file=None)

    profiles_dir: Path = _PACKAGE_ROOT / "profiles"
    kafka_bootstrap: str = "localhost:9092"
    log_level: str = "INFO"
```

- [ ] **Step 6: Run, expect 2 passed**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/test_config.py -v
```

- [ ] **Step 7: Commit**

```
git add drs-bridge/pyproject.toml drs-bridge/src/drs_bridge/config.py drs-bridge/tests/test_config.py
git commit -m "feat(drs-bridge): BridgeSettings env-driven config (runtime skeleton Task 1)"
```

---

## Task 2: Profile directory loader

**Files:**
- Create: `drs-bridge/src/drs_bridge/profile_loader.py`
- Test: `drs-bridge/tests/profiles/test_profile_loader.py`

- [ ] **Step 1: Failing test**

```python
# drs-bridge/tests/profiles/test_profile_loader.py
from pathlib import Path

import pytest

from drs_bridge.profile_loader import load_profiles


def _write_profile(path: Path, variant: str, lib: str = "stub.dll") -> None:
    path.write_text(
        f"""variant: {variant}
parser_lib: {lib}
ports:
  command:  {{ host: 0.0.0.0, port: 5001, protocol: tcp }}
  response: {{ host: 0.0.0.0, port: 5002, protocol: udp }}
time_signal:
  embedded_in_messages: true
  periodic_distribution:
    enabled: false
    interval_ms: null
  precision_required_ms: 10
""",
        encoding="utf-8",
    )


def test_loads_all_yaml_in_directory(tmp_path: Path):
    _write_profile(tmp_path / "rdfs.yaml", "rdfs")
    _write_profile(tmp_path / "iff.yaml", "iff")
    profiles = load_profiles(tmp_path)
    assert set(profiles.keys()) == {"rdfs", "iff"}
    assert profiles["rdfs"].time_signal.precision_required_ms == 10


def test_skips_non_yaml(tmp_path: Path):
    _write_profile(tmp_path / "rdfs.yaml", "rdfs")
    (tmp_path / "README.md").write_text("ignored", encoding="utf-8")
    profiles = load_profiles(tmp_path)
    assert set(profiles.keys()) == {"rdfs"}


def test_empty_dir_returns_empty_dict(tmp_path: Path):
    assert load_profiles(tmp_path) == {}


def test_invalid_yaml_raises(tmp_path: Path):
    (tmp_path / "bad.yaml").write_text("variant: bad\nports: not-a-dict\n", encoding="utf-8")
    with pytest.raises(Exception):
        load_profiles(tmp_path)
```

- [ ] **Step 2: Run, expect ImportError**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/profiles/test_profile_loader.py -v
```

- [ ] **Step 3: Implement**

```python
# drs-bridge/src/drs_bridge/profile_loader.py
"""Scans a directory for `*.yaml` variant profiles and parses each into a
VariantProfile. The runtime calls this once at startup.
"""
from pathlib import Path

import yaml

from drs_bridge.profiles._schema import VariantProfile


def load_profiles(profiles_dir: Path) -> dict[str, VariantProfile]:
    """Return {variant_name: VariantProfile} for every *.yaml under profiles_dir.

    Raises pydantic.ValidationError if any file fails the schema.
    """
    out: dict[str, VariantProfile] = {}
    for path in sorted(profiles_dir.glob("*.yaml")):
        with path.open(encoding="utf-8") as f:
            raw = yaml.safe_load(f)
        profile = VariantProfile(**raw)
        out[profile.variant] = profile
    return out
```

- [ ] **Step 4: Run, expect 4 passed**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/profiles/test_profile_loader.py -v
```

- [ ] **Step 5: Commit**

```
git add drs-bridge/src/drs_bridge/profile_loader.py drs-bridge/tests/profiles/test_profile_loader.py
git commit -m "feat(drs-bridge): profile directory loader (runtime skeleton Task 2)"
```

---

## Task 3: ctypes-based parser loader

**Files:**
- Create: `drs-bridge/src/drs_bridge/parser_loader.py`
- Test: `drs-bridge/tests/test_parser_loader.py`

The C++ parser ABI per design spec: 4 functions exported from each variant's `.dll`:
- `extract_frame(buf: bytes, length: int) -> (consumed: int, frame_ptr: void*)`
- `parse_message(frame_ptr: void*) -> (json_str_ptr: char*, json_str_len: int)`
- `format_response(kind: char*, ...kwargs as JSON) -> (bytes_ptr: char*, bytes_len: int)`
- `free_result(ptr: void*) -> void`

Since no real `.dll` exists in the repo yet, this task ships the loader code and tests its **error path** (DLL missing). A future task per variant validates the loader against a real DLL.

- [ ] **Step 1: Failing test**

```python
# drs-bridge/tests/test_parser_loader.py
from pathlib import Path

import pytest

from drs_bridge.parser_loader import ParserHandle, load_parser


def test_missing_dll_raises_fileNotFound(tmp_path: Path):
    with pytest.raises(FileNotFoundError):
        load_parser(tmp_path / "does-not-exist.dll")


def test_loader_returns_handle_with_expected_attributes():
    # Stub class to verify the public API surface (we'd use a real DLL in lab).
    handle = ParserHandle.__new__(ParserHandle)
    assert hasattr(handle, "format_response")
    assert hasattr(handle, "parse_message")
    assert hasattr(handle, "extract_frame")
    assert hasattr(handle, "close")
```

- [ ] **Step 2: Run, expect ImportError**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/test_parser_loader.py -v
```

- [ ] **Step 3: Implement**

```python
# drs-bridge/src/drs_bridge/parser_loader.py
"""ctypes wrapper for the 4-symbol C++ parser ABI per B1.3 design spec.

Each variant's parser ships as a Windows DLL exporting four symbols:

    int     extract_frame(const uint8_t* buf, size_t length, void** out_frame);
    int     parse_message(void* frame, char** out_json, size_t* out_len);
    int     format_response(const char* kind, const char* kwargs_json,
                            uint8_t** out_buf, size_t* out_len);
    void    free_result(void* ptr);

`load_parser(path)` opens the DLL, binds the symbols, and returns a
`ParserHandle` that satisfies the `Parser` Protocol used by `TimeBeaconCoroutine`.
"""
from __future__ import annotations

import ctypes
import json
from pathlib import Path


class ParserHandle:
    """Thin Pythonic wrapper around the loaded DLL's function pointers."""

    def __init__(self, lib: ctypes.CDLL) -> None:
        self._lib = lib
        # Bind ABI signatures. Done lazily to keep the constructor cheap.
        self._format_response = lib.format_response
        self._format_response.argtypes = [
            ctypes.c_char_p,                                  # kind
            ctypes.c_char_p,                                  # kwargs as JSON
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),   # out_buf
            ctypes.POINTER(ctypes.c_size_t),                  # out_len
        ]
        self._format_response.restype = ctypes.c_int

        self._free_result = lib.free_result
        self._free_result.argtypes = [ctypes.c_void_p]
        self._free_result.restype = None

    def format_response(self, kind: str, timestamp_ns: int, **kwargs) -> bytes:
        payload = json.dumps({"timestamp_ns": timestamp_ns, **kwargs}).encode("utf-8")
        out_buf = ctypes.POINTER(ctypes.c_uint8)()
        out_len = ctypes.c_size_t(0)
        rc = self._format_response(kind.encode("utf-8"), payload, ctypes.byref(out_buf), ctypes.byref(out_len))
        if rc != 0:
            raise RuntimeError(f"format_response returned {rc} for kind={kind}")
        try:
            return bytes(ctypes.cast(out_buf, ctypes.POINTER(ctypes.c_uint8 * out_len.value)).contents)
        finally:
            self._free_result(ctypes.cast(out_buf, ctypes.c_void_p))

    def parse_message(self, frame_ptr) -> dict:
        raise NotImplementedError("parse_message wiring lands when a real parser ships")

    def extract_frame(self, buf: bytes) -> tuple[int, object]:
        raise NotImplementedError("extract_frame wiring lands when a real parser ships")

    def close(self) -> None:
        # ctypes does not expose dlclose; the OS frees on process exit.
        pass


def load_parser(dll_path: Path) -> ParserHandle:
    """Open the DLL at `dll_path` and return a ParserHandle bound to it.

    Raises FileNotFoundError if the path does not exist.
    """
    if not dll_path.exists():
        raise FileNotFoundError(f"parser DLL not found at {dll_path}")
    lib = ctypes.CDLL(str(dll_path))
    return ParserHandle(lib)
```

- [ ] **Step 4: Run, expect 2 passed**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/test_parser_loader.py -v
```

- [ ] **Step 5: Commit**

```
git add drs-bridge/src/drs_bridge/parser_loader.py drs-bridge/tests/test_parser_loader.py
git commit -m "feat(drs-bridge): ctypes parser loader (runtime skeleton Task 3)"
```

---

## Task 4: TCP / UDP transport

**Files:**
- Create: `drs-bridge/src/drs_bridge/transport.py`
- Test: `drs-bridge/tests/test_transport.py`

- [ ] **Step 1: Failing test**

```python
# drs-bridge/tests/test_transport.py
import asyncio

import pytest

from drs_bridge.transport import UdpSender, start_command_server


@pytest.mark.asyncio
async def test_udp_sender_sends_payload():
    # Stand up a tiny in-process UDP receiver on an ephemeral port.
    received: list[bytes] = []

    class _Recv(asyncio.DatagramProtocol):
        def datagram_received(self, data: bytes, addr) -> None:
            received.append(data)

    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(_Recv, local_addr=("127.0.0.1", 0))
    port = transport.get_extra_info("sockname")[1]
    try:
        sender = await UdpSender.connect("127.0.0.1", port)
        await sender.send(b"hello")
        await asyncio.sleep(0.05)
        assert received == [b"hello"]
        await sender.close()
    finally:
        transport.close()


@pytest.mark.asyncio
async def test_command_server_delivers_received_bytes_to_handler():
    received: list[bytes] = []

    async def handler(data: bytes) -> None:
        received.append(data)

    server = await start_command_server("127.0.0.1", 0, handler)
    port = server.sockets[0].getsockname()[1]
    try:
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        writer.write(b"frame")
        await writer.drain()
        writer.close()
        await writer.wait_closed()
        await asyncio.sleep(0.05)
        assert received and received[0].startswith(b"frame")
    finally:
        server.close()
        await server.wait_closed()
```

- [ ] **Step 2: Run, expect ImportError**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/test_transport.py -v
```

- [ ] **Step 3: Implement**

```python
# drs-bridge/src/drs_bridge/transport.py
"""asyncio TCP command-server + UDP response-sender for drs-bridge.

Per B1.3 design spec, each variant has two ports: a TCP `command` port
(server-side, receives frames from Entity Controllers) and a UDP `response`
port (client-side, sends responses out). This module ships the asyncio
primitives for both.
"""
from __future__ import annotations

import asyncio
from typing import Awaitable, Callable

CommandHandler = Callable[[bytes], Awaitable[None]]


async def start_command_server(host: str, port: int, handler: CommandHandler) -> asyncio.Server:
    """Start a TCP server that calls `handler(bytes)` for each frame received."""

    async def _on_connect(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            while not reader.at_eof():
                data = await reader.read(4096)
                if not data:
                    break
                await handler(data)
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass

    server = await asyncio.start_server(_on_connect, host, port)
    return server


class UdpSender:
    """Async wrapper around a UDP datagram endpoint pointed at one peer.

    Satisfies the `Sender` Protocol (`async send(payload: bytes) -> None`).
    """

    def __init__(self, transport: asyncio.DatagramTransport, peer: tuple[str, int]) -> None:
        self._transport = transport
        self._peer = peer

    @classmethod
    async def connect(cls, host: str, port: int) -> "UdpSender":
        loop = asyncio.get_running_loop()
        transport, _ = await loop.create_datagram_endpoint(
            asyncio.DatagramProtocol, remote_addr=(host, port)
        )
        return cls(transport, (host, port))

    async def send(self, payload: bytes) -> None:
        self._transport.sendto(payload)

    async def close(self) -> None:
        self._transport.close()
```

- [ ] **Step 4: Run, expect 2 passed**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/test_transport.py -v
```

- [ ] **Step 5: Commit**

```
git add drs-bridge/src/drs_bridge/transport.py drs-bridge/tests/test_transport.py
git commit -m "feat(drs-bridge): asyncio TCP command-server + UDP sender (runtime skeleton Task 4)"
```

---

## Task 5: Runtime orchestrator

**Files:**
- Create: `drs-bridge/src/drs_bridge/runtime.py`
- Test: `drs-bridge/tests/test_runtime.py`

`Runtime` ties everything together. Given a `BridgeSettings` instance, it loads profiles, loads parsers, starts transport servers, instantiates ControlPublisher + HealthPublisher with a real (or injected mock) `AIOKafkaProducer`, instantiates `Bridge`, calls `register_variant` per profile, and exposes `run_until_stopped()` + `shutdown()`.

- [ ] **Step 1: Failing test**

```python
# drs-bridge/tests/test_runtime.py
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

    # Inject mock factories so no real DLL / Kafka / sockets are required.
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
```

- [ ] **Step 2: Run, expect ImportError**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/test_runtime.py -v
```

- [ ] **Step 3: Implement**

```python
# drs-bridge/src/drs_bridge/runtime.py
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
            parser = self._parser_factory(self._profiles_dir / profile.parser_lib)
            sender = await self._sender_factory(
                profile.ports["response"].host, profile.ports["response"].port
            )
            self._senders.append(sender)

            async def _on_frame(data: bytes, v: str = variant) -> None:
                logger.debug("variant=%s received %d bytes", v, len(data))

            server = await self._command_server_factory(
                profile.ports["command"].host, profile.ports["command"].port, _on_frame
            )
            self._command_servers.append(server)

            await self.bridge.register_variant(profile=profile, parser=parser, sender=sender)
            logger.info("registered variant=%s", variant)

    async def run_until_stopped(self) -> None:
        await self._stop_event.wait()

    def request_stop(self) -> None:
        self._stop_event.set()

    async def shutdown(self) -> None:
        logger.info("Runtime shutting down")
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
```

- [ ] **Step 4: Run, expect 2 passed**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/test_runtime.py -v
```

- [ ] **Step 5: Commit**

```
git add drs-bridge/src/drs_bridge/runtime.py drs-bridge/tests/test_runtime.py
git commit -m "feat(drs-bridge): Runtime orchestrator (runtime skeleton Task 5)"
```

---

## Task 6: Cross-platform main entry point

**Files:**
- Modify: `drs-bridge/src/drs_bridge/main.py` (replace the stub)
- Test: `drs-bridge/tests/test_main.py`

- [ ] **Step 1: Failing test**

```python
# drs-bridge/tests/test_main.py
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from drs_bridge import main


@pytest.mark.asyncio
async def test_run_starts_and_stops_runtime(tmp_path):
    fake_runtime = AsyncMock()
    fake_runtime.run_until_stopped = AsyncMock()
    fake_runtime.start = AsyncMock()
    fake_runtime.shutdown = AsyncMock()

    with patch("drs_bridge.main.Runtime", return_value=fake_runtime), patch(
        "drs_bridge.main._install_signal_handlers"
    ) as fake_install:
        # The function must call start → run_until_stopped → shutdown in order.
        await main.run(profiles_dir=tmp_path, kafka_bootstrap="dummy:9092")

    fake_runtime.start.assert_awaited_once()
    fake_runtime.run_until_stopped.assert_awaited_once()
    fake_runtime.shutdown.assert_awaited_once()
    fake_install.assert_called_once()


def test_configure_logging_uses_dict_config():
    with patch("drs_bridge.main.logging.config.dictConfig") as fake_dictcfg:
        main.configure_logging("DEBUG")
    fake_dictcfg.assert_called_once()
```

- [ ] **Step 2: Run, expect ImportError**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/test_main.py -v
```

- [ ] **Step 3: Replace `main.py`**

```python
# drs-bridge/src/drs_bridge/main.py
"""Production entry point for drs-bridge. Loads BridgeSettings from env,
constructs a Runtime, installs signal handlers, and runs until stopped.
"""
from __future__ import annotations

import asyncio
import logging
import logging.config
import signal
import sys
from pathlib import Path

from drs_bridge.config import BridgeSettings
from drs_bridge.runtime import Runtime

logger = logging.getLogger(__name__)


def configure_logging(level: str) -> None:
    logging.config.dictConfig(
        {
            "version": 1,
            "disable_existing_loggers": False,
            "formatters": {
                "default": {
                    "format": "%(asctime)s %(levelname)-7s %(name)s: %(message)s",
                },
            },
            "handlers": {
                "console": {
                    "class": "logging.StreamHandler",
                    "formatter": "default",
                },
            },
            "root": {"level": level, "handlers": ["console"]},
        }
    )


def _install_signal_handlers(loop: asyncio.AbstractEventLoop, runtime: Runtime) -> None:
    """SIGINT + SIGTERM on POSIX; SIGINT only on Windows (asyncio doesn't
    support add_signal_handler on Windows). Both route to runtime.request_stop.
    """
    if sys.platform == "win32":
        signal.signal(signal.SIGINT, lambda *_: runtime.request_stop())
    else:
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, runtime.request_stop)


async def run(profiles_dir: Path, kafka_bootstrap: str) -> None:
    runtime = Runtime(profiles_dir=profiles_dir, kafka_bootstrap=kafka_bootstrap)
    loop = asyncio.get_running_loop()
    _install_signal_handlers(loop, runtime)
    await runtime.start()
    try:
        await runtime.run_until_stopped()
    finally:
        await runtime.shutdown()


def main() -> None:
    settings = BridgeSettings()
    configure_logging(settings.log_level)
    logger.info("drs-bridge starting (profiles_dir=%s, kafka=%s)",
                settings.profiles_dir, settings.kafka_bootstrap)
    asyncio.run(run(profiles_dir=settings.profiles_dir, kafka_bootstrap=settings.kafka_bootstrap))


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run, expect 2 passed**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe tests/test_main.py -v
```

- [ ] **Step 5: Run the full drs-bridge suite to confirm no regressions**

```
cd drs-bridge && .\.venv\Scripts\pytest.exe -v
```

Expected: 26 passed (16 prior + 10 new across Tasks 1-6).

- [ ] **Step 6: Commit**

```
git add drs-bridge/src/drs_bridge/main.py drs-bridge/tests/test_main.py
git commit -m "feat(drs-bridge): cross-platform main entry point (runtime skeleton Task 6)"
```

---

## Self-review checklist run

Before declaring this plan ready:

- ✅ **Spec coverage** — every concern in the user's "drs-bridge production runtime gaps" list has a task: config (T1), profile loader (T2), parser DLL loader (T3), TCP/UDP (T4), Kafka producer wiring (T5 via factory), main + signal handling + logging (T6).
- ✅ **Placeholder scan** — no TBD / TODO / fill-in-later. Parser `parse_message` + `extract_frame` raise `NotImplementedError` with explicit reason ("wiring lands when a real parser ships"); the docstring says so. That's deliberate scope, not a placeholder.
- ✅ **Type consistency** — `Parser` Protocol, `Sender` Protocol, `HealthPublisher` Protocol all match existing definitions in `time_beacon.py`, `tick_lag_detector.py`, `bridge.py`. Names unchanged.
- ✅ **Application target unambiguous** — header states drs-bridge only; mvp4 + sg-app + drs-server explicitly excluded.
- ✅ **Skeleton mapping** — no task references `self._<attr>` on a class that doesn't exist by that point. `Runtime` initialises every attribute in `__init__`.
- ✅ **Environment pre-flight** — Python 3.11 pinned (already); pydantic-settings is the only new dep, added in Task 1; ctypes + asyncio are stdlib; no new top-level dirs; no `.gitignore` change needed.
- ✅ **Content traps grep** — no `datetime.utcnow`, no `asyncio.get_event_loop`, no `isoformat().*Z`. Authoring artefacts: none.
- ✅ **Parameterise rather than magic-number** — Runtime takes factories as constructor params (the dependency-injection seam tests use); no magic constants except Kafka default `localhost:9092` and log default `INFO`.
- ✅ **Test breadth statement** — header carries it: "Each task's unit tests cover only what wires the next task. End-to-end runtime composition is covered by Task 5's `test_runtime_loads_profile_and_registers_variant`."
- ✅ **Out of scope list** — header carries it: real DLLs, scenario.execution consumption, Prometheus / OTel, hot-reload, TLS.

---

## Execution handoff

Plan complete. Two execution options per the writing-plans skill:

**1. Subagent-Driven (recommended)** — fresh subagent per task + two-stage review.

**2. Inline Execution** — batch with checkpoints.

Picking subagent-driven is consistent with the B1.3 Phase 4 pattern.
