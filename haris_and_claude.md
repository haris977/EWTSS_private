# EWTSS v2 — DRS Bridge: Full Data & Memory Flow
> C++ Parser DLL ↔ Python drs-bridge ↔ Kafka

---

## 1. The Big Picture — One Frame's Journey

```
                         ┌─────────────────────────────────────────────────────┐
                         │                  WS2 — drs-bridge                   │
                         │                                                      │
  DRS Hardware           │   TCP Layer       Python Core      C++ DLL Layer     │
  ────────────           │   ─────────       ───────────      ─────────────     │
                         │                                                      │
  [Device A] ──port 5001─┼──► asyncio  ───► route_to_dll() ──► drs_alpha.dll   │
  [Device B] ──port 5002─┼──► asyncio  ───► route_to_dll() ──► drs_bravo.dll   │
  [Device C] ──port 5003─┼──► asyncio  ───► route_to_dll() ──► drs_charlie.dll │
       ...               │      ...              ...               ...          │
  [Device N] ──port 50XX─┼──► asyncio  ───► route_to_dll() ──► drs_xxxxx.dll   │
                         │                                                      │
                         │              ▼ parsed + formatted ▼                  │
                         │                                                      │
                         │         ┌──────────────────────┐                    │
                         │         │   aiokafka producer  │────► Kafka Topic   │
                         │         └──────────────────────┘                    │
                         └─────────────────────────────────────────────────────┘
```

---

## 2. YAML — The Only File You Touch to Add Hardware

Python reads this at startup. No Python code changes ever needed for new hardware.

```yaml
# config/hardware_variants.yml

drs_bridge:
  kafka:
    broker: "localhost:9092"
    topic:  "drs.frames"

  hardware_variants:

    - port: 5001
      name: "DRS-Alpha"
      dll:  "parsers/drs_alpha_parser.dll"

    - port: 5002
      name: "DRS-Bravo"
      dll:  "parsers/drs_bravo_parser.dll"

    - port: 5003
      name: "DRS-Charlie"
      dll:  "parsers/drs_charlie_parser.dll"

    # ── Adding new hardware ──────────────────────────────
    # Step 1: Write drs_delta_parser.dll in C++ (same ABI)
    # Step 2: Add this block below. Python picks it up on restart.
    # Step 3: Done. No Python code touched.
    #
    # - port: 5004
    #   name: "DRS-Delta"
    #   dll:  "parsers/drs_delta_parser.dll"
```

### YAML → Python startup animation

```
Python starts
     │
     ▼
  Load hardware_variants.yml
     │
     ├──► port 5001 → ctypes.CDLL("drs_alpha_parser.dll")   ✓ loaded
     ├──► port 5002 → ctypes.CDLL("drs_bravo_parser.dll")   ✓ loaded
     └──► port 5003 → ctypes.CDLL("drs_charlie_parser.dll") ✓ loaded
     │
     ▼
  DLL_REGISTRY = {
      5001: <alpha_dll>,
      5002: <bravo_dll>,
      5003: <charlie_dll>
  }
     │
     ▼
  asyncio TCP server starts — listening on all registered ports
```

---

## 3. C++ DLL — The 6-Symbol ABI

Every hardware DLL exports exactly these 6 functions. Same names, same signatures, different internals.

