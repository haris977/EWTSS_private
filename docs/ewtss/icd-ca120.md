# ICD Reference — R&S CA120 Multichannel Signal Analysis Software

**Document source:** R&S-CA120-ICD-V15  
**Role in EWTSS v2:** Central signal-analysis server. Exposes an XML control interface and
multiple binary AMMOS data streams. The drs-bridge `ca120_parser.dll` connects to both paths.

---

## 1. Communication Channels

| Port | Protocol | Direction | Purpose |
|---|---|---|---|
| UDP 7999 | Broadcast | Device → LAN | Service discovery |
| TCP 9001 | Raw XML | Bidirectional | Control / configuration |
| TCP >9200 (dynamic) | AMMOS binary | Device → You | One port per active data stream |

Dynamic data ports are negotiated over the XML control channel:
the client sends a `DataStream action="start"` command specifying its own IP + port;
the CA120 then pushes AMMOS frames to that port.

---

## 2. XML Control Channel (port 9001)

**Wire format:** raw UTF-8 XML text — no length prefix, no binary wrapper.

**Message framing:** one complete XML document per message.
Buffer until the closing root tag (`</Reply>`, `</Request>`, or `</Event>`) is received.

### 2.1 Request/Reply Pattern

```xml
<!-- Client → CA120 -->
<Request type="set|get" id="<uint>">
  <RootNode>
    ...
  </RootNode>
</Request>

<!-- CA120 → Client -->
<Reply type="set|get" id="<uint>">
  <RootNode>
    ...
  </RootNode>
</Reply>
```

### 2.2 Common Root Nodes

| Root node | Purpose |
|---|---|
| `<Control>` | Data stream start/stop, system control |
| `<FFT>` | Spectrum analysis settings |
| `<tuner>` | Tuner frequency / bandwidth |
| `<DetectAndClassify>` | Emission detection settings |
| `<FrequencyHopping>` | FH detection settings |
| `<ResourceManager>` | Hardware resource allocation |

### 2.3 Start a Data Stream (example)

```xml
<Request type="set" id="68917">
  <Control>
    <DataStream action="start" type="tunerSpectrum">
      <Protocol>tcp</Protocol>
      <IP>192.168.1.226</IP>   <!-- your listener IP -->
      <Port>14776</Port>       <!-- your listener port (>9200) -->
    </DataStream>
  </Control>
</Request>
```

The CA120 replies with the same `id` and immediately begins pushing AMMOS frames
to `192.168.1.226:14776`.

### 2.4 Stop a Data Stream

```xml
<Request type="set" id="69035">
  <Control>
    <DataStream action="stop" type="tunerSpectrum">
      <IP>192.168.1.226</IP>
      <Port>14776</Port>
    </DataStream>
  </Control>
</Request>
```

---

## 3. AMMOS Binary Data Stream

### 3.1 Frame Structure

Every AMMOS frame has the same 3-part layout:

```
[ Frame_Header 24 bytes ][ Data_Header (variable) ][ Data_Body (variable) ]
```

### 3.2 Frame_Header — 24 bytes (6 × uint32)

Endianness is not explicitly stated in the ICD; treat as host-native (little-endian on Windows).

| Offset | Field | Type | Value / Notes |
|---|---|---|---|
| 0x00 | MagicWord | uint32 | **0xFB746572** — sync / frame boundary |
| 0x04 | FrameLength | uint32 | Total frame size in 32-bit words (max 0x100000). Next MagicWord is at `offset + (FrameLength−1)×4` |
| 0x08 | FrameCount | uint32 | Sequential counter; rolls over. Gap → lost frame |
| 0x0C | FrameType | uint32 | Data type (see §3.3) |
| 0x10 | DataHeaderLength | uint32 | Size of the Data_Header in 32-bit words |
| 0x14 | Reserved | uint32 | Bit 0 = extended frame flag; rest = 0 |

**Synchronisation rule:** scan for `0xFB746572` to align to frame boundaries.
The next frame starts exactly `FrameLength × 4` bytes after the current MagicWord.

### 3.3 FrameType Values

| FrameType | Stream |
|---|---|
| 0x01 | IF Data (raw I/Q samples) |
| 0x13 | Spectrum Data (FFT bins) |
| 0x100 | Audio Data (demodulated) |
| 0x130 | Symbol Data |
| 0x200 | PDW / IQDW Data |

### 3.4 IF Data Header (FrameType 0x01)

Follows immediately after Frame_Header. Size given by `DataHeaderLength`.

