# DRS Bridge — Project Overview

## What This Project Does

DRS Bridge is a Python service that connects physical electronic warfare (EW) hardware to a software back-end called **drs-server** via Apache Kafka.

The hardware devices are radio frequency jammers/detectors:
- **DP-ECM-1071** — HF (High Frequency) jammer/receiver
- **DP-ECM-1074** — VU (VHF/UHF) jammer/receiver

The bridge decodes binary radio frames from the hardware, converts them to JSON, and publishes them to Kafka. In the other direction it receives JSON commands from the drs-server, re-encodes them to binary frames, and sends them back to the hardware.

---

## High-Level Architecture

```
  Hardware Device (DP-ECM-1071 / DP-ECM-1074)
        │  TCP (device connects to bridge)
        │  sends binary SDFC frames
        ▼
  ┌─────────────────────────────────────────┐
  │           DRS BRIDGE (Python)           │
  │                                         │
  │  Transport Layer                        │
  │   └─ TCP server  (receives CMD/RESP)    │
  │   └─ UDP sender  (sends encoded frames) │
  │                                         │
  │  ctypes ↔ C++ DLL (per variant)        │
  │   └─ extract_frame()  frame detection   │
  │   └─ parse_message()  binary → JSON     │
  │   └─ format_response() JSON → binary    │
  │                                         │
  │  Kafka Layer                            │
  │   └─ TelemetryPublisher → hw.*.telemetry│
  │   └─ CommandConsumer  ← drs.commands    │
  │   └─ RosterConsumer   ← drs.roster      │
  │   └─ HealthPublisher  → drs.health      │
  └─────────────────────────────────────────┘
        │  Kafka topics
        ▼
  DRS-SERVER (separate service)
```

---

## Wire Protocol — SDFC Frames

All hardware communication uses the **SDFC** binary framing protocol.

Every frame has:
- A 4-byte **magic header** identifying the frame type
- A fixed header with status, payload size, group ID, unit ID
- Variable-length payload
- A 4-byte **magic footer**

| Direction | Header (hex) | Footer (hex) | Meaning |
|-----------|--------------|--------------|---------|
| Device → Bridge | `AA AB BA BB` | `CC CD DC DD` | CMD — device requests a reply |
| Device → Bridge | `EE EF FE FF` | `FF FE EF EE` | RESP — device pushes data/ack |
| Bridge → Device | `EE EF FE FF` | `FF FE EF EE` | RESP — bridge replies to device |

The frame body layout (little-endian):
```
[status u16][size u32][group u16][unit u16][payload: size bytes]
```

**group** and **unit** together identify the ICD command (e.g. group=101, unit=70 → FF Detection Report).

---

## C++ DLL ABI

Each hardware variant ships as a Windows DLL. The DLL exports exactly **4 C symbols** — no name mangling, stable ABI:

```c
// Scan a raw TCP byte buffer for a complete SDFC frame.
// Returns: 1=cmd, 2=resp, 3=stream, 0=need more bytes, -1=corrupt.
int extract_frame(const uint8_t* buf, int buf_len,
                  uint8_t* out_frame, int* out_len);

// Decode a complete frame into a heap-allocated UTF-8 JSON string.
// Returns: JSON string pointer (caller must free), or nullptr on failure.
const char* parse_message(const uint8_t* frame, int frame_len, int frame_type);

// Encode a JSON command object into a binary DRS→device response frame.
// Returns: bytes written, or -1 on error.
int format_response(const char* json_response, uint8_t* out_frame);

// Free the memory returned by parse_message(). Safe with nullptr.
void free_result(const char* result);
```

The DLL dispatch tables are keyed by `(group_id, unit_id)`. Each entry is a C++ function that unpacks the binary payload and writes named JSON fields.

---

## Python ↔ C++ Integration (ctypes)

**File:** `src/drs_bridge/parsers/parser_loader.py`

This is the **only** place where Python talks to C++. The class `ParserHandle` wraps the DLL using Python's `ctypes` library.

### Loading the DLL

```python
# parser_loader.py:198-211
def load_parser(dll_path: Path) -> ParserHandle:
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(dll_path.resolve().parent))  # find sibling MinGW DLLs
    lib = ctypes.CDLL(str(dll_path))
    return ParserHandle(lib)
```

### Binding the 4 symbols (ParserHandle.__init__)