```c
// ─── Lifecycle ──────────────────────────────────────────────────────────────

// Call once when a DRS device connects (one context per TCP connection)
void* drs_create_context(void);

// Call when the TCP connection closes (releases per-connection state)
void  drs_destroy_context(void* ctx);


// ─── Hot Path ────────────────────────────────────────────────────────────────

// Feed raw TCP bytes → reassembles stream → extracts one complete frame
//
// Parameters:
//   ctx            — per-connection state handle (from drs_create_context)
//   stream_buf     — raw bytes from TCP recv()
//   stream_len     — number of bytes in stream_buf
//   out_frame      — [OUT] C++ allocates frame here if complete; NULL if not yet
//   out_frame_size — [OUT] size of the extracted frame in bytes
//
// Returns:
//   > 0   bytes consumed from stream_buf (advance your buffer by this amount)
//     0   need more TCP data — do not advance buffer
//    -1   unrecoverable parse error — close the connection
int32_t extract_frame(
    void*           ctx,
    const uint8_t*  stream_buf,
    int32_t         stream_len,
    uint8_t**       out_frame,
    int32_t*        out_frame_size
);

// Parse a complete frame → structured result object
//
// Parameters:
//   frame_buf  — frame from extract_frame (or NULL-check first)
//   frame_size — byte length of frame_buf
//
// Returns: opaque handle to parsed result (C++ owned)
//          NULL on error — call get_last_error()
void* parse_message(
    const uint8_t*  frame_buf,
    int32_t         frame_size
);

// Serialize parsed result → Kafka-ready bytes (JSON or msgpack)
//
// Parameters:
//   parsed_handle — result from parse_message
//   out_size      — [OUT] byte length of serialized output
//
// Returns: pointer to serialized bytes (C++ owned)
//          NULL on error — call get_last_error()
const uint8_t* format_response(
    void*     parsed_handle,
    int32_t*  out_size
);

// Free any C++ heap allocation (frame, parsed handle, or formatted bytes)
//
// Parameters:
//   ptr — any pointer returned by extract_frame, parse_message, format_response
void free_result(void* ptr);


// ─── Error reporting (no exceptions cross DLL boundary) ──────────────────────

// Returns last error string (static storage — read before next call)
const char* get_last_error(void);
```

---

## 4. Variable Packet Sizes — The Core Problem

Even on the same port (same hardware variant), every frame can be a different size.
A telemetry burst message might be 48 bytes. A waveform capture might be 64 KB.
TCP does not care — it delivers a raw byte stream with no frame markers.

### What the DRS frame looks like on the wire

```
   ┌──────────────────────────────────────────────────────────────────┐
   │                    RAW TCP STREAM (port 5001)                    │
   │                                                                  │
   │  [MAGIC][LEN ][TYPE][...payload... variable length ...][CRC ]    │
   │   2 bytes 4 B  1 B        LEN bytes                    2 bytes   │
   │                                                                  │
   │  Frame 1: MAGIC=0xABCD, LEN=48,    payload=48 bytes             │
   │  Frame 2: MAGIC=0xABCD, LEN=1024,  payload=1024 bytes           │
   │  Frame 3: MAGIC=0xABCD, LEN=65500, payload=65500 bytes          │
   └──────────────────────────────────────────────────────────────────┘

Total frame size = 2 + 4 + 1 + LEN + 2 = LEN + 9 bytes
C++ reads LEN from the header → malloc(LEN + 9) → exact size every time
```

### Three scenarios TCP can deliver to Python on one recv()

```
─────────────────────────────────────────────────────────────────────────
SCENARIO A — partial frame (very common at 1 Gbps, large frames)
─────────────────────────────────────────────────────────────────────────

TCP recv() gives:  [ AB CD 00 00 FF 00 01 ]    ← only 7 bytes arrived
                     ─────────────────────
                     MAGIC + 4-byte LEN field — but LEN says 1024 bytes needed
                     header says: total frame = 1033 bytes
                     we have: 7 bytes

extract_frame():
  ctx stores:  partial_header = [AB CD 00 00 FF 00 01]
               bytes_needed   = 1033 - 7 = 1026 more bytes
  returns:     consumed = 7,  out_frame = NULL  ← no frame yet

Python:  self.buf = self.buf[7:]   (empty now)
         break — wait for more TCP data


─────────────────────────────────────────────────────────────────────────
SCENARIO B — exactly one frame
─────────────────────────────────────────────────────────────────────────

TCP recv() gives:  [ AB CD 00 00 00 30 01 ...48 payload bytes... XX YY ]
                     ──────────────────────────────────────────────────
                     MAGIC + LEN=48 + TYPE + payload(48) + CRC = 57 bytes

extract_frame():
  reads header → LEN = 48 → expected total = 57 bytes
  have = 57 bytes  ✓  complete frame
  malloc(57)  →  copy frame into new buffer
  returns:  consumed = 57,  out_frame = <ptr to 57-byte buffer>

Python:  self.buf = self.buf[57:]   (empty now)
         process the frame → parse → format → kafka


─────────────────────────────────────────────────────────────────────────
SCENARIO C — multiple frames in one recv() (batching at 1 Gbps)
─────────────────────────────────────────────────────────────────────────

TCP recv() gives:  [ Frame1(57B) | Frame2(1033B) | Frame3(partial 20B) ]
                     ──────────────────────────────────────────────────
                     Total bytes received = 1110

extract_frame() call 1:
  reads Frame1 header → LEN=48 → complete (57 bytes)
  malloc(57) → extracts Frame1
  returns:  consumed = 57,  out_frame = <Frame1 ptr>

  Python: processes Frame1, advances buf by 57
          buf now = [ Frame2(1033B) | Frame3(partial 20B) ]

extract_frame() call 2:
  reads Frame2 header → LEN=1024 → complete (1033 bytes)
  malloc(1033) → extracts Frame2
  returns:  consumed = 1033,  out_frame = <Frame2 ptr>

  Python: processes Frame2, advances buf by 1033
          buf now = [ Frame3(partial 20B) ]

extract_frame() call 3:
  reads partial Frame3 header → LEN unknown yet (need 9 bytes, have 7)
  ctx stores partial state
  returns:  consumed = 0,  out_frame = NULL

  Python: break — wait for more TCP data
```

