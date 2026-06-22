# DRS-Bridge — Direct Communication Ports Reference

**Maintained by:** Claude (auto-updated from ICD reading sessions)
**Last updated:** 2026-06-18
**Purpose:** All port numbers that drs-bridge communicates with directly, per device variant.

---

## 1. DDF-550 (Wideband Direction Finder)
**Parser DLL:** `ddf550_parser.dll`
**Protocol:** Binary-wrapped XML (control) + EB200 binary (data)
**Endianness:** BIG-ENDIAN
**Device is SERVER** — drs-bridge connects out as client

| Port | Protocol | Channel | Direction | Notes |
|------|----------|---------|-----------|-------|
| TCP **9150** | Binary-wrapped XML | Control commands | bridge → device / device → bridge | `[MagicStart][Len][XML][MagicEnd]` framing |
| TCP **9152** | EB200 binary | Mass data stream | device → bridge | DF bearings, spectrum, audio, I/Q, GPS |
| TCP **9153** | Raw XML | Preclassifier control | bridge → DDF-CL | DDFCLCmdPort — no binary wrapper |
| TCP **9154** | Raw XML (FORMAT02) | Preclassifier output | DDF-CL → bridge | Classified emitters: Static/Burst/Hopper/Chirp |
| TCP **5563** | Binary-wrapped XML | Direct control | bridge → device | Bypasses SCIF (5555+8). Alternate to 9150 |
| TCP **5565** | EB200 binary | Direct mass data | device → bridge | Bypasses SCIF (5555+10). Alternate to 9152 |

**EB200 frame sync magic:** `0x000EB200`
**Trace data ports used in TraceEnable:** client specifies its own IP + listener port via XML command (device pushes EB200 to that address)

---

## 2. DDF-1GTX (High Speed Scanning HF Direction Finder)
**Parser DLL:** `ddf1gtx_parser.dll`
**Protocol:** Identical to DDF-550
**Endianness:** BIG-ENDIAN
**Device is SERVER** — drs-bridge connects out as client

| Port | Protocol | Channel | Direction | Notes |
|------|----------|---------|-----------|-------|
| TCP **9150** | Binary-wrapped XML | Control commands | bridge ↔ device | Same framing as DDF-550 |
| TCP **9152** | EB200 binary | Mass data stream | device → bridge | Same format as DDF-550 |
| TCP **9153** | Raw XML | Preclassifier control | bridge → DDF-CL | Same as DDF-550 |
| TCP **9154** | Raw XML (FORMAT02) | Preclassifier output | DDF-CL → bridge | Same as DDF-550 |

**Only differences from DDF-550:** eSPAN max = 30 MHz (vs 80 MHz), adds DFPAN_STEP_10HZ enum value.

---

## 3. CA120 (Multichannel Signal Analysis — aggregates DDF-550 / EB500 / ESME)
**Parser DLL:** `ca120_parser.dll`
**Protocol:** Raw XML (control) + AMMOS binary (data)
**Endianness:** LITTLE-ENDIAN (AMMOS)
**CA120 is SERVER** — drs-bridge connects out as client

| Port | Protocol | Channel | Direction | Notes |
|------|----------|---------|-----------|-------|
| UDP **7999** | Broadcast | Service discovery | CA120 → LAN | Bridge listens to learn CA120's control IP/port |
| TCP **9001** | Raw XML | Control & config | bridge ↔ CA120 | `<Request>/<Reply>/<Event>` — no binary wrapper |
| TCP **>9200** (dynamic) | AMMOS binary | Data streams | CA120 → bridge | Port negotiated over XML (DataStream start command). One socket per active stream |

**AMMOS frame sync magic:** `0xFB746572`
**AMMOS FrameLength unit:** 32-bit words (multiply × 4 for bytes)

**AMMOS stream types (requested via DataStream XML command):**

| `type=` value | AMMOS Section | Data |
|---|---|---|
| `tunerSpectrum` | 6.4 | FFT spectrum bins |
| `analogAudio` / `digitalAudio` | 6.2 | Demodulated audio |
| `ifData` | 6.1 | Raw I/Q samples |
| `demresult` | 6.5 | Demodulation results |
| `decoded` | 6.7 | Decoded bitstream/text |
| `hopDensity` | 6.8 | Frequency hopping density |
| `histogram` | 6.9 | Signal statistics |
| `image` | 6.6 | Time-domain I/Q snapshot |

---

## 4. DP-ECM-1071 HF (1–30 MHz HF Receiver-Processor-Exciter)
**Parser DLL:** `dp_ecm_hf_parser.dll`
**Protocol:** Data Patterns binary (command/response frames)
**Endianness:** LITTLE-ENDIAN (inferred, not explicitly stated in ICD)
**Device is SERVER** — drs-bridge connects out as client

