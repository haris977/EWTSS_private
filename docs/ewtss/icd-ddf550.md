# ICD Reference — R&S DDF-550 Wideband Direction Finder

**Document source:** R&S-DDF-550-ICD-V13  
**Role in EWTSS v2:** Wideband DF sensor. The drs-bridge `ddf550_parser.dll` handles
the XML control path (binary-wrapped XML) and the EB200 binary mass-data path.

---

## 1. Communication Channels

| Port (SCIF) | Port (direct) | Protocol | Direction | Purpose |
|---|---|---|---|---|
| TCP 9150 | 5563 | Binary-wrapped XML | Bidirectional | Control / configuration |
| TCP 9152 | 5565 | EB200 binary | Device → You | Mass data (traces) |
| TCP 9153 | — | Raw XML | Bidirectional | Preclassifier control |
| TCP 9154 | — | Raw XML | Device → You | Preclassifier output (FORMAT02) |

The DDF-550 acts as a **TCP server**. You connect to it as a client.

---

## 2. XML Control Channel (port 9150)

### 2.1 Wire Format — Binary Packet Wrapper

XML commands are **NOT sent as raw text**. Every XML message is wrapped in a
4-byte length-prefix packet. All integers are **big-endian (network byte order)**.

```
Bytes 0–3:      MagicWordStart   INT32 BE   (vendor-defined constant)
Bytes 4–7:      Length (n)       INT32 BE   number of XML bytes that follow
Bytes 8..8+n-1: XML text         INT8 × n   UTF-8 encoded XML
Bytes 8+n..+3:  MagicWordEnd     INT32 BE   (vendor-defined constant)
```

**Framing:** buffer until `offset + 8 + n + 4` bytes are received. Use the Length
field to know exactly how many bytes to wait for — do not scan for a closing XML tag.

### 2.2 XML Message Structure

```xml
<!-- You → DDF-550 -->
<Request type="set|get" id="<uint>">
  <Command name="<CommandName>">
    <Param name="<ParamName>">value</Param>
    ...
  </Command>
</Request>

<!-- DDF-550 → You -->
<Reply type="set|get" id="<uint>">
  <Command name="<CommandName>">
    <Param name="<ParamName>">value</Param>
    ...
  </Command>
</Reply>
```

### 2.3 Common Commands

| Command | Type | Purpose |
|---|---|---|
| `DfMode` | set/get | Operating mode (DFMODE_FFM, DFMODE_SCAN, etc.) |
| `MeasureSettingsFFM` | set/get | Measurement parameters for FFM mode |
| `ScanRangeAdd` | set | Add a scan range (freq start/end, span, etc.) |
| `ScanRangeDeleteAll` | set | Clear all scan ranges |
| `TraceEnable` | set | Start sending a trace type on the mass data port |
| `TraceDisable` | set | Stop a trace type |
| `TraceDelete` | set | Delete entire trace connection |
| `DeviceInfo` | get | Hardware/firmware version info |

### 2.4 Example — Enable Audio Trace

```xml
<Request type="set" id="123">
  <Command name="TraceEnable">
    <Param name="eTraceTag">TRACETAG_AUDIO</Param>
    <Param name="zIP">192.168.1.100</Param>
    <Param name="iPort">9152</Param>
  </Command>
</Request>
```

---

## 3. EB200 Binary Mass Data Channel (port 9152)

### 3.1 Overview

The EB200 protocol is Rohde & Schwarz's proprietary binary streaming format.
Each packet = one 16-byte EB200 header + one Generic Attribute (Trace Attribute + Trace Data).
All fields are **big-endian**.

### 3.2 EB200 Header — 16 bytes

> Note: The byte-level table (Table 2) in the ICD was published as an image.
> The field layout below is from the R&S EB200 protocol standard.

| Offset | Field | Type | Description |
|---|---|---|---|
| 0–1 | Tag | uint16 BE | Trace type identifier (see §3.3) |
| 2–3 | AttributeLength | uint16 BE | Total EB200 packet size in **32-bit words** (includes this 4-word header) |
| 4–5 | Selector | uint16 BE | `0x4242` — EB200 magic marker |
| 6–7 | Number | uint16 BE | Stream / channel number (0-indexed) |
| 8–11 | Timestamp | uint32 BE | 100 ns resolution; rolls over every ~429 seconds |
| 12–13 | SequenceNumber\_Low | uint16 BE | Packet sequence counter (low word) |
| 14–15 | SequenceNumber\_High | uint16 BE | High word (v60+); 0 in conventional mode |

**Total packet size** = `AttributeLength × 4` bytes.

**Magic check:** verify `Selector == 0x4242` before parsing.

### 3.3 Trace Tags (selected)

