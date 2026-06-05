# EWTSS v2 — ICD Protocol Reference (COMM DF Receiver)

**Audience:** the C++ developer implementing a `drs-bridge` parser library; reviewers checking that an ICD has been faithfully translated to code; the C++ developer adding the *next* hardware variant who needs a worked example to mirror.
**Purpose:** end-to-end reference for one hardware variant — transport, framing, command groups, RF parameters, parser-API mapping, YAML profile, Kafka topics, TimescaleDB columns. The framing pattern documented here is the template; other variants (RDFS, JV/UHF, JHF, etc.) deviate only in command IDs, payload layouts, and RF parameters.
**Source:** the COMM DF Receiver ICD (the example ICD shipped to the development team for v2). Other ICDs will use different sheet names and different command groupings; the four sheet conventions in §6 are the bridge.

---

## 1. Why a worked example

`drs-bridge` is generic by design — one YAML profile + one C++ parser source per variant, plus a generic `ResponseRouter` and asyncio TCP server (see [Developer Handbook §12](developer-handbook.md#12-drs-bridge-internal-design)). But "generic" doesn't help the developer staring at an Excel ICD for the first time. This document follows one ICD top-to-bottom and shows exactly what code each section of the ICD turns into.

When you add the next hardware variant:

1. Read this document end-to-end once.
2. Open the new variant's ICD alongside.
3. The shape will mostly mirror this. The places it differs are exactly the places you write parser logic.

---

## 2. Transport layer

| Parameter | Value |
|---|---|
| Protocol | TCP/IP |
| Bandwidth | 1 Gbps |
| Direction | Full-duplex: SDFC ↔ DRS |
| Frame format | Fixed header + variable payload + fixed footer |
| Max payload | 1 MB per message |

Two distinct frame formats appear on the wire: the **SDFC↔DRS format** (4-byte headers, used for command/response between the scenario controller and DRS hardware) and the **SCD↔DRS format** (compact 2-byte header, used for direct controller communication). The same parser library handles both.

---

## 3. Frame formats

### 3.1 SDFC↔DRS command (SDFC → DRS)

```
┌────────────────┬──────────────┬────────────────┬────────────────┬───────────────────┬────────────────┐
│ Header         │ Message Size │ Command        │ Command        │ Message Data      │ Footer         │
│ 4 bytes        │ 4 bytes      │ Group ID       │ Unit ID        │ 0 – 1,048,576 B   │ 4 bytes        │
│ 0xAA AB BA BB  │ uint32 LE    │ 2 bytes ushort │ 2 bytes ushort │                   │ 0xCC CD DC DD  │
└────────────────┴──────────────┴────────────────┴────────────────┴───────────────────┴────────────────┘
```

- Header magic: `0xAA 0xAB 0xBA 0xBB`
- Footer magic: `0xCC 0xCD 0xDC 0xDD`
- Total fixed overhead: **16 bytes** (4 header + 4 size + 2 group + 2 unit + 4 footer)
- `extract_frame` returns `1` (FRAME_SDFC_CMD) on a successful match.

### 3.2 SDFC↔DRS response (DRS → SDFC)

```
┌────────────────┬────────────────┬──────────────┬─────────────────┬─────────────────┬───────────────────┬────────────────┐
│ Header         │ Message Status │ Message Size │ Response        │ Response        │ Message Data      │ Footer         │
│ 4 bytes        │ 2 bytes short  │ 4 bytes      │ Group ID        │ Unit ID         │ 0 – 1,048,576 B   │ 4 bytes        │
│ 0xEE EF FE FF  │ int16 LE       │ uint32 LE    │ 2 bytes ushort  │ 2 bytes ushort  │                   │ 0xFF FE EF EE  │
└────────────────┴────────────────┴──────────────┴─────────────────┴─────────────────┴───────────────────┴────────────────┘
```

- Header magic: `0xEE 0xEF 0xFE 0xFF`
- Footer magic: `0xFF 0xFE 0xEF 0xEE`
- Total fixed overhead: **18 bytes** (4 header + 2 status + 4 size + 2 group + 2 unit + 4 footer)
- `extract_frame` returns `2` (FRAME_SDFC_RESP).

### 3.3 SCD↔DRS compact

```
┌───────────────┬──────────────────┬────────────────────┬──────────────────────┬───────────────────┬──────────────┐
│ Header        │ Command Code     │ Sequence Number    │ Message Data Length  │ Message Data      │ Footer       │
│ 2 bytes       │ 2 bytes ushort   │ 2 bytes ushort     │ 4 bytes uint         │ 0 – 1,048,576 B   │ 2 bytes      │
│ 0xAA 0xAA     │                  │                    │                      │                   │ 0xEE 0xEE    │
└───────────────┴──────────────────┴────────────────────┴──────────────────────┴───────────────────┴──────────────┘
```

- Header magic: `0xAA 0xAA`
- Footer magic: `0xEE 0xEE`
- Total fixed overhead: **12 bytes** (2 header + 2 code + 2 seq + 4 length + 2 footer)
- `extract_frame` returns `3` (FRAME_SCD).

> **First-byte ambiguity:** the SDFC command header (`0xAA 0xAB ...`) and the SCD compact header (`0xAA 0xAA`) share `0xAA` as the first byte. Header detection must check the second byte before committing to a frame type. This is why `extract_frame` returns the frame type (rather than having `parse_message` re-scan the header) — the disambiguation happens once.

---

## 4. Command groups and command IDs

The ICD organises commands into numbered groups. Each frame embeds a Group ID; the Command/Response Unit IDs distinguish individual operations within the group. For COMM DF, three groups are defined.

### 4.1 Group 0 — System Management (Group ID 100)

| Operation | Command Unit ID | Response Unit ID | Response payload |
|---|---|---|---|
| Get System Version | 1 | 2 | 26 bytes — versions + IDs (see §4.4) |
| PBIT Status | 5 | 6 | TBD per ICD revision |
| Module Health Status | 31 | — | Per-module health bitmask (no Response Unit ID — pushed asynchronously) |

### 4.2 Group 1 — RF Measurement and Detection (Group ID 101)

| Operation | Command Unit ID | Response Unit ID | Response payload |
|---|---|---|---|
| Set Threshold | 25 | — | ACK only |
| Set Resolution | 27 | — | ACK only |
| Set Min/Max Pulse Range | 47 | — | ACK only |
| Configure Detection | 37 | — | ACK only |
| Get FFT Data | 43 | 44 | (1600 × 4) + 4 bytes = 6,404 bytes (1600 bins × float32 + 4-byte header) |
| Start FH Detection | 39 | 40 | 2 + 2 + (64 × hopper_count) bytes |
| Start FF Detection | 69 | 70 | TBD per ICD revision (fixed-frequency detection result) |
| Start Burst Detection | 83 | 84 | 4 + (52 × burst_count) bytes |

### 4.3 Group 11 — BITE (Built-In Test Equipment) (Group ID 111)

| Operation | Command Unit ID | Response Unit ID | Response payload |
|---|---|---|---|
| BITE Enable | 1 | — | ACK only |
| BITE Disable | 1 | — | ACK only |
| Signal BITE Test | 3 | 4 | 12 bytes |
| All Channel BITE Test | — | — | TBD per ICD revision |

### 4.4 System Version response (Group 100, RespID 2) — byte layout

```
Offset  Size  Field
0       4     Firmware Version (uint32)
4       4     Driver Version  (uint32)
8       4     FPGA Version    (uint32)
12      4     BSP Version     (uint32)
16      2     Processor ID    (uint16)
18      2     RF Tuner ID [0] (uint16)
20      2     RF Tuner ID [1] (uint16)
22      2     RF Tuner ID [2] (uint16)
24      2     FPGA Type ID    (uint16)
```

26 bytes total. The Version fields are packed integers (typically major.minor.patch encoded into uint32 — actual packing per ICD revision; check the spec).

---

## 5. RF configuration parameters

The COMM DF ICD defines four RF station types with distinct frequency plans. The parser must validate frequency values in `Set Resolution`, `Set Threshold`, and `Set Min/Max Pulse Range` payloads against the active station's range.

| RF Station | Frequency range | Channel count | Channel spacing | Max hop rate |
|---|---|---|---|---|
| VHF CNR | 30–88 MHz | 2,320 | 25 kHz | 250 hops/sec |
| Bluetooth | 2.402–2.480 GHz | 79 | 1 MHz | 1,600 hops/sec |
| Motorola | 902–928 MHz | 50 | 0.5 MHz | 11 hops/sec |
| SDR | 30–512 MHz | — | — | 500 hops/sec |

The `RF Config and Params` sheet of the ICD Excel maps to these rows. The codegen tool extracts them and uses them to size `receive_buffer_bytes` and to emit per-station validation constants.

---

## 6. FF detection command sequence

Fixed Frequency detection is a multi-step sequence — the SDFC must issue commands in this order:

```
1. Set Threshold        (Group 1, CmdID 25)   →  ACK
2. Set Resolution       (Group 1, CmdID 27)   →  ACK
3. Configure Detection  (Group 1, CmdID 37)   →  ACK
4. BITE Enable          (Group 11, CmdID 1)   →  ACK
5. (optional) Reference measurement
6. Start FF Detection   (Group 1, CmdID 69)   →  Response (RespID 70)
```

`drs-bridge` is **stateless with respect to command ordering** — each command is parsed atomically and the parser emits one JSON event per response. Sequencing is the SDFC's responsibility (in EWTSS v2 today, the scenario publisher: `Sg.App` for Mode A, `Sg.Server` for Mode B). If a future ICD revision adds an FSM that drs-bridge must enforce, it goes in `ResponseRouter` (Layer 3 of [Developer Handbook §12](developer-handbook.md#12-drs-bridge-internal-design)) — not in the C++ parser.

---

## 7. C++ parser — framing constants and dispatch

The framing constants above map directly to the C++ parser's frame-detection layer.

> **Starting point:** the canonical 4-symbol ABI + a buildable scaffold lives at
> [`drs-bridge/parsers/reference/`](../../drs-bridge/parsers/reference/). Copy that
> directory to `drs-bridge/parsers/comm_df/`, rename the source files, and replace
> the synthetic 1-byte-magic frame format with the COMM DF layouts documented in §3.
> See [Developer Handbook §9.3](developer-handbook.md#93-c-parser-interface-contract)
> for the per-step walkthrough. The ABI symbols, CMake config, and pytest integration
> harness do not need to be reinvented per variant.

### 7.1 Frame constants header

```cpp
// parsers/comm_df/frame_constants.h

// SDFC→DRS command frame markers
static constexpr uint8_t CMD_HEADER[4]  = {0xAA, 0xAB, 0xBA, 0xBB};
static constexpr uint8_t CMD_FOOTER[4]  = {0xCC, 0xCD, 0xDC, 0xDD};

// DRS→SDFC response frame markers
static constexpr uint8_t RESP_HEADER[4] = {0xEE, 0xEF, 0xFE, 0xFF};
static constexpr uint8_t RESP_FOOTER[4] = {0xFF, 0xFE, 0xEF, 0xEE};

// SCD↔DRS compact frame markers
static constexpr uint8_t SCD_HEADER[2]  = {0xAA, 0xAA};
static constexpr uint8_t SCD_FOOTER[2]  = {0xEE, 0xEE};

// Fixed overhead sizes
static constexpr int CMD_FRAME_OVERHEAD  = 16;   // 4 + 4 + 2 + 2 + 4
static constexpr int RESP_FRAME_OVERHEAD = 18;   // 4 + 2 + 4 + 2 + 2 + 4
static constexpr int SCD_FRAME_OVERHEAD  = 12;   // 2 + 2 + 2 + 4 + 2
static constexpr int MAX_PAYLOAD_BYTES   = 1048576;
```

This file is generated by `tools/icd_codegen` from the `Command-Response Structures` sheet of the ICD Excel (see [`specs/icd-codegen-tool-design.md`](specs/icd-codegen-tool-design.md)). Do not hand-edit the constants; if the ICD revises a magic byte, re-run the codegen.

### 7.2 `extract_frame` algorithm

1. Scan the buffer for any of the three header magic sequences.
2. Once a header is found, read the `Message Size` field at the appropriate offset for that frame type.
3. Validate `size ≤ MAX_PAYLOAD_BYTES`.
4. Check that the buffer holds at least `overhead + size` bytes.
5. Verify the footer magic at offset `overhead + size − footer_len`.
6. Copy the complete frame to `out_frame`; set `out_len`; return the frame type (1, 2, or 3).
7. If any validation fails: return 0 (incomplete) if more bytes might complete the frame, or -1 (corrupt) if the header was matched but size or footer check failed unrecoverably.

### 7.3 `parse_message` dispatch

```cpp
const char* parse_message(const uint8_t* frame, int frame_len, int frame_type) {
    JsonWriter w;
    FrameHeader hdr = decode_header(frame, frame_type);
    w.set("group_id",   hdr.group_id);
    w.set("unit_id",    hdr.unit_id);
    w.set("frame_type", frame_type == FRAME_SDFC_RESP ? "response" : "command");
    if (frame_type == FRAME_SDFC_RESP) w.set("status", hdr.status);

    switch (hdr.group_id) {
        case 100: parse_group0(frame  + hdr.payload_offset, hdr.payload_len, hdr.unit_id, w); break;
        case 101: parse_group1(frame  + hdr.payload_offset, hdr.payload_len, hdr.unit_id, w); break;
        case 111: parse_group11(frame + hdr.payload_offset, hdr.payload_len, hdr.unit_id, w); break;
        default:  w.set("raw_hex", to_hex(frame + hdr.payload_offset, hdr.payload_len));
    }
    return w.release();   // caller calls free_result()
}
```

Each `parse_groupN` is a `switch (unit_id)` with one `case` per command. The codegen tool emits the `case` skeletons with `TODO` comments listing the field names + types from the ICD; the developer fills in the field-decode body.

---

## 8. Example JSON outputs

These are the JSON shapes the parser library is expected to produce, drawn from the ICD's defined response structures.

### 8.1 System Version (Group 100, RespID 2)

```json
{
  "group_id": 100,
  "unit_id": 2,
  "frame_type": "response",
  "status": 0,
  "fw_version": "2.4.1",
  "driver_version": "1.0.3",
  "fpga_version": "3.2.0",
  "bsp_version": "1.1.0",
  "processor_id": 7,
  "rf_tuner_ids": [1, 2, 3],
  "fpga_type_id": 4
}
```

### 8.2 FH Detection (Group 101, RespID 40) — 3 hoppers

```json
{
  "group_id": 101,
  "unit_id": 40,
  "frame_type": "response",
  "status": 0,
  "hopper_count": 3,
  "hoppers": [
    {"channel": 14, "frequency_hz": 48350000, "power_dbm": -61.2, "dwell_us": 4000},
    {"channel": 27, "frequency_hz": 56750000, "power_dbm": -58.8, "dwell_us": 3900},
    {"channel": 41, "frequency_hz": 65150000, "power_dbm": -63.1, "dwell_us": 4100}
  ]
}
```

### 8.3 Burst Detection (Group 101, RespID 84) — 2 bursts

```json
{
  "group_id": 101,
  "unit_id": 84,
  "frame_type": "response",
  "status": 0,
  "burst_count": 2,
  "bursts": [
    {"start_us": 1712394820100000, "duration_us": 12400, "frequency_hz": 34200000, "power_dbm": -55.4, "bandwidth_hz": 25000},
    {"start_us": 1712394820150000, "duration_us": 9800,  "frequency_hz": 34225000, "power_dbm": -57.1, "bandwidth_hz": 25000}
  ]
}
```

### 8.4 Output conventions (apply to every variant)

- All numeric scalars in SI units: `frequency_hz`, `power_dbm`, `azimuth_deg`, `elevation_deg`, `dwell_us`. Hardware-native units (kHz, dBuV) are converted in the parser.
- `frame_type` is always `"command"` or `"response"` — the dispatcher uses this to route to the cmd-record vs response-record Kafka topic.
- Arrays are sized by the count field; the parser reads `count`, then reads `count × element_size` bytes from the payload.
- Unknown commands (default branch) emit `raw_hex` so debugging can recover the bytes without crashing the bridge.

---

## 9. Hardware profile YAML

```yaml
# profiles/comm_df.yaml
name: comm_df
port: 5490                          # TCP port
kafka_topic: comm_df.drs.ui
kafka_broker: "${KAFKA_BROKER}"
parser_lib: parsers/libcomm_df.dll
max_connections: 4
health_interval_ms: 1000
receive_buffer_bytes: 65536         # large buffer: FFT response is 6,404 bytes per call
frame_terminator: "magic_bytes"
protocols: [sdfc_drs, scd_drs]
protocol_version: "ICD-COMM-DF-v1"
group_ids: [100, 101, 111]
store_fft: true                     # set false to drop FFT data before Kafka (see §11)
```

`receive_buffer_bytes: 65536` (64 KB) — not the 8 KB default — because the FFT Data response (Group 1, RespID 44) is 6,404 bytes per call, and Burst Detection payloads scale with burst count. Sizing this correctly is the responsibility of whoever writes the YAML; the codegen tool reads the largest declared response from the ICD's `Group N Commands` sheets and emits a sensible default.

---

## 10. Kafka topic mapping

The `comm_df` parser produces messages on these topics, all consumed by `drs-server`:

| Topic | Source group / command | Message types | TimescaleDB target |
|---|---|---|---|
| `comm_df.drs.ui` | 101 (RF detection) | FF, FH, Burst detection results | `measurements` hypertable |
| `comm_df.system.version` | 100, RespID 2 | System version response | `system_versions` table (regular table — written once per session) |
| `comm_df.system.health` | 100, CmdID 31 | Module health status | `health_status` hypertable |
| `comm_df.bite.result` | 111 | Signal / All-Channel BITE results | `system_logs` hypertable |
| `comm_df.cmd` (auto-suffix) | All inbound commands | Audit trail of SDFC→DRS commands | `measurements` hypertable (for audit queries) |
| `entity.comm_df.response` | All outbound responses (in Integrated mode only) | DRS→entity-app responses | `measurements` hypertable, indexed by entity |

Naming convention: `<hardware_type>.<domain>.<subtype>`. The convention is the same across all variants — adding a variant adds topics by name, no new infrastructure.

---

## 11. TimescaleDB column mapping

ICD response fields map to the `measurements` hypertable as follows (schema in [Developer Handbook §10.2](developer-handbook.md#102-time-series-measurement-schema-drs-server-writes)):

| ICD field | `measurements` column | Notes |
|---|---|---|
| recv timestamp at the bridge | `recorded_at` | Partition key — bridge stamps wall-clock on receive |
| `group_id` / `unit_id` | `message_type` | Encoded `"G{group_id}_U{unit_id}"` for query convenience |
| (context from active session) | `session_id` | Injected by `drs-server` consumer from session registry |
| `"comm_df"` | `hardware_type` | Constant per bridge instance |
| `hopper_count` + hopper array | `payload` (jsonb) | Full FH record stored in jsonb; scalar `frequency_hz` duplicated to `measurement_scalars` |
| `burst_count` + burst array | `payload` (jsonb) | Full burst array in jsonb; scalar `power_dbm` duplicated to `measurement_scalars` |
| FFT 1600-bin float32 array | `payload` (jsonb) | Stored as JSON array; large — see volume note below |
| `fw_version`, `driver_version`, etc. | `system_versions` (separate table) | Flat columns; written once per session startup |

> **FFT data volume:** at 1600 bins × float32 × 20 Hz = ~128 KB/s per COMM DF instance. With 100 instances, that's ~12.8 MB/s of FFT data alone. If post-session FFT analysis isn't required, set `store_fft: false` in the YAML profile — the bridge drops FFT messages before Kafka production. If it *is* required, consider provisioning a dedicated `fft_data` hypertable with shorter retention (1–7 days) rather than letting it inflate the general `measurements` table.

---

## 12. ICD-revision change-management

ICDs do change. When the customer issues a new revision:

1. Diff the new ICD against the version on file. Focus on §3 (frame magic / overhead) and §4 (group/command IDs) — these are the breaking changes.
2. Re-run `tools/icd_codegen` against the new ICD into a side directory.
3. Diff the regenerated `frame_constants.h` and the `*_frame_types.h` constants files against the in-tree ones.
4. If only constants changed (most common case for revisions): apply the diff and commit; existing field-decode bodies still apply.
5. If new commands or new fields were added: the codegen will emit new `case` blocks with `TODO` comments. Implement the new decoders by hand.
6. If commands were *removed* or *renamed*: update the `case` blocks manually; the codegen does not delete code (regeneration produces additions only).
7. Bump `protocol_version` in the YAML profile.
8. Add a regression test: capture one frame of each new command type using a hardware simulator and add to the parser's golden-frame test suite.

> **The codegen tool is run-once-per-revision, not run-on-every-build.** Generated files are normal source files. They get edited, committed, reviewed; they do not regenerate on `cmake --build`. This is intentional: hand-written field decoders must not be overwritten by accidental regeneration.

---

## 13. Mapping conventions for other variants

When the next variant arrives (e.g. RDFS, JV/UHF, JHF), the *shape* of this document mostly carries over. What changes:

| Section | What's variant-specific |
|---|---|
| §2 Transport | Same TCP/IP for all defence-radio variants. GNSS variants may use UDP. |
| §3 Frame formats | Most use SDFC↔DRS + SCD↔DRS; a few use proprietary framing. NMEA variants use line-terminated ASCII (no `extract_frame` magic-byte scan — special-case in the C++ parser). |
| §4 Command groups | Different group IDs per variant; one parser case per group. |
| §5 RF parameters | Variant-specific frequency ranges, modulations, hop rates. |
| §6 Sequences | Variant-specific. Some variants are stateless (single-shot commands); others have multi-step initialisation sequences like FF detection above. |
| §10 Kafka topic mapping | Variant name prefixes the topic; family `<variant>.drs.ui` etc. is consistent. |
| §11 TimescaleDB column mapping | Most variants reuse `measurements` + `measurement_scalars`. Variants with bulk float arrays (FFT, IQ samples) may warrant a dedicated hypertable with shorter retention. |

To document the next variant: copy this file, rename for the variant, update §2–§6, §8 examples, §9 YAML, §10 topic table, §11 column mapping. §1, §7, §12, §13 are largely shared boilerplate and can be referenced rather than copied.

---

## 14. References

- [Developer Handbook §9](developer-handbook.md#9-how-to-add-a-new-hardware-variant-telemetry-phase) — the procedural runbook for adding a variant.
- [Developer Handbook §12](developer-handbook.md#12-drs-bridge-internal-design) — `drs-bridge` internal layered design + the C++ parser ABI this document conforms to.
- [Developer Handbook §10](developer-handbook.md#10-database-schema-reference) — full TimescaleDB schema.
- [`specs/icd-codegen-tool-design.md`](specs/icd-codegen-tool-design.md) — the ICD code-generator that produces the constants header + dispatch skeleton from the ICD Excel.
- [v2 tech-stack archive §19](specs/v2-tech-stack-archive.md) — original specification, archived.