| Port | Protocol | Channel | Direction | Notes |
|------|----------|---------|-----------|-------|
| TCP **10014** | Binary frames | SJC commands | bridge ↔ device | Spot Jammer Controller — groups 100/101/108/109/111/112 |
| TCP **10015** | Binary frames | MRX commands | bridge ↔ device | HF-RPE monitoring — groups 1/3/4/5/6/7 |
| TCP **10021** | Binary | IQ stream Ch1 | device → bridge | One port per IQ channel |
| TCP **10022** | Binary | IQ stream Ch2 | device → bridge | |
| TCP **10023** | Binary | IQ stream Ch3 | device → bridge | |
| TCP **10024** | Binary | IQ stream Ch4 | device → bridge | |
| TCP **10025** | Binary | IQ stream Ch5 | device → bridge | |
| TCP **10026** | Binary | IQ stream Ch6 | device → bridge | |
| TCP **10027** | Binary | IQ stream Ch7 | device → bridge | |
| TCP **10028** | Binary | IQ stream Ch8 | device → bridge | |

**Frame magic (cmd):** `AA AB BA BB` (header) / `CC CD DC DD` (footer)
**Frame magic (resp):** `EE EF FE FF` (header) / `FF FE EF EE` (footer)
**Frame overhead:** 16 bytes (cmd) / 18 bytes (resp) — NO CRC field

---

## 5. DP-ECM-1074 VU (30 MHz–6000 MHz VHF/UHF/SHF Receiver-Processor-Exciter)
**Parser DLL:** `dp_ecm_vu_parser.dll`
**Protocol:** Data Patterns binary (same family as DP-ECM-1071)
**Endianness:** LITTLE-ENDIAN (inferred)
**Device is SERVER** — drs-bridge connects out as client

| Port | Protocol | Channel | Direction | Notes |
|------|----------|---------|-----------|-------|
| TCP **10014** | Binary frames | SRx commands | bridge ↔ device | Search/wideband detection — large group numbers (100/101/200...) |
| TCP **10015** | Binary frames | MRx commands | bridge ↔ device | Monitoring/ESM — small group numbers (1/3/4/5/6/7) |

**Same frame format family as DP-ECM-1071.** Key differences: wider band (30–6000 MHz), adds Exciter/Jamming + SHF groups, no dedicated IQ streaming ports defined in ICD.

---

## 6. COMM DF (Reference Variant — Data Patterns binary, worked example)
**Parser DLL:** `comm_df_parser.dll` (reference / template)
**Protocol:** Data Patterns binary (same family as DP-ECM variants)
**Endianness:** LITTLE-ENDIAN
**Bridge is SERVER** — device connects in to bridge

| Port | Protocol | Channel | Direction | Notes |
|------|----------|---------|-----------|-------|
| TCP **5490** | Binary frames | SDFC↔DRS commands | device → bridge | Bridge listens (0.0.0.0:5490). YAML-configured, not ICD-fixed |

**Three frame types on same port:**
- SDFC→DRS command: magic `AA AB BA BB` / `CC CD DC DD`
- SDFC→DRS response: magic `EE EF FE FF` / `FF FE EF EE`
- SCD compact: magic `AA AA` / `EE EE`

**Second-byte disambiguation:** both SDFC cmd and SCD share first byte `0xAA` — check byte 2 (`0xAB` vs `0xAA`) to identify frame type.

---

## 8. ESME (R&S Enhanced Spectrum Monitoring Equipment — MONUV subsystem)
**Parser DLL:** `esme_parser.dll`
**Protocol:** EB200 binary (data, default) + AMMOS AIF/DDCE binary (data, alternative) + SCPI text (control)
**Endianness:** BIG-ENDIAN for EB200 and VITA 49.0 | LITTLE-ENDIAN for AMMOS AIF/DDCE
**ESME is SERVER** — drs-bridge connects out as client for control and TCP data; bridge listens for UDP data

| Port | Protocol | Channel | Direction | Notes |
|------|----------|---------|-----------|-------|
| TCP **5555** | SCPI text (IEEE 488.2) | Control / configuration | bridge → ESME | Default. Configure data paths here before expecting any data |
| TCP **5565** (5555+10) | EB200 binary stream | Mass data (TCP path) | ESME → bridge | Default TCP data port. Bridge connects and sends 1 keep-alive byte to activate |
| UDP **any** (SCPI-configured) | EB200 binary datagrams | Mass data (UDP path) | ESME → bridge | Bridge listens on a port it chooses; registers it via `TRAC:UDP:TAG "<ip>",<port>,<tags>` |
| UDP **17222** | EB200 binary datagrams | DFPan / IFPan streaming (typical) | ESME → bridge | Commonly used example port for DFPan in RDFS. Bridge must open socket before sending SCPI |