### The context object — why it exists

```
Without ctx (WRONG):
  Call 1:  recv 7 bytes of a 1033-byte frame → discard partial state
  Call 2:  recv next 1026 bytes → C++ has no memory of the first 7
           → cannot reconstruct the frame → data loss

With ctx (CORRECT):
  Call 1:  recv 7 bytes → ctx.partial_buf = [7 bytes], ctx.bytes_needed = 1026
  Call 2:  recv 1026 bytes → ctx sees partial_buf + new bytes = complete frame
           → malloc(1033) → copy both pieces into contiguous buffer → done

ctx holds:
  ┌────────────────────────────────────────────────────────┐
  │  struct ConnectionContext {                            │
  │      uint8_t  partial_buf[MAX_HEADER];  // header accum│
  │      int32_t  partial_len;              // bytes so far│
  │      int32_t  expected_total;           // from LEN field│
  │      uint8_t* frame_accum;             // body accumulator│
  │      int32_t  frame_accum_len;         // body bytes so far│
  │  };                                                    │
  └────────────────────────────────────────────────────────┘
  One of these per TCP connection — 100 connections = 100 contexts
```

### How malloc size is always exact — never fixed

```
Frame arrives         C++ reads LEN field      malloc uses actual size
─────────────         ────────────────────     ──────────────────────

Frame A: LEN=48   →   expected = 48+9 = 57  →  malloc(57)   ← 57 bytes
Frame B: LEN=1024 →   expected = 1024+9=1033→  malloc(1033) ← 1033 bytes
Frame C: LEN=65500→   expected = 65500+9    →  malloc(65509) ← 65509 bytes

out_frame_size OUT parameter carries the exact malloc'd size back to Python.
Python never guesses — it only uses what C++ reports.
```

### Python always uses out_frame_size — never hardcodes size

```python
# C++ sets out_frame_size to the exact allocated size
consumed = self.dll.extract_frame(
    self.ctx, raw_buf, len(self.buf),
    ctypes.byref(out_frame),
    ctypes.byref(out_frame_size)   # ← exact size, could be 57 or 65509
)

# parse_message receives exact size — C++ reads exactly these bytes
parsed_handle = self.dll.parse_message(frame_ptr, out_frame_size.value)

# format_response reports exact JSON output size via out parameter
json_size = ctypes.c_int32(0)
json_ptr  = self.dll.format_response(parsed_handle, ctypes.byref(json_size))

# from_address uses json_size.value — correct size for THIS message
view = (ctypes.c_uint8 * json_size.value).from_address(
    ctypes.cast(json_ptr, ctypes.c_void_p).value
)
# json_size.value could be 120 bytes for one message, 8192 for another
# Python handles both identically — size flows from C++ to ctypes automatically
```

---

## 6. Memory Lifecycle — Animated (Variable Sizes)

### Step 1 — TCP data arrives, Python buffers it

```
TCP recv() ──► [ raw bytes: AA BB CC DD EE FF 00 11 ... ]
                              │
                              ▼
              self.buf (bytearray) ← Python owns this buffer
              [ AA BB CC DD EE FF 00 11 ... ]
```