| Offset | Field | Type | Description |
|---|---|---|---|
| 0x00 | DatablockCount | uint32 | Number of data blocks in this frame |
| 0x04 | DatablockLength | uint32 | Length of each block in words (excl. block header) |
| 0x08 | bigtimeTimeStamp | int64 | µs since 1 Jan 1970 UTC |
| 0x10 | StatusWord | uint32 | Bitfield (see §3.5) |
| 0x14 | SignalSourceID | uint32 | Antenna / signal source ID |
| 0x18 | SignalSourceState | uint32 | Config set ID or scan step |
| 0x1C | TunerFrequency_Low | uint32 | Center frequency low 32 bits (Hz) |
| 0x20 | TunerFrequency_High | uint32 | Center frequency high 32 bits |
| 0x24 | Bandwidth | uint32 | IF bandwidth (Hz) |
| 0x28 | Samplerate | uint32 | ADC sample rate (samples/s) |
| 0x2C | uintInterpolation | uint32 | Interpolation factor |
| 0x30 | Decimation | uint32 | Decimation factor |
| 0x34 | AntennaVoltageRef | uint32 | Front-end correction (0.1 dBµV) |
| 0x38 | Ext_bigtimeStartTimeStamp | int64 | Extended timestamp (ns since 1970) |
| 0x40 | Ext_SampleCounter_Low | uint32 | 64-bit sample counter low |
| 0x44 | Ext_SampleCounter_High | uint32 | 64-bit sample counter high |
| 0x48 | Ext_KFactor | uint32 | Antenna k-factor (0.1 dB/m); 0x80000000 = undefined |

### 3.5 StatusWord Bitfield (IF stream)

| Bit | Name | Meaning |
|---|---|---|
| 31 | ChangeFlag | 1 = at least one field changed from previous header |
| 30 | dBFS_Flag | 1 = samples in dBFS; 0 = use AntennaVoltageRef + RecipGain |
| 29–8 | Reserved | Must be 0 |
| 7–0 | UserFlags | Internal signaling |

### 3.6 Audio Data Header (FrameType 0x100)

| Offset | Field | Type | Description |
|---|---|---|---|
| 0x00 | Samplerate | uint32 | Audio sample rate (Hz) |
| 0x04 | StatusWord | uint32 | Audio squelch status flags |

---

## 4. Suggested JSON Output Schema

The parser DLL should emit one JSON object per parsed frame.

### IF Data Frame
```json
{
  "hw": "ca120",
  "stream": "if_data",
  "frame_count": 12345,
  "timestamp_us": 1748786400000000,
  "source_id": 1,
  "frequency_hz": 105000000,
  "bandwidth_hz": 10000000,
  "sample_rate_hz": 25000000,
  "datablock_count": 4
}
```

### Spectrum Frame
```json
{
  "hw": "ca120",
  "stream": "spectrum",
  "frame_count": 12346,
  "timestamp_us": 1748786400100000,
  "source_id": 1,
  "frequency_hz": 105000000,
  "bandwidth_hz": 10000000
}
```

### Audio Frame
```json
{
  "hw": "ca120",
  "stream": "audio",
  "frame_count": 12347,
  "sample_rate_hz": 8000
}
```

---

## 5. Step-by-Step: Building `ca120_parser.dll`

### Step 1 — Project skeleton

```
ca120_parser/
  CMakeLists.txt
  include/
    ca120_parser.h      ← 4-symbol C ABI declaration
  src/
    ca120_parser.cpp    ← DLL entry point
    ammos_frame.h       ← internal frame structs
    ammos_frame.cpp     ← parse logic
    xml_control.h       ← XML helper (reuse from demo)
    xml_control.cpp
```

### Step 2 — Declare the 4-symbol C ABI

```c
// ca120_parser.h
#ifdef __cplusplus
extern "C" {
#endif

// Returns > 0 (bytes needed) when buf holds a complete frame; 0 = incomplete; -1 = bad magic.
int  extract_frame   (const uint8_t* buf, int len);

// Parses a complete AMMOS frame. Returns opaque handle or NULL on error.
void* parse_message  (const uint8_t* buf, int len);

// Serialises the parsed message to JSON. Returns bytes written or -1.
int  format_response (void* msg, char* out_json, int cap);

// Frees the handle returned by parse_message.
void free_result     (void* msg);

#ifdef __cplusplus
}
#endif
```

### Step 3 — Implement `extract_frame`