| Tag | Name | Description |
|---|---|---|
| 1 | AUDIO | Audio samples (30 ms packets) |
| 2 | IFPAN | IF panorama (spectrum) |
| 3 | DF | Direction-finding result |
| 5 | VIDEO | Video panorama |
| 7 | PSCAN | Panorama scan |
| 8 | SIGP | Signal processor output |
| 10 | GPS\_COMPASS | GPS + compass data |
| 11 | ANT\_LEVEL | Antenna level |
| 20 | HRPAN | High-resolution panorama |
| 5000+ | Advanced | Advanced attribute type (extended metadata) |

### 3.4 Conventional vs Advanced Attribute

- Tag < 5000: **Conventional** Generic Attribute (shorter metadata header)
- Tag ≥ 5000: **Advanced** Generic Attribute (more fields, extended metadata)

Each Generic Attribute contains:
1. **Trace Attribute** — metadata (timestamp, trace length, data format, etc.)
2. **Trace Data** — actual measurement samples

### 3.5 TCP Stream Reassembly

```
while buffer holds >= 16 bytes:
    read Tag + AttributeLength from bytes 0..3
    verify Selector (bytes 4..5) == 0x4242
    total_bytes = AttributeLength × 4
    if buffer.size < total_bytes: wait for more
    consume total_bytes from front of buffer
    dispatch on Tag
```

---

## 4. Preclassifier Output Channel (port 9154)

**Wire format:** raw XML, no binary wrapper. FORMAT02 is used.

Classification data is sent every ~8 seconds with all active emitters.
The `<DFData>` root element carries DDF-CL-IDs as attributes.

### Emitter Classes

| Class | Code | Description |
|---|---|---|
| Static | F | Fixed-frequency, long-duration signal |
| Burst | B | Short single-pulse emission |
| Hopper | H | Frequency-hopping emitter |
| Chirp | C | Frequency-sweeping emitter |

### Preclassifier Filter Command (sent on port 9153)

```xml
<DFSelect>
  <EmitterClass>Hopper</EmitterClass>
  <EmitterClass>Burst</EmitterClass>
</DFSelect>
```

---

## 5. Suggested JSON Output Schema

### EB200 Audio Trace
```json
{
  "hw": "ddf550",
  "stream": "audio",
  "tag": 1,
  "channel": 0,
  "timestamp_100ns": 4294967295,
  "seq": 1024,
  "payload_bytes": 480
}
```

### EB200 DF Result Trace
```json
{
  "hw": "ddf550",
  "stream": "df",
  "tag": 3,
  "channel": 0,
  "timestamp_100ns": 4294967295,
  "seq": 1025,
  "payload_bytes": 64
}
```

### Preclassifier Hopper (FORMAT02)
```json
{
  "hw": "ddf550",
  "stream": "preclassifier",
  "emitter_class": "Hopper",
  "freq_start_hz": 30000000,
  "freq_center_hz": 35000000,
  "freq_stop_hz": 40000000,
  "azimuth_deg": 127.5
}
```

---

## 6. Step-by-Step: Building `ddf550_parser.dll`

### Step 1 — Project skeleton

```
ddf550_parser/
  CMakeLists.txt
  include/
    ddf550_parser.h     ← 4-symbol C ABI
  src/
    ddf550_parser.cpp
    eb200_frame.h       ← EB200 header + dispatch
    eb200_frame.cpp
    xml_wrapped.h       ← binary-wrapper XML read/write
    xml_wrapped.cpp
    preclass_xml.h      ← FORMAT02 parser
    preclass_xml.cpp
```

### Step 2 — Declare the 4-symbol C ABI

```c
// ddf550_parser.h
#ifdef __cplusplus
extern "C" {
#endif

int   extract_frame   (const uint8_t* buf, int len);
void* parse_message   (const uint8_t* buf, int len);
int   format_response (void* msg, char* out_json, int cap);
void  free_result     (void* msg);

#ifdef __cplusplus
}
#endif
```

### Step 3 — Two separate extract_frame paths

The drs-bridge will call one DLL per TCP connection. Each connection knows its port, so
it always calls the same DLL with the same protocol. But for robustness, detect inside
the DLL which path is active:

```cpp
// EB200 path: check Selector bytes 4–5 == 0x4242
// XML-wrapped path: MagicWordStart in bytes 0–3 (big-endian)
int extract_frame(const uint8_t* buf, int len) {
    if (len < 8) return 0;

    // EB200: big-endian Selector at offset 4
    uint16_t sel = (uint16_t)((buf[4] << 8) | buf[5]);
    if (sel == 0x4242) {
        uint16_t attr_len = (uint16_t)((buf[2] << 8) | buf[3]);
        int total = attr_len * 4;
        return (len >= total) ? total : 0;
    }

    // XML-wrapped: read big-endian Length at offset 4
    uint32_t n = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16)
               | ((uint32_t)buf[6] <<  8) |  (uint32_t)buf[7];
    int total = 4 + 4 + (int)n + 4;   // MagicStart + Length + XML + MagicEnd
    return (len >= total) ? total : 0;
}
```