### Step 2 — extract_frame called, C++ reads LEN field → exact malloc

```
Python calls:
  extract_frame(ctx, stream_buf, stream_len, &out_frame, &out_frame_size)
                                                   │
                          ┌────────────────────────┘
                          ▼
              C++ reads MAGIC (2B) + LEN field (4B) from header
              LEN = 1019  →  total frame = 1019 + 9 = 1028 bytes
              C++ calls malloc(1028)   ← exact size for THIS frame

RAM:  [ ████████... 1028 bytes ...████ ]  ← C++ just allocated exact size
        out_frame ──────────────────────►
        out_frame_size = 1028            ← tells Python the exact size

Returns consumed = 1028  (Python advances stream buffer by 1028 bytes)

Python's stream buf: [ 00 11 22 ... ]  (remaining bytes, next frame starts)
```

### Step 3 — parse_message called, C++ allocates parsed result

```
Python calls:
  parse_message(out_frame, out_frame_size)
       │
       ▼  C++ reads frame bytes, extracts fields
          C++ calls malloc(sizeof(ParsedResult))

RAM:  [ ████ frame ████ ]  [ ████ parsed ████ ]
        ^                     ^
        out_frame              parsed_handle  ← returned to Python
```

### Step 4 — format_response called, C++ allocates JSON bytes

```
Python calls:
  format_response(parsed_handle, &out_size)
       │
       ▼  C++ serializes ParsedResult to JSON
          C++ calls malloc(json_size)

RAM:  [ ████ frame ████ ]  [ ████ parsed ████ ]  [ ████ json ████ ]
        ^                     ^                     ^
        out_frame              parsed_handle          json_ptr ← returned to Python
```

### Step 5 — Python wraps pointer (ZERO COPY)

```
Python calls:
  view = (c_uint8 * json_len).from_address(json_ptr)

RAM:  [ ████ frame ████ ]  [ ████ parsed ████ ]  [ ████ json ████ ]
                                                     ^
                                                     │
                           Python ctypes view ───────┘
                           (just a pointer — no data moved)
```

### Step 6 — aiokafka copies json bytes (ONE unavoidable copy)

```
Python calls:
  await producer.send(topic, value=memoryview(view))

aiokafka reads from json_ptr and copies into its internal send batch:

RAM:  [ ████ frame ████ ]  [ ████ parsed ████ ]  [ ████ json ████ ]
                                                     │
                                             memcpy  │
                                                     ▼
Kafka batch buf:  [ ████ json copy ████ ]  ← aiokafka owns this now

producer.send() returns → aiokafka HAS the data → C++ buffer safe to free
```

### Step 7 — Python calls free_result (C++ frees all 3 allocations)

```
Python calls (in finally block):
  free_result(json_ptr)      → RAM: [ ░░░░ freed ░░░░ ]  [ ████ parsed ████ ]  [ ████ frame ████ ]
  free_result(parsed_handle) → RAM: [ ░░░░ freed ░░░░ ]  [ ░░░░ freed ░░░░  ]  [ ████ frame ████ ]
  free_result(out_frame)     → RAM: [ ░░░░ freed ░░░░ ]  [ ░░░░ freed ░░░░  ]  [ ░░░░ freed ░░░░ ]

All three allocations gone. No leaks.
```

---

## 7. The USE-AFTER-FREE Danger — What NOT to do

```
                         ┌─────────────────────────────────────────┐
                         │  WRONG — free before Kafka send         │
                         └─────────────────────────────────────────┘

json_ptr ──► [ ████ json ████ ]   ← valid data here

free_result(json_ptr)             ← Python frees TOO EARLY

json_ptr ──► [ ░░░░ freed ░░░░ ]  ← memory released

  ... time passes, new message arrives ...

json_ptr ──► [ ████ NEW MSG █ ]   ← C++ reused this address!

aiokafka reads json_ptr           ← reads WRONG MESSAGE silently
                                     no crash, no error
                                     corrupted telemetry sent to Kafka
```