**SCPI to start EB200 DFPan stream (typical RDFS setup):**
```
TRAC:UDP:TAG "10.0.0.1", 17222, DFPan
TRAC:UDP:FLAG "10.0.0.1", 17222, "DFLevel", "AZImuth", "DFQuality", "OPTional"
```

**EB200 frame sync magic:** `0x000EB200` (big-endian on wire → `ntohl()` → 0x000EB200)
**AMMOS frame sync magic:** `0xFB746572` (little-endian on wire → no swap on x86)

**Key EB200 trace tag numbers:**
| Tag | Name | RDFS relevance |
|-----|------|---------------|
| 101 | FScan | Medium |
| 501 | IFPan | HIGH |
| 901 | IF (I/Q) | HIGH |
| 1201 | PScan | Medium |
| 1401 | **DFPan** | **CRITICAL — bearing data** |
| 1801 | GPSCompass | HIGH |

**TCP data port formula:** TCP data port = SCPI port + 10 (e.g., 5555 + 10 = 5565).
**SWAP flag:** Sending `TRAC:UDP:FLAG "<ip>",<port>,"SWAP"` switches EB200 payload to little-endian (expensive for ESME; prefer native big-endian and byte-swap in the parser).
**AMMOS alternative:** Use `TRAC:TCP:TAG:ON <ip>,<port>,AIF` to receive AMMOS AIF frames instead of EB200.

---

## 7. RDFS (Reference variant from rdfs.yaml)
**Parser DLL:** `rdfs/parser.dll`
**Bridge is SERVER** — device connects in

| Port | Protocol | Channel | Direction | Notes |
|------|----------|---------|-----------|-------|
| TCP **5001** | Binary | Command | device → bridge | Bridge listens (0.0.0.0:5001) |
| UDP **5002** | Binary | Response | bridge → device | Bridge sends out |

---

## 9. HIMSHAKTI RSEC (Radar Segment Entity Controller — Subsystem Interfaces)
**Parser DLL:** `rsec.dll` ✓ built (`drs-bridge/parsers/rsec/build/Debug/rsec.dll`)
**Protocol:** Custom DLRL binary (4 frame variants) + NMEA 0183 ASCII
**Endianness:** **UNCONFIRMED — VERIFICATION PENDING**
**Ports:** NOT defined in IRS — obtain from RSEC integration team / deployment config
**Role:** drs-bridge connects out as client to each subsystem (bridge is SERVER when replacing a subsystem in test)

> **⚠ ENDIANNESS STATUS (as of 2026-06-17)**
> IRS v1.0 (DLRL/HIMSHAKTI/RSEC/2025/IRS) is **silent on byte order** for Variant A and B binary frames.
> The parser is currently coded as **BIG-ENDIAN** (DLRL convention, network byte order).
> **Action required before live integration:** verify with ESMP team or against a live packet capture.
> If confirmed little-endian, flip all `load_be16/be32` and `store_be16/be32` calls to `load_le16/le32` in `rsec_parser.cpp` — all helpers are in one place.
> Variant C (SCU) and Variant D (GNSS) are unaffected — no multi-byte fields.

### 4 Frame Variants on Wire

| Variant | Used with | SOM | EOM | Header size | Notes |
|---------|-----------|-----|-----|-------------|-------|
| **A — Main** | ESMP, ECMP, CC | `0xAAAA` (2 B) | `0xEEEE` (2 B) | 10 B | Header: SOM + CmdCode UINT16 + SeqNo UINT16 + BodyLen UINT32 |
| **B — BB Rx / RFPS** | BB Rx, RFPS | `0xAAABBABB` (4 B) | `0xCCCDDCDD` (4 B) | 12 B | Header: SOM + BodyLen UINT32 + CmdGroup 0x64 UINT16 + CmdUnitID UINT16 |
| **C — SCU servo** | SCU (JHB-E only) | `0x24` `$` (1 B) | `0x0D` CR (1 B) | per-cmd | 1-byte fields throughout; XOR checksum = XOR(DataLen … byte before checksum) |
| **D — GNSS** | GNSS receiver | `$` (1 B) | `*XX\r\n` | NMEA sentence | Comma-delimited ASCII; checksum = XOR between `$` and `*` |

**Variant A full wire layout:**
```
[0xAAAA 2B][CmdCode UINT16 2B][SeqNo UINT16 2B][BodyLen UINT32 4B][ body ][0xEEEE 2B]
```
**Variant B full wire layout:**
```
[0xAAABBABB UINT32 4B][BodyLen UINT32 4B][CmdGroup 0x64 UINT16 2B][CmdUnitID UINT16 2B][ body ][0xCCCDDCDD UINT32 4B]
```

