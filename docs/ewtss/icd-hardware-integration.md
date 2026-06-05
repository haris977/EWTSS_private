# Hardware Integration Overview — CA120 + DDF-550 + DDF-1GTX

This document describes how the three R&S hardware devices relate to each other, how they
connect into the EWTSS v2 drs-bridge layer, and which parser DLL handles each TCP channel.

---

## 1. Physical Relationship Between Devices

In a real R&S EW installation:

```
                     ┌───────────────────────────────────┐
                     │         CA120 Server               │
                     │  (Multichannel Signal Analysis)    │
                     │                                     │
                     │  Aggregates data from:              │
                     │  • DDF-550 (via internal link)      │
                     │  • EB500 receivers                  │
                     │  • ESME receivers                   │
                     └────────────────┬────────────────────┘
                                      │ (internal R&S link)
              ┌───────────────────────┴────────────────────┐
              │                                             │
     ┌────────┴──────────┐                    ┌────────────┴────────────┐
     │    DDF-550         │                    │       DDF-1GTX           │
     │  Wideband DF       │                    │  HF High-Speed DF        │
     └───────────────────┘                    └─────────────────────────┘
```

CA120 can aggregate data from attached DDF-550 and other receivers. However, from
**EWTSS v2's perspective**, the drs-bridge connects independently to each device's TCP
ports — there is no shared connection or combined protocol.

---

## 2. EWTSS v2 drs-bridge Connection Map

Each hardware device exposes multiple TCP ports. The drs-bridge spawns one connection
per port and calls the matching parser DLL.

```
EWTSS v2 drs-bridge
│
├── CA120 (192.168.x.x)
│   ├── :9001  → XML control     → ca120_parser.dll  (raw XML, no wrapper)
│   └── :>9200 → AMMOS binary    → ca120_parser.dll  (AMMOS frames, magic 0xFB746572)
│
├── DDF-550 (192.168.x.y)
│   ├── :9150  → XML control     → ddf550_parser.dll (XML in [MW][Len][XML][MW] wrapper)
│   ├── :9152  → EB200 binary    → ddf550_parser.dll (EB200 packets, Selector 0x4242)
│   ├── :9153  → Preclassifier   → ddf550_parser.dll (raw XML)
│   └── :9154  → Preclass output → ddf550_parser.dll (raw XML FORMAT02)
│
└── DDF-1GTX (192.168.x.z)
    ├── :9150  → XML control     → ddf1gtx_parser.dll
    ├── :9152  → EB200 binary    → ddf1gtx_parser.dll
    ├── :9153  → Preclassifier   → ddf1gtx_parser.dll
    └── :9154  → Preclass output → ddf1gtx_parser.dll
```

---

## 3. Binary vs XML Channel Breakdown

The "6 binary + XML" split in the requirements maps to:

| # | Hardware | Port | Format | Parser |
|---|---|---|---|---|
| Binary 1 | CA120 | >9200 (IF stream) | AMMOS binary | ca120_parser.dll |
| Binary 2 | CA120 | >9200 (spectrum) | AMMOS binary | ca120_parser.dll |
| Binary 3 | CA120 | >9200 (audio) | AMMOS binary | ca120_parser.dll |
| Binary 4 | DDF-550 | 9152 | EB200 binary | ddf550_parser.dll |
| Binary 5 | DDF-1GTX | 9152 | EB200 binary | ddf1gtx_parser.dll |
| Binary 6 | (spare / second CA120 stream type) | >9200 | AMMOS binary | ca120_parser.dll |
| XML 1 | CA120 | 9001 | Raw XML | ca120_parser.dll |
| XML 2 | DDF-550 | 9150 | Wrapped XML | ddf550_parser.dll |
| XML 3 | DDF-550 | 9154 | Raw XML (FORMAT02) | ddf550_parser.dll |
| XML 4 | DDF-1GTX | 9150 | Wrapped XML | ddf1gtx_parser.dll |
| XML 5 | DDF-1GTX | 9154 | Raw XML (FORMAT02) | ddf1gtx_parser.dll |

---

## 4. Protocol Quick-Reference

