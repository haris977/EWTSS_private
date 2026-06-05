# ICD Code Generator (`tools/icd_codegen`) — Design

**Status:** design — not yet implemented. Lifted from the v2 tech-stack archive §20 and re-scoped for the Hybrid architecture.
**Audience:** the C++ developer adding hardware variants in the v2 hardening phase; the team lead deciding whether to build this tool or hand-write each variant.
**Decision asked:** build this as part of the v2 hardening phase, or defer / drop.

---

## 1. Why this exists

EWTSS v2 supports 12+ hardware variants (RDFS, JV/UHF, JHF, SJRR, JLB, JMB, JHB×4, AUS, PADS, plus additions). Each variant ships with an Interface Control Document (ICD) — typically an Excel workbook describing frame formats, command groups, command/response IDs, payload field layouts, and RF parameter ranges.

Two ways to turn an ICD into a working `drs-bridge` parser library:

1. **Hand-write everything.** The C++ developer reads the ICD and types out the constants, dispatch structure, and field decoders. Error-prone for the ~80% of the parser that is mechanical (constants, dispatch skeleton, frame-format tables); valuable for the ~20% requiring judgement (bit-packing, conditional layouts, enumeration semantics).
2. **Generate the mechanical parts; hand-write the rest.** A small tool reads the ICD Excel, emits the C++ skeleton + YAML profile + TypeScript types. The developer fills in the field-decode bodies. Generated files are then edited and not regenerated.

The legacy system used a half-measure: ICDs were hand-transcribed into per-variant `command.csv` + `structure.csv` files. This worked but introduced four fragility points (silent `ast.literal_eval` failures, no TCP stream reassembly, inline magic bytes scattered across files, 50-branch if-elif dispatch). v2 replaces both the CSV tables and the runtime parser with compiled C++; the question is whether the *generation step* gets tooled.

**Recommendation:** build the tool. With 12+ variants ahead, the per-variant savings amortise quickly. The tool is small (~500 LOC of Python), uses two offline-vendorable dependencies, and produces editable output (no regeneration round-trip needed).

---

## 2. Scope

The generator writes **skeletons**:

- Frame-format constants (magic bytes, overhead sizes, max payload).
- Group / command / response ID constants.
- Frame extraction header-scan structure.
- Group-dispatch `switch` skeleton with one `case` per command.
- `TODO` comments inside each `case` listing the fields to decode.
- Hardware profile YAML with sensible defaults.
- TypeScript interfaces for the response messages.

The generator does **not** produce:

| Not generated | Reason |
|---|---|
| Field-decode bodies inside `parse_group_NNN()` | Requires judgement: bit-packing, multi-field conditions, enumeration semantics not fully expressible in Excel |
| Checksum / CRC validation | Algorithm + polynomial are not present in the ICD Excel; must be confirmed from the hardware spec |
| Unit tests for the parser | Test cases need real captured frames; cannot be synthesised from Excel alone |
| TimescaleDB migration SQL | Column-level schema decisions (what to index, what goes in jsonb vs scalars) require DBA review |
| Kafka topic names beyond the default convention | Topic naming policy is a system-level decision, not ICD-driven |

Generated files are normal source files. The developer edits them, commits them, and the tool is not re-run on the same hardware variant. If the ICD is later revised, the developer either applies the diff manually or runs the tool against the new ICD into a side directory and merges.

---

## 3. Tool structure

```
tools/icd_codegen/
  icd_codegen.py              CLI entry point
  excel_reader.py             extracts protocol facts from ICD Excel sheets
  generators/
    cpp_parser.py             produces {hw}_parser.cpp + {hw}_frame_types.h
    yaml_profile.py           produces profiles/{hw}.yaml
    typescript_types.py       produces {hw}.types.ts
  templates/
    parser.cpp.j2             Jinja2: frame detect + group dispatch skeleton
    frame_types.h.j2          magic byte constants + group/cmd ID constants
    profile.yaml.j2
    types.ts.j2
  tests/
    test_excel_reader.py      tests against the COMM DF ICD as reference fixture
    fixtures/
      ICD_COMM_DF.xlsx        reference ICD used in tests
  packages/                   offline-vendored wheels
  requirements.txt            openpyxl>=3.1, jinja2>=3.1
```

---

## 4. CLI usage

```bash
python tools/icd_codegen/icd_codegen.py \
  --icd          "ICD_EWTSS.xlsx"                \
  --hw           comm_df                         \
  --port         5490                            \
  --out-parsers  drs-bridge/parsers/src/         \
  --out-profiles drs-bridge/profiles/            \
  --out-types    frontend/src/app/models/
```

Optional flags:

| Flag | Purpose |
|---|---|
| `--sheet-map '{"Command-Response Structures": "Frame Format", ...}'` | Override expected sheet names when an ICD uses non-standard naming |
| `--protocols sdfc_drs,scd_drs` | Declare which frame formats this device uses (defaults to `sdfc_drs` only) |
| `--max-payload 1048576` | Override max payload bytes (default 1 MB from ICD) |
| `--dry-run` | Print what would be generated without writing files |

The tool fails fast (non-zero exit, clear message to stderr) on any of:

- Missing or unreadable ICD file.
- Unrecognised sheet names with no `--sheet-map` override.
- Output directories that don't exist.
- Output files that already exist (use `--force` to overwrite, intentionally non-default — the tool's output is meant to be edited; overwriting an edited file silently is the worst possible default).

---

## 5. Expected ICD Excel sheet conventions

| Sheet name | Columns used | What is extracted |
|---|---|---|
| `Command-Response Structures` | Header, Footer, Size field offset | Frame format table: magic byte sequences, field widths, overhead sizes |
| `Group N Commands` (one per group) | Group ID, Cmd ID, Resp ID, Payload size, Field name, Type, Size | Group + command/response unit IDs; payload byte layouts |
| `All_commands` | Command name, Group, Cmd ID, Field name, Type, Min, Max | Human-readable command names → semantic constant identifiers |
| `RF Config and Params` | Station, Freq range, Channels, Spacing, Max hop rate | `receive_buffer_bytes` sizing guidance |

If an ICD uses different sheet names, `--sheet-map` remaps. The tool reports unrecognised or missing sheets clearly rather than silently skipping them.

---

## 6. `excel_reader.py` data model

The reader extracts a single `IcdDocument` value from the workbook:

```python
@dataclass
class FrameFormat:
    name: str                    # "sdfc_cmd", "sdfc_resp", "scd"
    header_bytes: bytes          # e.g. b"\xaa\xab\xba\xbb"
    footer_bytes: bytes
    size_field_offset: int       # byte offset of the length field
    size_field_width: int        # bytes (4 for uint32)
    fixed_overhead: int          # total bytes excluding payload

@dataclass
class FieldDef:
    name: str                    # "fw_version"
    c_type: str                  # "uint32_t"
    byte_size: int
    is_array: bool
    array_count: int | None
    value_min: float | None
    value_max: float | None

@dataclass
class CommandDef:
    group_id: int
    command_id: int
    response_id: int | None
    command_name: str            # "Get System Version"
    constant_name: str           # "CMD_GET_SYSTEM_VERSION" (generated)
    payload_fields: list[FieldDef]
    response_fields: list[FieldDef]

@dataclass
class IcdDocument:
    hw_name: str
    frame_formats: list[FrameFormat]
    groups: dict[int, list[CommandDef]]   # group_id → commands
    max_payload_bytes: int
    rf_stations: list[RfStation]

def read_icd(path: str, hw_name: str, sheet_map: dict | None = None) -> IcdDocument:
    wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
    doc = IcdDocument(hw_name=hw_name, ...)
    _parse_frame_formats(wb, doc, sheet_map)
    _parse_command_groups(wb, doc, sheet_map)
    _parse_rf_params(wb, doc, sheet_map)
    return doc
```

Each generator (`cpp_parser`, `yaml_profile`, `typescript_types`) takes an `IcdDocument` and renders a Jinja2 template against it. Generators are pure functions of the document.

---

## 7. Generated C++ output

Given a `CommandDef` for "Get System Version" (Group 100, CmdID 1, RespID 2):

### 7.1 `{hw}_frame_types.h` (generated, do not edit)

```cpp
// AUTO-GENERATED by tools/icd_codegen — do not edit
// Source: ICD_COMM_DF.xlsx  hw: comm_df

#pragma once
#include <cstdint>

namespace comm_df {

// Frame type tags (match extract_frame return values)
constexpr int FRAME_SDFC_CMD  = 1;
constexpr int FRAME_SDFC_RESP = 2;
constexpr int FRAME_SCD       = 3;

// SDFC→DRS command frame
constexpr uint8_t CMD_HEADER[4]  = {0xAA, 0xAB, 0xBA, 0xBB};
constexpr uint8_t CMD_FOOTER[4]  = {0xCC, 0xCD, 0xDC, 0xDD};
constexpr int     CMD_OVERHEAD   = 16;

// DRS→SDFC response frame
constexpr uint8_t RESP_HEADER[4] = {0xEE, 0xEF, 0xFE, 0xFF};
constexpr uint8_t RESP_FOOTER[4] = {0xFF, 0xFE, 0xEF, 0xEE};
constexpr int     RESP_OVERHEAD  = 18;

// SCD compact frame
constexpr uint8_t SCD_HEADER[2]  = {0xAA, 0xAA};
constexpr uint8_t SCD_FOOTER[2]  = {0xEE, 0xEE};
constexpr int     SCD_OVERHEAD   = 12;

constexpr int MAX_PAYLOAD = 1048576;

// Group IDs
constexpr uint16_t GROUP_SYSTEM_MGMT  = 100;
constexpr uint16_t GROUP_RF_DETECT    = 101;
constexpr uint16_t GROUP_BITE         = 111;

// Command Unit IDs — Group 100
constexpr uint16_t CMD_GET_SYSTEM_VERSION = 1;
constexpr uint16_t RESP_SYSTEM_VERSION    = 2;
constexpr uint16_t CMD_PBIT_STATUS        = 5;
// ... (one constant per command)

// Command Unit IDs — Group 101
constexpr uint16_t CMD_SET_THRESHOLD      = 25;
constexpr uint16_t CMD_SET_RESOLUTION     = 27;
constexpr uint16_t CMD_GET_FFT_DATA       = 43;
constexpr uint16_t RESP_FFT_DATA          = 44;
// ... etc.

} // namespace comm_df
```

### 7.2 `{hw}_parser.cpp` (skeleton, then hand-completed)

```cpp
// AUTO-GENERATED skeleton — fill in parse_group_NNN() bodies
#include "comm_df_frame_types.h"
#include "../common/parser_api.h"
#include "../common/json_writer.h"
using namespace comm_df;

// ── Frame extraction ─────────────────────────────────────────────────────────

int extract_frame(const uint8_t* buf, int buf_len,
                  uint8_t* out_frame, int* out_len) {
    // GENERATED: scan for CMD_HEADER, RESP_HEADER, SCD_HEADER
    // Returns FRAME_SDFC_CMD / FRAME_SDFC_RESP / FRAME_SCD / 0 / -1
    // (Header scan loop + length validation + footer check are emitted here.)
    return 0;  // replace with the generated implementation
}

// ── Message parsing ───────────────────────────────────────────────────────────

static void parse_group_100(const uint8_t* payload, int len,
                             uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {
        case RESP_SYSTEM_VERSION: {
            // TODO: decode 26-byte System Version response
            // Fields: fw_version(uint32), driver_version(uint32),
            //         fpga_version(uint32), bsp_version(uint32),
            //         processor_id(uint16), rf_tuner_ids[3](uint16),
            //         fpga_type_id(uint16)
            break;
        }
        // ... one case per command (generated)
        default:
            w.set("raw_hex", to_hex(payload, len));
    }
}

static void parse_group_101(const uint8_t* payload, int len,
                             uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {
        case RESP_FFT_DATA: {
            // TODO: decode 6404-byte FFT response
            // Fields: bin_count(uint32) + bins[1600](float32)
            break;
        }
        // ... generated cases for FH, FF, Burst, etc.
        default:
            w.set("raw_hex", to_hex(payload, len));
    }
}

const char* parse_message(const uint8_t* frame, int frame_len, int frame_type) {
    JsonWriter w;
    FrameHeader hdr = decode_header(frame, frame_type);
    w.set("frame_type",  frame_type == FRAME_SDFC_RESP ? "response" : "command");
    w.set("group_id",    hdr.group_id);
    w.set("unit_id",     hdr.unit_id);
    if (frame_type == FRAME_SDFC_RESP) w.set("status", hdr.status);

    switch (hdr.group_id) {
        case GROUP_SYSTEM_MGMT: parse_group_100(frame + hdr.payload_offset,
                                                 hdr.payload_len, hdr.unit_id, w); break;
        case GROUP_RF_DETECT:   parse_group_101(frame + hdr.payload_offset,
                                                 hdr.payload_len, hdr.unit_id, w); break;
        case GROUP_BITE:        parse_group_111(frame + hdr.payload_offset,
                                                 hdr.payload_len, hdr.unit_id, w); break;
        default: w.set("raw_hex", to_hex(frame + hdr.payload_offset, hdr.payload_len));
    }
    return w.release();
}

void free_result(const char* result) { delete[] result; }
```

The generated cases each have a `TODO` comment listing the field names + types from the ICD. The developer writes the decode body; the surrounding scaffolding is correct out of the box.

---

## 8. Generated TypeScript output

```typescript
// AUTO-GENERATED by tools/icd_codegen — do not edit
// Source: ICD_COMM_DF.xlsx  hw: comm_df

export interface CommDfSystemVersion {
  frameType: 'response';
  groupId: 100;
  unitId: 2;
  status: number;
  fwVersion: string;
  driverVersion: string;
  fpgaVersion: string;
  bspVersion: string;
  processorId: number;
  rfTunerIds: [number, number, number];
  fpgaTypeId: number;
}

export interface CommDfFftData {
  frameType: 'response';
  groupId: 101;
  unitId: 44;
  status: number;
  binCount: number;
  bins: number[];            // 1600 float32 values
}

export interface CommDfFhDetection {
  frameType: 'response';
  groupId: 101;
  unitId: 40;
  status: number;
  hopperCount: number;
  hoppers: Array<{
    channel: number;
    frequencyHz: number;
    powerDbm: number;
    dwellUs: number;
  }>;
}

// Union for all COMM DF messages — use in Angular component inputs
export type CommDfMessage =
  | CommDfSystemVersion
  | CommDfFftData
  | CommDfFhDetection
  // ... one per response type
  ;
```

Used by Mode B's Angular SPA (`Sg.Web`) to type-narrow incoming WebSocket messages by `groupId` / `unitId`.

---

## 9. Generated YAML profile

```yaml
# AUTO-GENERATED by tools/icd_codegen — safe to edit port/broker/topic
# Source: ICD_COMM_DF.xlsx  hw: comm_df  generated: 2026-04-11

name: comm_df
port: 5490                          # --port argument
kafka_topic: comm_df.drs.ui
kafka_broker: "${KAFKA_BROKER}"
parser_lib: parsers/libcomm_df.dll
max_connections: 4
health_interval_ms: 1000
receive_buffer_bytes: 65536         # sized to max response: FFT = 6,404 bytes
frame_terminator: "magic_bytes"
protocols: [sdfc_drs, scd_drs]
protocol_version: "ICD-COMM-DF-v1"
group_ids: [100, 101, 111]
store_fft: true                     # set false to drop FFT data before Kafka
```

The header comment marks the file as generated but explicitly permits manual edits to deployment-specific fields (`port`, `kafka_broker`, `kafka_topic`). The structural fields (`group_ids`, `protocol_version`) come from the ICD and should not drift.

---

## 10. Test plan

The tool ships with one reference fixture — the COMM DF ICD — to lock the parser's expected output. Tests:

1. `test_excel_reader.py` — given `fixtures/ICD_COMM_DF.xlsx`, assert the extracted `IcdDocument` contains exactly the expected frame formats, command groups, and field definitions.
2. `test_cpp_generator.py` — given a known `IcdDocument`, render the C++ skeleton and assert it compiles (via a minimal `g++` call) and contains the expected `case` blocks.
3. `test_yaml_generator.py` — same idea, assert YAML is valid + has the expected keys.
4. `test_typescript_generator.py` — assert TS compiles via `tsc --noEmit`.

All tests run offline. CI gate is "tests pass + tool compiles its own reference fixture."

---

## 11. Offline vendoring

Both runtime dependencies are small wheels:

```
openpyxl==3.1.5        # ~250 KB wheel
jinja2==3.1.4          # ~175 KB wheel
markupsafe==2.1.5      # jinja2 dependency, ~25 KB
```

Vendor under `tools/icd_codegen/packages/` and install with:

```bash
pip install --no-index --find-links=packages -r requirements.txt
```

No internet access required on the development machine.

---

## 12. Effort estimate

- Reader (`excel_reader.py`): ~150 LOC, ~2 days. Most of the work is robust handling of cell types and missing columns.
- Generators (3 × ~80 LOC): ~3 days total. Mostly Jinja templates.
- CLI + tests: ~1 day.
- Reference-ICD fixture + parser-output round-trip test: ~1 day.

**Total: ~1 week of one Python developer.**

The break-even is around 3 hardware variants — at variant 4+, the tool has paid for itself in skeleton-typing time saved. With 12+ variants planned, the ROI is unambiguous.

---

## 13. Decision: build, defer, or drop?

| Choice | When this is right |
|---|---|
| **Build now** (recommended) | If the v2 hardening phase is funded and 12+ variants are in scope. ~1 week of work returns ~3 days saved per variant from variant 4 onward. |
| **Defer to "we'll see"** | If only 2–3 variants will be built before scope review. Hand-write those; revisit the tool when the variant count climbs. |
| **Drop** | If the ICDs are too non-standard for shared sheet conventions and `--sheet-map` overrides aren't enough. Empirically unlikely for the existing variants but worth confirming against the customer's ICDs before committing. |

**Default recommendation: build, in parallel with the first variant's hand-implementation.** Use the first variant as the test fixture; second variant is the first to benefit from the tool. This avoids the "tool built in isolation, doesn't fit reality" failure mode.

---

## 14. References

- [Architecture Overview §3.6](../architecture-overview.md#36-drs-bridge--hardware-bridges) — drs-bridge component summary.
- [Developer Handbook §9](../developer-handbook.md#9-how-to-add-a-new-hardware-variant-telemetry-phase) — the add-a-variant procedure that this tool accelerates.
- [Developer Handbook §11](../developer-handbook.md#12-drs-bridge-internal-design) — drs-bridge internal layered model + the C++ parser ABI this tool's output conforms to.
- [v2 tech-stack archive §20](v2-tech-stack-archive.md) — the original specification, archived.