### Subsystem Port Map (ports TBD — not in IRS)

| Subsystem | Frame | Direction | Channel |
|-----------|-------|-----------|---------|
| **ESMP** (ES Measure Processor) | A | bridge ↔ ESMP | ESM commands (RSEC→ESMP) + Active Track 0x1502 + Operational Data 0x1504 (ESMP→RSEC) |
| **ECMP** (EA/ECM Processor) | A | bridge ↔ ECMP | Jam commands 0x1101–0x1106 (RSEC→ECMP) + EA Operational Data 0x1153 (ECMP→RSEC) |
| **BB Rx** (2.2–18 GHz wideband) | B | bridge → BB Rx | SFB selection 0x1126, Sector Blank 0x1127, CAL on/off 0x1128 |
| **RFPS** (Fingerprint System) | B + shared FS | bridge ↔ RFPS | IQ logging trigger 0x1009 (via ESMP) + Fingerprint Response 3507 (RFPS→RSEC) |
| **SCU** (Servo — JHB-E only) | C | bridge ↔ SCU | Position 0x5001, Spin 0x5002, Sector Scan 0x5003, Stop 0x5006 |
| **GNSS** | D NMEA | GNSS → bridge | `$GPRMC` / `$GPGGA` / `$GPHDT` inbound |
| **CC** (Control Centre) | A | CC ↔ bridge | Integrated-mode mission commands |

**Key command codes (Variant A — main protocol):**

| Code | Direction | Description |
|------|-----------|-------------|
| 0x100F | RSEC→ESMP | Restart Emitter Processing |
| 0x1003 | RSEC→ESMP | Load Warner Library (up to 500 entries; ESMP stops sending periodic data during load) |
| 0x1005 | RSEC→ESMP | Set Lockout Frequency Bands (up to 16 bands) |
| 0x1118 | RSEC→ESMP | Set Scan Bands |
| 0x1502 | ESMP→RSEC | Active Track Data (periodic, 1 Hz per track — **never ACK**) |
| 0x1504 | ESMP→RSEC | Operational Data heartbeat (1 Hz — **never ACK**) |
| 0x1101 | RSEC→ECMP | Track Command (semi-auto, tracks 1–500) |
| 0x1103 | RSEC→ECMP | Track & Jam |
| 0x1105 | RSEC→ECMP | Stop Jam |
| 0x1104 | RSEC→ECMP | Break Track |
| 0x1153 | ECMP→RSEC | EA Operational Data (live jam status) |
| 0x150A | RSEC→ESMP | ACK/NACK |
| 0x1509 | ESMP→RSEC | ACK/NACK |

**Track ID namespaces:** 1–500 = ESM-correlated (semi-auto) · 501–550 = manually-entered (manual EA mode). Never mix them.

---

## 10. AUS Protocol Analyser (DRS HTTP Endpoint)
**Role in AUS system:** drs-bridge acts as the "Protocol Analyser" — AUS-C2 (Command Post) pushes aggregated detection events to the bridge over HTTP POST in JSON format
**Protocol:** HTTP, JSON body
**Endianness:** N/A (JSON text)
**Bridge is SERVER** — AUS-C2 connects out to bridge as HTTP client

| Port | Protocol | Channel | Direction | Notes |
|------|----------|---------|-----------|-------|
| HTTP **5000** | HTTP POST, JSON | Detection events | AUS-C2 → bridge | Endpoint: `POST /api/v4.2/system`. AUS-C2 IP on Himashakti subnet: `192.168.23.60`. Bridge IP: `192.168.23.100`. JSON schema in `API Server_PROTOCOL_ANALYSER.json` |

**Subnet context:** The bridge and AUS-C2 are on the same `/24` subnet (`192.168.23.0/24`) — the same NIC/VLAN that carries the Himashakti TCP:5010 traffic. The bridge is **not** on the Radar, Camera, or GNSS subnets.

**What AUS-C2 sends:** fused detection events from all sensors — RF detections from Himashakti (RFDDS/SKYCOPE), radar tracks from CDU, and GNSS/compass own-position. The bridge sees the post-fusion picture, not raw sensor frames.

---

## Port Quick-Reference Summary