```python
# parser_loader.py:66-108
self._extract_frame = lib.extract_frame
self._extract_frame.argtypes = [
    ctypes.POINTER(ctypes.c_uint8),   # buf
    ctypes.c_int,                      # buf_len
    ctypes.POINTER(ctypes.c_uint8),   # out_frame (caller-allocated)
    ctypes.POINTER(ctypes.c_int),     # out_len
]
self._extract_frame.restype = ctypes.c_int

self._parse_message = lib.parse_message
self._parse_message.argtypes = [
    ctypes.POINTER(ctypes.c_uint8),   # frame
    ctypes.c_int,                      # frame_len
    ctypes.c_int,                      # frame_type
]
self._parse_message.restype = ctypes.c_void_p  # raw ptr — must free_result()

self._format_response = lib.format_response
self._format_response.argtypes = [
    ctypes.c_char_p,                   # json_response (UTF-8)
    ctypes.POINTER(ctypes.c_uint8),   # out_frame (caller-allocated)
]
self._format_response.restype = ctypes.c_int

self._free_result = lib.free_result
self._free_result.argtypes = [ctypes.c_void_p]
self._free_result.restype = None
```

### Calling the DLL at runtime

**Frame detection** (called by the TCP server on every read):
```python
# parser_loader.py:114-140
def extract_frame(self, buf: bytes) -> tuple[int, bytes | None]:
    in_buf  = (ctypes.c_uint8 * len(buf)).from_buffer_copy(buf)
    out_frame = (ctypes.c_uint8 * _MAX_FRAME_BUFFER_BYTES)()   # 1 MB + 64 B
    out_len = ctypes.c_int(0)
    rc = self._extract_frame(in_buf, len(buf), out_frame, ctypes.byref(out_len))
    if rc <= 0:
        return rc, None
    return 0, bytes(out_frame[: out_len.value])
```

**Frame decoding** (called once a complete frame is extracted):
```python
# parser_loader.py:146-168
def parse_message(self, frame: bytes, frame_type: int) -> dict | None:
    in_buf = (ctypes.c_uint8 * len(frame)).from_buffer_copy(frame)
    result_ptr = self._parse_message(in_buf, len(frame), frame_type)
    if not result_ptr:
        return None
    try:
        json_bytes = ctypes.cast(result_ptr, ctypes.c_char_p).value
        return json.loads(json_bytes.decode("utf-8"))
    finally:
        self._free_result(ctypes.c_void_p(result_ptr))  # MUST free DLL heap memory
```

**Frame encoding** (called when sending a command to the device):
```python
# parser_loader.py:174-191
def format_response(self, json_response: dict) -> bytes:
    payload = json.dumps(json_response).encode("utf-8")
    out_frame = (ctypes.c_uint8 * _MAX_FRAME_BUFFER_BYTES)()
    written = self._format_response(payload, out_frame)
    if written < 0:
        raise RuntimeError(f"format_response failed (rc={written})")
    return bytes(out_frame[:written])
```

---

## Data Flow — Device Telemetry (RX)

```
Device (TCP client)
  │  binary SDFC bytes (stream)
  ▼
transport/__init__.py: probation_connection()
  │  buffers raw bytes, calls detector on each read
  ▼
ParserHandle.extract_frame()  ← ctypes → C++ DLL
  │  returns (0, frame_bytes) when a complete frame is found
  ▼
runtime.py: _publish_telemetry()
  │  calls frame_type_from_bytes() to get 1=CMD / 2=RESP
  ▼
ParserHandle.parse_message()  ← ctypes → C++ DLL
  │  returns Python dict  e.g.:
  │  {"frame_type": "response", "group_id": 101, "unit_id": 40,
  │   "hop_min_hz": 220100000, "hop_max_hz": 234000000, ...}
  ▼
kafka/telemetry_publisher.py: publish()
  │  JSON → Kafka topic  hw.dp_ecm_hf.telemetry
  ▼
drs-server consumes and processes
```

## Data Flow — Commands (TX)

```
drs-server
  │  JSON → Kafka topic  drs.commands
  │  {"instance_id": "dp_ecm_hf#1", "group_id": 200, "unit_id": 9, ...}
  ▼
kafka/command_consumer.py: run()
  ▼
runtime.py: _on_command()
  ▼
ParserHandle.format_response()  ← ctypes → C++ DLL
  │  returns binary SDFC RESP frame bytes
  ▼
transport/__init__.py: UdpSender.send()
  │  UDP packet → device
  ▼
Device receives command
```

---

## Roster — Hot-Reload of Device Instances

The bridge does not have a static device list. Instead, drs-server publishes a **roster** to the Kafka topic `drs.roster`. On every new roster snapshot the bridge:

1. Reads the list of `RosterEntry` objects (instance_id, variant, host, TCP port, UDP port, enabled flag)
2. Diffs against what is currently bound
3. **Binds** new/changed entries: starts a TCP asyncio server + a UDP socket
4. **Tears down** removed/disabled entries: closes server + socket

This means you can add or remove a device instance at runtime without restarting the bridge.