```
                         ┌─────────────────────────────────────────┐
                         │  CORRECT — free inside finally          │
                         └─────────────────────────────────────────┘

json_ptr ──► [ ████ json ████ ]

await producer.send(topic, value=memoryview(view))
  │
  └── aiokafka copies bytes synchronously at THIS line
      by the time send() returns, aiokafka has its own copy

free_result(json_ptr)   ← safe — aiokafka no longer needs json_ptr

[ ░░░░ freed ░░░░ ]     ← clean
```

---

## 8. Python Code — Full Per-Connection Handler

```python
import ctypes
import yaml
import asyncio

# ── Startup: load YAML and build DLL registry ──────────────────────────────

def load_dll_registry(config_path: str) -> dict:
    with open(config_path) as f:
        cfg = yaml.safe_load(f)

    registry = {}
    for variant in cfg["drs_bridge"]["hardware_variants"]:
        dll = ctypes.CDLL(variant["dll"])

        # Declare all function signatures (ctypes requires this for correctness)
        dll.drs_create_context.restype  = ctypes.c_void_p
        dll.drs_create_context.argtypes = []

        dll.drs_destroy_context.restype  = None
        dll.drs_destroy_context.argtypes = [ctypes.c_void_p]

        dll.extract_frame.restype  = ctypes.c_int32
        dll.extract_frame.argtypes = [
            ctypes.c_void_p,                           # ctx
            ctypes.POINTER(ctypes.c_uint8),            # stream_buf
            ctypes.c_int32,                            # stream_len
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),  # out_frame
            ctypes.POINTER(ctypes.c_int32),            # out_frame_size
        ]

        dll.parse_message.restype  = ctypes.c_void_p
        dll.parse_message.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),            # frame_buf
            ctypes.c_int32,                            # frame_size
        ]

        dll.format_response.restype  = ctypes.POINTER(ctypes.c_uint8)
        dll.format_response.argtypes = [
            ctypes.c_void_p,                           # parsed_handle
            ctypes.POINTER(ctypes.c_int32),            # out_size
        ]

        dll.free_result.restype  = None
        dll.free_result.argtypes = [ctypes.c_void_p]

        dll.get_last_error.restype  = ctypes.c_char_p
        dll.get_last_error.argtypes = []

        registry[variant["port"]] = dll

    return registry


# ── Per-connection class ────────────────────────────────────────────────────

class DRSConnection:
    def __init__(self, port: int, dll, kafka_producer):
        self.port     = port
        self.dll      = dll
        self.producer = kafka_producer
        self.ctx      = dll.drs_create_context()   # per-connection C++ state
        self.buf      = bytearray()                # TCP stream reassembly buffer

    async def on_data(self, tcp_bytes: bytes):
        self.buf.extend(tcp_bytes)

        # Loop: one TCP recv can contain multiple complete frames
        while len(self.buf) > 0:
            out_frame      = ctypes.POINTER(ctypes.c_uint8)()
            out_frame_size = ctypes.c_int32(0)

            raw_buf = (ctypes.c_uint8 * len(self.buf)).from_buffer(self.buf)

            consumed = self.dll.extract_frame(
                self.ctx,
                raw_buf,
                len(self.buf),
                ctypes.byref(out_frame),
                ctypes.byref(out_frame_size),
            )

            if consumed == -1:
                err = self.dll.get_last_error()
                raise RuntimeError(f"DLL parse error on port {self.port}: {err}")

            if consumed == 0:
                break  # incomplete frame — wait for more TCP bytes

            self.buf = self.buf[consumed:]  # advance stream buffer

            if out_frame and out_frame_size.value > 0:
                await self._process_frame(out_frame, out_frame_size.value)

    async def _process_frame(self, frame_ptr, frame_size):
        parsed_handle = None
        json_ptr      = None

        try:
            # C++ parses frame → allocates parsed result
            parsed_handle = self.dll.parse_message(frame_ptr, frame_size)
            if not parsed_handle:
                raise RuntimeError(self.dll.get_last_error())

            # C++ serializes → allocates JSON bytes
            json_size = ctypes.c_int32(0)
            json_ptr  = self.dll.format_response(parsed_handle, ctypes.byref(json_size))
            if not json_ptr:
                raise RuntimeError(self.dll.get_last_error())

            # Python wraps pointer — ZERO COPY
            view = (ctypes.c_uint8 * json_size.value).from_address(
                ctypes.cast(json_ptr, ctypes.c_void_p).value
            )

            # aiokafka copies bytes into batch buffer synchronously — ONE COPY
            await self.producer.send("drs.frames", value=memoryview(view))

        finally:
            # Always free — even if parse or send threw an exception
            if json_ptr:
                self.dll.free_result(json_ptr)
            if parsed_handle:
                self.dll.free_result(parsed_handle)
            if frame_ptr:
                self.dll.free_result(frame_ptr)

    def close(self):
        self.dll.drs_destroy_context(self.ctx)
        self.ctx = None
```