### Step 4 — Implement EB200 parse_message

```cpp
struct Eb200Parsed {
    uint16_t tag;
    uint16_t attr_len;
    uint16_t channel;
    uint32_t timestamp_100ns;
    uint32_t seq;
    const uint8_t* payload;
    int payload_bytes;
    bool is_xml;           // false for EB200, true for XML-wrapped
};

void* parse_message(const uint8_t* buf, int len) {
    if (len < 16) return nullptr;
    auto* out = new Eb200Parsed{};
    uint16_t sel = (uint16_t)((buf[4] << 8) | buf[5]);
    if (sel == 0x4242) {
        out->tag         = (uint16_t)((buf[0] << 8) | buf[1]);
        out->attr_len    = (uint16_t)((buf[2] << 8) | buf[3]);
        out->channel     = (uint16_t)((buf[6] << 8) | buf[7]);
        out->timestamp_100ns = ((uint32_t)buf[8]  << 24) | ((uint32_t)buf[9]  << 16)
                             | ((uint32_t)buf[10] <<  8) |  (uint32_t)buf[11];
        out->seq         = ((uint32_t)buf[12] << 8) | buf[13];
        out->payload     = buf + 16;
        out->payload_bytes = out->attr_len * 4 - 16;
        out->is_xml      = false;
    } else {
        // XML-wrapped: skip 8-byte header, strip 4-byte footer
        uint32_t n = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16)
                   | ((uint32_t)buf[6] << 8)  |  (uint32_t)buf[7];
        out->payload       = buf + 8;
        out->payload_bytes = (int)n;
        out->is_xml        = true;
    }
    return out;
}
```

### Step 5 — Implement format_response

```cpp
int format_response(void* msg, char* out, int cap) {
    auto* f = static_cast<Eb200Parsed*>(msg);
    if (f->is_xml) {
        // forward inner XML parse to xml helper
        return snprintf(out, cap,
            "{\"hw\":\"ddf550\",\"stream\":\"xml_ctrl\","
            "\"xml_len\":%d}", f->payload_bytes);
    }
    const char* stream = (f->tag == 1) ? "audio"
                       : (f->tag == 2) ? "ifpan"
                       : (f->tag == 3) ? "df"
                       : (f->tag == 7) ? "pscan"
                       : "unknown";
    return snprintf(out, cap,
        "{\"hw\":\"ddf550\",\"stream\":\"%s\","
        "\"tag\":%u,\"channel\":%u,"
        "\"timestamp_100ns\":%u,\"seq\":%u,"
        "\"payload_bytes\":%d}",
        stream, f->tag, f->channel,
        f->timestamp_100ns, f->seq, f->payload_bytes);
}
```

### Step 6 — free_result

```cpp
void free_result(void* msg) {
    delete static_cast<Eb200Parsed*>(msg);
}
```

### Step 7 — CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(ddf550_parser)
set(CMAKE_CXX_STANDARD 17)

add_library(ddf550_parser SHARED
    src/ddf550_parser.cpp
    src/eb200_frame.cpp
    src/xml_wrapped.cpp
    src/preclass_xml.cpp
)
target_include_directories(ddf550_parser PRIVATE include)
if(MSVC)
    target_compile_options(ddf550_parser PRIVATE /W4 /wd4100)
endif()
```

### Step 8 — Testing checklist

- [ ] Build a synthetic 16-byte EB200 header with `Selector=0x4242`, `AttributeLength=4` → `extract_frame` returns 16
- [ ] Build a 20-byte buffer (only 15 data bytes) → `extract_frame` returns 0 (wait)
- [ ] Build an XML-wrapped packet with known inner XML → `parse_message` sets `is_xml=true`, `payload_bytes` correct
- [ ] Verify big-endian field extraction: tag, timestamp, seq
- [ ] Feed a preclassifier XML packet from port 9154 and confirm FORMAT02 `<DFData>` parsing
- [ ] Sequence-number gap detection test (inject gap → log warning)

---

## 7. Key Notes

- All EB200 and XML-wrapper integers are **big-endian** (network byte order).
- The XML control wrapper `[MagicWordStart][Length][XML][MagicWordEnd]` is mandatory on port 9150 — raw XML is rejected.
- Port 9154 (preclassifier output) is raw XML — no wrapper.
- EB200 Timestamp wraps every ~429 seconds; correlate across packets using SequenceNumber for continuity.
- Use `TraceEnable` XML command on port 9150 to subscribe to specific trace types on port 9152.