---

## Key Source Files

| File | Purpose |
|------|---------|
| `src/drs_bridge/runtime.py` | Top-level orchestrator — roster diff, bind/teardown, telemetry pipeline |
| `src/drs_bridge/parsers/parser_loader.py` | **ctypes DLL wrapper** — the Python/C++ boundary |
| `src/drs_bridge/parsers/profile_loader.py` | Loads `*.yaml` variant profiles at startup |
| `src/drs_bridge/transport/__init__.py` | asyncio TCP server (framing probation) + UDP sender |
| `src/drs_bridge/profiles/_schema.py` | Pydantic models: VariantProfile, RosterEntry, Roster |
| `src/drs_bridge/profiles/dp_ecm_hf.yaml` | HF variant config (DLL path, time-signal config, ICD command names) |
| `src/drs_bridge/profiles/dp_ecm_vu.yaml` | VU variant config |
| `src/drs_bridge/kafka/command_consumer.py` | Reads `drs.commands` topic, dispatches to runtime |
| `src/drs_bridge/kafka/roster_consumer.py` | Reads `drs.roster` topic, triggers apply_roster() |
| `src/drs_bridge/kafka/telemetry_publisher.py` | Publishes decoded frames to `hw.<variant>.telemetry` |
| `parsers/dp_ecm/src/dp_ecm_hf_parser.cpp` | C++ HF DLL — 105 CMD + RESP decoders |
| `parsers/dp_ecm/src/dp_ecm_vu_parser.cpp` | C++ VU DLL — shared MRx decoders + VU-specific |
| `parsers/reference/src/reference_parser.cpp` | Minimal reference DLL used in unit tests |
| `tools/dp_ecm_full_coverage.py` | Test script — sends all 400+ SDFC frames through the bridge |
| `tools/dp_ecm_simulator.py` | Standalone device simulator for local testing |

---

## Variant Profiles (YAML)

Each hardware type has a YAML file in `src/drs_bridge/profiles/`:

```yaml
variant: dp_ecm_hf
parser_lib: parsers/dp_ecm/build/libdp_ecm_hf.dll
time_signal:
  embedded_in_messages: true
  precision_required_ms: 1.0
  periodic_distribution:
    enabled: false
    interval_ms: null

commands:         # human-readable ICD map (ignored by pydantic, used for logging)
  101/70: "FF Detection Report [RESP/DATA]"
  200/9:  "PA Activate [CMD]"
  ...
```

`parser_lib` points to the C++ DLL for this variant. The `commands:` section is not part of the pydantic schema — it is read separately by `profile_loader.load_commands()` and used only to annotate log output.

---

## ICD Group / Unit Number Mapping

The ICD uses "ECM Group N" but the C++ code adds 100:

| ICD name | Code group |
|----------|-----------|
| ECM Group 0 (System Health) | 100 |
| ECM Group 1 (Detection) | 101 |
| ECM Group 6 (Immediate Jam) | 106 |
| ECM Group 8 (Responsive FF) | 108 |
| ECM Group 9 (Date/Time) | 109 |
| ECM Group 11 (Signal Processing) | 111 |
| ECM Group 12 (PA/ASU/SDU) | 112 |
| HPASU (not in ICD V06) | 200 |
| MRx Groups 1–7 | 1–7 (unchanged) |

MRx groups (1–7) are **shared** between the HF and VU DLLs — they handle the monitoring receiver subsystem common to both devices.

---

## C++ DLL — Decoder Count (HF)

| Group | Description | CMD decoders | RESP decoders |
|-------|-------------|-------------|--------------|
| 100 | ECM System Health | 1 | 8 |
| 101 | ECM Detection & Control | 27 | ~20 |
| 109 | Date/Time | 1 | 1 |
| 111 | Signal Processing | 8 | 8 |
| 112 | ASU/SDU Config | 4 | 4 |
| 200 | HPASU Jam Control | 11 | 5 |
| 1 | MRx Diagnostics | 9 | 9 |
| 3 | MRx Channel/Board | 5 | 5 |
| 4 | MRx Data Acquisition | 19 | 19 |
| 5 | MRx RF Tuner | 4 | 4 |
| 6 | MRx GO2Monitor | 5 | 5 |
| 7 | MRx Signal BITE | 11 | 11 |
| **Total** | | **105** | **~99** |

---

## Traffic Direction Labels

| Label | Meaning |
|-------|---------|
| `RX CMD` | Bridge received a CMD frame from the device (device asking for a reply) |
| `RX RESP` | Bridge received a RESP frame from the device (device pushing telemetry/ack) |
| `TX` | Bridge sent a RESP frame to the device via UDP (replying to a command) |