| Port | Device | Channel |
|------|--------|---------|
| UDP 7999 | CA120 | Service discovery (broadcast) |
| TCP 5001 | RDFS | Command (bridge listens) |
| UDP 5002 | RDFS | Response (bridge sends) |
| TCP 5490 | COMM DF | Command/response (bridge listens) |
| TCP 5555 | **ESME** | **SCPI control (bridge → ESME)** |
| TCP 5563 | DDF-550 | Direct control (bypass SCIF) |
| TCP 5565 | DDF-550 / **ESME** | Direct mass data (bypass SCIF) / **EB200 TCP data stream (ESME default)** |
| UDP any (SCPI-set) | **ESME** | **EB200 / AMMOS data datagrams (bridge listens on self-chosen port)** |
| TCP 9001 | CA120 | XML control |
| TCP 9150 | DDF-550 / DDF-1GTX | XML control (binary-wrapped) |
| TCP 9152 | DDF-550 / DDF-1GTX | EB200 mass data |
| TCP 9153 | DDF-550 / DDF-1GTX | Preclassifier control |
| TCP 9154 | DDF-550 / DDF-1GTX | Preclassifier output (FORMAT02) |
| TCP >9200 | CA120 | AMMOS data streams (dynamic) |
| TCP 10014 | DP-ECM-1071 HF | SJC port |
| TCP 10014 | DP-ECM-1074 VU | SRx port |
| TCP 10015 | DP-ECM-1071 HF | MRX port |
| TCP 10015 | DP-ECM-1074 VU | MRx port |
| TCP 10021–10028 | DP-ECM-1071 HF | IQ streaming Ch1–Ch8 |
| TCP **TBD** ⚠ | HIMSHAKTI RSEC — ESMP | ESM commands + Active Track 0x1502 + Operational Data 0x1504 |
| TCP **TBD** ⚠ | HIMSHAKTI RSEC — ECMP | EA jam commands + EA Operational Data 0x1153 |
| TCP **TBD** ⚠ | HIMSHAKTI RSEC — BB Rx | Wideband Rx config (Variant B: 0xAAABBABB/0xCCCDDCDD) |
| TCP **TBD** ⚠ | HIMSHAKTI RSEC — RFPS | Fingerprint reports (Variant B) |
| serial/TCP **TBD** ⚠ | HIMSHAKTI RSEC — SCU | Servo control (JHB-E entity only; Variant C: $/CR frames) |
| HTTP **5000** | AUS Protocol Analyser | Detection events from AUS-C2 (bridge listens; AUS-C2 POSTs JSON to `/api/v4.2/system`) |

---

## Endianness Master Table

> **Why this matters for your DLL code:** wrong endianness is silent — the frame parses without crashing but every multi-byte value is garbage. The rule is: call `load_be16/be32/be64` for R&S devices and `load_le16/le32/le64` for Data Patterns devices. Never `reinterpret_cast` a multi-byte field directly.

### Per-device, per-channel breakdown