---

## 9. Adding New Hardware — What Changes vs What Stays the Same

```
┌─────────────────────────────────────────────────────────────────────┐
│  New hardware variant arrives (e.g. DRS-Delta, port 5004)           │
└─────────────────────────────────────────────────────────────────────┘

Person C (C++ developer) does:                Person A (Python) does:
────────────────────────────────              ──────────────────────
  1. Write drs_delta_parser.cpp                 NOTHING
     implementing same 6-symbol ABI
  2. Build drs_delta_parser.dll
  3. Drop dll into parsers/ folder
  4. Add YAML entry:
       - port: 5004
         name: "DRS-Delta"
         dll:  "parsers/drs_delta_parser.dll"
  5. Restart drs-bridge

                 ▼
  Python auto-loads new DLL on startup
  DLL_REGISTRY[5004] = <delta_dll>
  asyncio begins accepting port 5004
  New device works immediately
```

### File change summary per new hardware variant

```
parsers/
  drs_alpha_parser.dll      ← existing, untouched
  drs_bravo_parser.dll      ← existing, untouched
  drs_charlie_parser.dll    ← existing, untouched
  drs_delta_parser.dll      ← NEW (C++ builds this)

config/
  hardware_variants.yml     ← add 4-line YAML block (C++ dev or architect)

drs_bridge/
  connection.py             ← untouched
  server.py                 ← untouched
  registry.py               ← untouched
  (all Python files)        ← untouched
```

---

## 10. Copy Count Summary

```
Stage                                    Copies    Who owns memory
────────────────────────────────────     ──────    ───────────────
TCP recv → Python bytearray                 1      Python (bytearray)
Python bytearray → extract_frame            0      Python passes view, C++ reads
extract_frame malloc (frame)                0      C++ allocates
parse_message malloc (parsed)               0      C++ allocates
format_response malloc (json)               0      C++ allocates
Python from_address(json_ptr)               0      C++ still owns, Python has pointer
aiokafka.send(memoryview)                   1      Kafka batch buffer (aiokafka)
free_result × 3                             0      Memory returned to OS

Total copies for one message:               2
  Copy 1 — TCP kernel → Python userspace    (unavoidable, OS boundary)
  Copy 2 — C++ json buffer → Kafka batch    (unavoidable, Kafka needs ownership)
```

---

## 11. Quick Reference Card

| Question | Answer |
|---|---|
| Who allocates frame buffer? | C++ inside `extract_frame` — size = LEN field + header overhead |
| Who allocates parsed result? | C++ inside `parse_message` |
| Who allocates json bytes? | C++ inside `format_response` |
| Who frees all three? | Python via `free_result(ptr)` |
| When does Python free? | Inside `finally` — after `producer.send()` returns |
| Does C++ ever free on its own? | Never — it waits for Python to call `free_result` |
| Are packet sizes fixed? | No — LEN field in header drives every malloc; 48 bytes to 65 KB same code |
| What if only part of a frame arrived? | `extract_frame` returns `consumed > 0, out_frame = NULL`; ctx holds partial state |
| What if one recv contains 3 frames? | Python loops calling `extract_frame` until it returns `consumed = 0` |
| How does Python know frame/json size? | `out_frame_size` and `json_size` OUT parameters — C++ always reports actual size |
| What if new hardware arrives? | Write DLL + add YAML line — Python untouched |
| What causes use-after-free? | Calling `free_result` before `producer.send()` returns |
| How many copies per message? | 2 (TCP kernel boundary + Kafka batch copy) |