```cpp
// AMMOS frame total size = FrameLength field × 4 bytes.
int extract_frame(const uint8_t* buf, int len) {
    if (len < 8) return 0;                         // need at least header + length field
    uint32_t magic;
    memcpy(&magic, buf, 4);
    if (magic != 0xFB746572u) return -1;           // bad magic
    uint32_t words;
    memcpy(&words, buf + 4, 4);
    int total = (int)(words * 4);
    return (len >= total) ? total : 0;             // 0 = wait for more bytes
}
```

### Step 4 — Implement `parse_message`

```cpp
struct AmmosParsed {
    uint32_t frame_type;
    uint32_t frame_count;
    // IF fields:
    int64_t  timestamp_us;
    uint64_t frequency_hz;
    uint32_t bandwidth_hz;
    uint32_t sample_rate_hz;
    uint32_t source_id;
    uint32_t datablock_count;
    // Audio fields:
    uint32_t audio_sample_rate;
};

void* parse_message(const uint8_t* buf, int len) {
    if (len < 24) return nullptr;
    auto* out = new AmmosParsed{};
    memcpy(&out->frame_type,  buf + 0x0C, 4);
    memcpy(&out->frame_count, buf + 0x08, 4);
    uint32_t dhl; memcpy(&dhl, buf + 0x10, 4);
    const uint8_t* dh = buf + 24;               // Data Header starts here
    if (out->frame_type == 0x01 && len >= 24 + (int)(dhl*4)) {
        memcpy(&out->datablock_count, dh + 0x00, 4);
        int64_t ts; memcpy(&ts, dh + 0x08, 8);  out->timestamp_us = ts;
        memcpy(&out->source_id,      dh + 0x14, 4);
        uint32_t fl, fh;
        memcpy(&fl, dh + 0x1C, 4);
        memcpy(&fh, dh + 0x20, 4);
        out->frequency_hz   = ((uint64_t)fh << 32) | fl;
        memcpy(&out->bandwidth_hz,   dh + 0x24, 4);
        memcpy(&out->sample_rate_hz, dh + 0x28, 4);
    } else if (out->frame_type == 0x100 && len >= 24 + 8) {
        memcpy(&out->audio_sample_rate, dh + 0x00, 4);
    }
    return out;
}
```

### Step 5 — Implement `format_response`

```cpp
int format_response(void* msg, char* out, int cap) {
    auto* f = static_cast<AmmosParsed*>(msg);
    const char* stream = (f->frame_type == 0x01) ? "if_data"
                       : (f->frame_type == 0x13) ? "spectrum"
                       : (f->frame_type == 0x100) ? "audio"
                       : "unknown";
    return snprintf(out, cap,
        "{\"hw\":\"ca120\",\"stream\":\"%s\",\"frame_count\":%u,"
        "\"timestamp_us\":%lld,\"source_id\":%u,"
        "\"frequency_hz\":%llu,\"bandwidth_hz\":%u,"
        "\"sample_rate_hz\":%u,\"datablock_count\":%u}",
        stream, f->frame_count,
        (long long)f->timestamp_us, f->source_id,
        (unsigned long long)f->frequency_hz,
        f->bandwidth_hz, f->sample_rate_hz, f->datablock_count);
}
```

### Step 6 — Implement `free_result`

```cpp
void free_result(void* msg) {
    delete static_cast<AmmosParsed*>(msg);
}
```

### Step 7 — CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(ca120_parser)
set(CMAKE_CXX_STANDARD 17)

add_library(ca120_parser SHARED
    src/ca120_parser.cpp
    src/ammos_frame.cpp
    src/xml_control.cpp
)
target_include_directories(ca120_parser PRIVATE include)
if(MSVC)
    target_compile_options(ca120_parser PRIVATE /W4 /wd4100)
endif()
# No LGPL libraries — static link only.
```

### Step 8 — Testing checklist

- [ ] Feed `extract_frame` a 20-byte buffer with magic `0xFB746572` + FrameLength=10 → returns 0 (incomplete)
- [ ] Feed same with 40-byte buffer → returns 40
- [ ] Feed wrong magic bytes → returns -1
- [ ] Feed a crafted IF frame → `parse_message` → `format_response` → valid JSON with correct frequency
- [ ] Feed a real AMMOS capture (Wireshark PCAP export) if available
- [ ] Check `FrameCount` sequential to detect lost frames in stream test

---

## 6. Key Notes

- CA120 XML uses **no binary wrapper** — raw XML text only.
- AMMOS data ports are **dynamic**: the client picks a free port > 9200 and tells CA120 via XML.
- FrameCount is your lost-frame detector — log a warning whenever `current − previous > 1`.
- For audio streams (30 ms packets), expect one frame every 30 ms per active audio channel.