| Device | Channel / Port | Wire Endianness | ICD Status | What needs byte-swapping on x86 |
|--------|---------------|-----------------|------------|----------------------------------|
| **DDF-550** | TCP 9150 — XML wrapper (MagicStart, Length, MagicEnd) | **BIG-ENDIAN** | Explicit — §3.3.1 "network byte order" | All INT32 wrapper fields |
| **DDF-550** | TCP 9150 — XML payload text | n/a (UTF-8 text) | n/a | Nothing — text only |
| **DDF-550** | TCP 9152 — EB200 header | **BIG-ENDIAN** | Explicit — §5.1.7.1 "Network Order" | MagicNumber, VersionMinor/Major, SeqNumLow/High, DataSize |
| **DDF-550** | TCP 9152 — EB200 Generic Attribute | **BIG-ENDIAN** | Explicit | TraceTag, Length, reserved fields |
| **DDF-550** | TCP 9152 — EB200 Trace Attribute (conventional) | **BIG-ENDIAN** | Explicit | NumberOfTraceItems (INT16), SelectorFlags (UINT32) |
| **DDF-550** | TCP 9152 — EB200 Trace Attribute (advanced) | **BIG-ENDIAN** | Explicit | NumberOfTraceItems (UINT32), OptionalHeaderLength, SelectorFlagsLow/High |
| **DDF-550** | TCP 9152 — EB200 DFPScan payload data | **BIG-ENDIAN** | Explicit | Azimuth INT16 (÷10 = °), DF_Quality INT16, DF_Level INT16, Elevation INT16 |
| **DDF-550** | TCP 9152 — EB200 Audio optional header | **BIG-ENDIAN** | Explicit | AudioMode INT16, FrameLength INT16, Freq_low/high UINT32, Bandwidth UINT32, OutputTimestamp UINT64 |
| **DDF-550** | TCP 9153/9154 — Preclassifier XML | n/a (UTF-8 text) | n/a | Nothing — text only |
| **DDF-1GTX** | All ports | **BIG-ENDIAN** | Identical to DDF-550 | Same as DDF-550 |
| **CA120** | TCP 9001 — XML control | n/a (UTF-8 text) | n/a | Nothing — text only |
| **CA120** | TCP >9200 — AMMOS Frame_Header | **LITTLE-ENDIAN** | Inferred (Windows host, x86 vendor) — **confirm against live capture** | MagicWord UINT32, FrameLength UINT32, FrameCount UINT32, FrameType UINT32, DataHeaderLength UINT32, Reserved UINT32 |
| **CA120** | TCP >9200 — AMMOS IF_Data_Header | **LITTLE-ENDIAN** | Inferred | bigtimeTimeStamp INT64, TunerFrequency_Low/High UINT32, Bandwidth UINT32, Samplerate UINT32, Ext_bigtimeStartTimeStamp INT64 |
| **CA120** | TCP >9200 — AMMOS Audio_Data_Header | **LITTLE-ENDIAN** | Inferred | Samplerate UINT32, StatusWord UINT32 |
| **CA120** | TCP >9200 — AMMOS payload (I/Q samples, spectrum bins) | **LITTLE-ENDIAN** | Inferred | All sample arrays |
| **DP-ECM-1071 HF** | TCP 10014/10015 — cmd frame header | **LITTLE-ENDIAN** | Inferred (ICD silent — **confirm against live capture**) | MessageSize UINT32, CommandGroupID UINT16, CommandUnitID UINT16 |
| **DP-ECM-1071 HF** | TCP 10014/10015 — cmd frame payload | **LITTLE-ENDIAN** | Inferred | All int/float/double fields in Message Data |
| **DP-ECM-1071 HF** | TCP 10014/10015 — response frame header | **LITTLE-ENDIAN** | Inferred | Status INT16, MessageSize UINT32, ResponseGroupID UINT16, ResponseUnitID UINT16 |
| **DP-ECM-1071 HF** | TCP 10021–10028 — IQ stream payload | **LITTLE-ENDIAN** | Inferred | All IQ sample pairs |
| **DP-ECM-1074 VU** | TCP 10014/10015 — all frames | **LITTLE-ENDIAN** | Inferred — same protocol family as HF | Same as DP-ECM-1071 |
| **COMM DF** | TCP 5490 — all frame types | **LITTLE-ENDIAN** | Explicit — §3.1 "uint32 LE size field" | MessageSize UINT32, GroupID UINT16, UnitID UINT16, Status INT16 |
| **ESME** | TCP 5555 — SCPI control text | n/a (UTF-8 text) | n/a | Nothing — text only |
| **ESME** | TCP 5565 / UDP any — EB200 header | **BIG-ENDIAN** | Explicit — R&S ESME UM §7.6.1 "network byte order" | MagicNumber UINT32, VersionMinor/Major UINT16, SeqNum_low/high UINT16, DataSize UINT32 |
| **ESME** | TCP 5565 / UDP any — EB200 GenericAttribute (conventional, Tag < 5000) | **BIG-ENDIAN** | Explicit | Tag UINT16, Length UINT16 |
| **ESME** | TCP 5565 / UDP any — EB200 GenericAttribute (advanced, Tag ≥ 5000) | **BIG-ENDIAN** | Explicit | Tag UINT16, reserved UINT16, Length UINT32, reserved[4] UINT32×4 |
| **ESME** | TCP 5565 / UDP any — EB200 TraceAttribute (conventional) | **BIG-ENDIAN** | Explicit | NumberOfTraceItems UINT16, ChannelNumber UINT8, OptionalHeaderLength UINT8, SelectorFlags UINT32 |
| **ESME** | TCP 5565 / UDP any — EB200 DFPanTraceHeader payload | **BIG-ENDIAN** | Explicit | Freq_low/high UINT32, FreqSpan UINT32, DFThreshold INT32, Azimuth INT16 (÷10=°), Level INT16 (÷10=dBµV), Quality INT16 (÷10=%), OutputTimestamp UINT64 (ns since 1970, NO leap seconds) |
| **ESME** | TCP 5565 / UDP any — EB200 PeriodicTraceData arrays | **BIG-ENDIAN** | Explicit | All INT16/INT32/UINT32 measurement values (azimuth, level, quality, freq). Data order follows ascending SelectorFlags bit positions |
| **ESME** | TCP/UDP any — AMMOS AIF Frame Header | **LITTLE-ENDIAN** | Explicit — R&S ESME UM §7.7 | MagicWord UINT32 (0xFB746572), FrameLength UINT32 (in 32-bit words ×4=bytes), FrameCount UINT32, FrameType UINT32 |
| **ESME** | TCP/UDP any — AMMOS AIF Data Header | **LITTLE-ENDIAN** | Explicit | uintTimeStampLow/High UINT32 (µs since 1970, NO leap seconds), uintFrequencyLow/High UINT32, uintBandwidth UINT32, uintSamplerate UINT32 |
| **ESME** | TCP/UDP any — AMMOS I/Q payload samples | **LITTLE-ENDIAN** | Explicit | All INT16 (16-bit mode) or INT32 (32-bit mode) sample pairs, interleaved I₁Q₁I₂Q₂… |
| **HIMSHAKTI RSEC** | TCP TBD — Variant A header (SOM 0xAAAA, CmdCode, SeqNo, BodyLen, EOM 0xEEEE) | **BIG-ENDIAN (probable)** | IRS v1.0 silent — inferred from DLRL convention; **must confirm with ESMP team before integration** | SOM UINT16, CmdCode UINT16, SeqNo UINT16, BodyLen UINT32, EOM UINT16 |
| **HIMSHAKTI RSEC** | TCP TBD — Variant A body multi-byte fields | **BIG-ENDIAN (probable)** | Same as above | UINT16 freq fields (Frequency, Start_Freq, Stop_Freq in 0x100F/0x1118 etc.); UINT32 in Active Track 0x1502 (Frequency KHz, PW ns, PRI µs×10, PRF Hz, TOFA s, TOLA s) |
| **HIMSHAKTI RSEC** | TCP TBD — Variant B header (SOM 0xAAABBABB, BodyLen, CmdGroup 0x64, CmdUnitID, EOM 0xCCCDDCDD) | **BIG-ENDIAN (probable)** | Same as above | SOM UINT32, BodyLen UINT32, CmdGroup UINT16, CmdUnitID UINT16, EOM UINT32 |
| **HIMSHAKTI RSEC** | SCU serial/TCP — Variant C all fields | **N/A — 1-byte fields only** | All DataLen, CmdCode, data bytes, and XOR checksum are single-byte; no multi-byte endianness concern | Nothing |
| **HIMSHAKTI RSEC** | GNSS — Variant D NMEA 0183 | **N/A — ASCII text** | Comma-delimited ASCII sentences | Nothing |
| **AUS Protocol Analyser** | HTTP 5000 — JSON body | **N/A — text (JSON)** | HTTP POST body is UTF-8 JSON | Nothing |