| Device | Port | Endian | Frame detection |
|---|---|---|---|
| CA120 AMMOS | >9200 | Little-endian | `MagicWord == 0xFB746572` at byte 0 |
| CA120 XML | 9001 | n/a (text) | Buffer until `</Reply>` or `</Request>` or `</Event>` |
| DDF-550/1GTX EB200 | 9152 | Big-endian | `Selector == 0x4242` at bytes 4–5 |
| DDF-550/1GTX XML control | 9150 | Big-endian (wrapper) | Length field at bytes 4–7 |
| DDF-550/1GTX Preclass output | 9154 | n/a (text) | Buffer until `</DFData>` closing tag |

---

## 5. JSON Output Schema — Common Fields

All DLLs emit a `hw` field so the Kafka consumer knows which device produced the data:

| `hw` value | Source |
|---|---|
| `"ca120"` | CA120 (AMMOS or XML) |
| `"ddf550"` | DDF-550 (EB200 or XML) |
| `"ddf1gtx"` | DDF-1GTX (EB200 or XML) |

All DLLs also emit a `stream` field identifying the data type:

| `stream` value | Source |
|---|---|
| `"if_data"` | CA120 AMMOS IF samples |
| `"spectrum"` | CA120 AMMOS FFT spectrum |
| `"audio"` | CA120 AMMOS or DDF EB200 audio |
| `"df"` | DDF EB200 direction-finding result |
| `"ifpan"` | DDF EB200 IF panorama |
| `"pscan"` | DDF EB200 panorama scan |
| `"preclassifier"` | DDF FORMAT02 emitter classification |
| `"xml_ctrl"` | Control-channel XML (for logging) |

---

## 6. DLL Interface Contract (all three DLLs)

The Python drs-bridge loads each DLL via `ctypes` and calls 4 C functions:

```
extract_frame(buf, len) → int
    > 0  : complete frame; value = bytes consumed
      0  : incomplete; wait for more TCP data
     -1  : bad magic / unrecognised frame; discard buffer

parse_message(buf, len) → void*
    Returns an opaque C++ object on the heap, or NULL on error.

format_response(msg, out_json, cap) → int
    Writes null-terminated JSON into out_json[0..cap-1].
    Returns bytes written (>0) or -1 on error.

free_result(msg) → void
    Must be called after format_response to release the heap object.
```

The Python caller pattern:

```python
n = lib.extract_frame(buf, len(buf))
if n > 0:
    msg = lib.parse_message(buf, n)
    if msg:
        lib.format_response(msg, json_buf, len(json_buf))
        kafka.send(json_buf.value)
        lib.free_result(msg)
    buf = buf[n:]   # slide window
```

---

## 7. DLL File Naming Convention

| DLL file | Handles |
|---|---|
| `ca120_parser.dll` | CA120 AMMOS binary + CA120 XML |
| `ddf550_parser.dll` | DDF-550 EB200 binary + DDF-550 XML (all 4 ports) |
| `ddf1gtx_parser.dll` | DDF-1GTX EB200 binary + DDF-1GTX XML (all 4 ports) |

DLLs are placed in the `drs-bridge/parsers/` directory and referenced by name in the
YAML hardware profile. The drs-bridge selects the DLL based on the `hw_variant` key
in the profile, not by runtime protocol detection.

---

## 8. Do the Three Devices Work Together?

**At hardware level:** Yes. CA120 physically aggregates data from DDF-550 (and other
receivers) via an internal R&S link. From CA120's XML interface you can query data that
originated on the DDF-550.

**At EWTSS v2 level:** They are independent. Each device has its own drs-bridge instance
and parser DLL. The EWTSS scenario engine correlates the resulting Kafka messages by
timestamp and frequency — the DLLs themselves have no knowledge of each other.

**Implication for the C++ developer:** Write and test each DLL independently.
Cross-device correlation is handled by the Python/Kafka layer, not the parser layer.

---

## 9. ICD Documents

| File | Device | Version |
|---|---|---|
| [icd-ca120.md](icd-ca120.md) | R&S CA120 | ICD V15 |
| [icd-ddf550.md](icd-ddf550.md) | R&S DDF-550 | ICD V13 |
| [icd-ddf1gtx.md](icd-ddf1gtx.md) | R&S DDF-1GTX | ICD V2 |