### One-line rule per DLL

```
ddf550_parser.dll    →  ALL binary fields:  load_be16 / load_be32 / load_be64
ddf1gtx_parser.dll   →  ALL binary fields:  load_be16 / load_be32 / load_be64
ca120_parser.dll     →  AMMOS binary only:  load_le16 / load_le32 / load_le64
                        XML (9001):         no swap needed (text)
dp_ecm_hf_parser.dll →  ALL binary fields:  load_le16 / load_le32 / load_le64
dp_ecm_vu_parser.dll →  ALL binary fields:  load_le16 / load_le32 / load_le64
comm_df_parser.dll   →  ALL binary fields:  load_le16 / load_le32 / load_le64
esme_parser.dll      →  EB200 path:         load_be16 / load_be32 / load_be64
                        AMMOS path:         load_le16 / load_le32 / load_le64
                        SCPI (TCP 5555):    no swap needed (text)
                        Switch on MagicWord: 0x000EB200 → BE path, 0xFB746572 → LE path
rsec.dll             →  Variant A/B binary:  load_be16 / load_be32 (CODED AS BE — ⚠ UNCONFIRMED; verify before live integration)
                        Variant C (SCU):      1-byte fields only; no multi-byte swap needed
                        Variant D (GNSS):     no swap needed (ASCII)
aus_parser           →  HTTP 5000 JSON:       no swap needed (text; use any JSON parser)
```

### Fields that are unit-scaled (not just endianness)

These fields need a unit conversion **after** the byte-swap:

| Device | Field | Raw unit | Divide by | Result |
|--------|-------|----------|-----------|--------|
| DDF-550 | Azimuth (DFPScan) | 1/10 degree | 10.0 | degrees |
| DDF-550 | DF_Quality | 1/10 percent | 10.0 | percent |
| DDF-550 | DF_Level, Elevation, DF_FSTRENGTH | 1/10 dBuV or 1/10 ° | 10.0 | dBuV / degrees |
| DDF-550 | iAttHoldTime (XML param) | 1/10 second | 10.0 | seconds |
| DDF-550 | AMMOS_IF KFactor (EB500/ESME) | 0.1 dB/m | 10.0 | dB/m |
| CA120 | AntennaVoltageRef (IF header) | 0.1 dBuV | 10.0 | dBuV |
| CA120 | bigtimeTimeStamp | µs since epoch | 1000.0 | ms, or keep µs |
| CA120 | Ext_bigtimeStartTimeStamp | ns since epoch | 1 | ns (already SI) |
| CA120 | FrameLength (AMMOS header) | 32-bit words | ×4 | bytes |
| HIMSHAKTI RSEC | DOA (Active Track 0x1502) | UINT16, 1/10 degree | 10.0 | degrees (0.0–359.9°) |
| HIMSHAKTI RSEC | PRI (Active Track 0x1502) | UINT32, µs × 10 | 10.0 | µs |
| HIMSHAKTI RSEC | SCU Azimuth (SCU feedback) | UINT16, steps of 0.0054° | × 0.0054 | degrees (0–360°) |

---

## Notes

- Ports marked as "YAML-configured" (COMM DF, RDFS) are assigned in the variant's `.yaml` profile and can change per deployment. The ICD does not fix them.
- Ports marked as "ICD-fixed" (DDF-550, CA120, DP-ECM) are defined by the device vendor and must match exactly.
- For DDF-550/1GTX the EB200 data actually flows to a client-specified IP:port (given in `TraceEnable` XML command) — port 9152 is where drs-bridge connects to the device to SET UP the stream, but the actual EB200 frames are pushed to whatever port the bridge told the device to use.
- `0x4242` mentioned in some architecture docs refers to the EB200 header VersionMinor field, not the frame sync magic (which is `0x000EB200`).

---

## AUS ICD Reading Order (for DRS-Bridge Developers)

AUS (Anti-UAV System) has 7 ICDs across different vendors. The DRS-bridge only directly implements **ICD #7 (Protocol Analyser / HTTP JSON endpoint)**, but understanding the other ICDs tells you what data AUS-C2 is aggregating before it POSTs to you. Read in this order:

### Step 1 — Start here: system overview
| File | What it gives you |
|------|-------------------|
| `AUS_ICD_COMPLETE_TEACHING.md` | Full system picture, all ports, the 5-phase kill chain (Detect→Track→Identify→Neutralize→Cessation), data-flow diagrams. Read this **before opening any raw ICD**. |

### Step 2 — Sensor ICDs (understand what AUS-C2 aggregates)
| File | ICD# | Subsystem | Why relevant to bridge |
|------|------|-----------|------------------------|
| `RFDDS_ICD.pdf` | #1 | Himashakti (RFDDS + GJSS + RFCMS) | RF detection fields in the JSON you receive — frequency, DOA, drone model, lat/lon — come from this subsystem's `Targets_Detected (202)` messages. Also the RFDDS in AUS is the same DLRL system as in the standalone RDFS deployment at HAL. |
| `ICD_CDU_AUS_C2_Updated25_April25_RADAR.pdf` | #2 | Radar CDU (4-panel AESA) | Radar track fields (position, velocity, classification) in the JSON come from `TGT_DATA (0xC002)`. Cartesian-to-lat/lon conversion happens inside AUS-C2 before posting to you. |
| `RUGGED GNSS RECEIVER ICD DOCUMNET_GNGGA_GPGGA_ONLY_COMPASS_ALSO.pdf` | #3/#4 | GNSS Receiver + Compass | Own-position and true-north. Not directly visible in detection JSON but used by AUS-C2 to geo-reference all other data. Skim only — NMEA `$GNGGA`/`$GPHDT` parsing is trivial. |
| `ICD file for AUS Camera_18_11.xlsx` | #5 | EOIR Camera (TI + Day + AVT + LRF + PTU) | Visual-ID metadata may appear in detection JSON. The camera has 6 subsystems on one TCP socket with different checksum algorithms per subsystem — tricky if you ever need to interface it directly. |

### Step 3 — Your direct interface
| File | ICD# | What to implement |
|------|------|-------------------|
| `API Server_PROTOCOL_ANALYSER.json` | #7 | **This is your actual job.** Defines the JSON schema for `POST /api/v4.2/system`. Implement an HTTP server on port 5000 at `192.168.23.100` that accepts this schema and passes parsed detections to the drs-bridge pipeline. |

### Key facts to keep in mind while reading
- AUS-C2 is the single integration point — it normalises all sensor data before JSON POST. Your bridge never talks to Himashakti, Radar, or Camera directly.
- Detection + Jamming are **mutually exclusive** on Himashakti: during active RF jamming, the `Targets_Detected (202)` stream stops. Expect gaps in incoming JSON during jam cycles.
- The Himashakti TCP:5010 binary protocol is raw struct packing with **unspecified endianness** — little-endian is probable (x86 SBC) but must be verified with Wireshark if you ever need to interface it directly.
- Coordinate encoding from Himashakti: lat/lon multiplied by 1,000,000 and sent as `int32` (`17123456 → 17.123456°`). AUS-C2 may decode these before posting to you — check the JSON schema for whether you receive raw integers or decimal degrees.
- `192.168.23.100` (bridge) and `192.168.23.60` (AUS-C2) are on the same subnet as Himashakti (`192.168.23.110`). The bridge only needs one NIC configured for this subnet.
